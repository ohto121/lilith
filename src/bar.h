#include <dirent.h>
#include <errno.h>
#include <fcft/fcft.h>
#include <fcntl.h>
#include <pixman.h>
#include <wlr/interfaces/wlr_buffer.h>

#if defined(__has_include)
#  if __has_include(<drm_fourcc.h>)
#    include <drm_fourcc.h>
#  endif
#endif
#ifndef DRM_FORMAT_ARGB8888
#define DRM_FORMAT_ARGB8888 0x34325241
#endif

#define BAR_MAXSLOTS 16
#define BAR_NBUF     3
#define BAR_TXT      192
#define BAR_NSOLID   64

typedef struct {
	struct wlr_buffer base;
	pixman_image_t *image;
	int pooled;
} BarBuffer;

typedef struct {
	int type, start, len, flex;
	int gap;
	int sep;
} BarSlot;

typedef struct {
	Monitor *m;
	pixman_image_t *img;
	int up;
	int edge_start;
	int cs, ct;
	uint32_t occ, urg, tagset, selmask;
	Client *sel;
	int tl[32];
	float sw[32];
	int slide;
	double pillx, pilll;
} BarCtx;

static struct fcft_font *bar_font;
static int bar_lrpad, bar_maxtagw;
static char bar_status[512];
static int bar_status_stdin;
static struct wl_event_source *bar_stdin_src, *bar_clock_src, *bar_sys_src, *bar_kick_src;
static Monitor *bar_hover;

static char bar_modtxt[BarModCount][BAR_TXT];
static int bar_modsch[BarModCount];
static int bar_modused[BarModCount];
static int bar_bat_ok, bar_bl_ok, bar_temp_ok;
static char bar_batcap[320], bar_batstat[320], bar_blcur[320], bar_temppath[320];
static long bar_blmax;

static int bar_vol_fd = -1, bar_vol_again;
static struct wl_event_source *bar_vol_src;
static char bar_vol_buf[256];
static size_t bar_vol_len;

static struct { uint32_t rgba; pixman_image_t *img; } bar_solids[BAR_NSOLID];
static int bar_solid_next;

static int bar_imin(int a, int b) { return a < b ? a : b; }
static int bar_imax(int a, int b) { return a > b ? a : b; }

static int bar_horiz(const Monitor *m) { return m->bar_pos == BarTop || m->bar_pos == BarBottom; }
static int bar_up(const Monitor *m)    { return !bar_horiz(m) && barvertmode == BarUpright; }
static int bar_rot(const Monitor *m)   { return !bar_horiz(m) && barvertmode != BarUpright; }

static uint64_t
bar_now_ms(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)ts.tv_nsec / 1000000ULL;
}

static int
bar_edge_start(const Monitor *m)
{
	switch (m->bar_pos) {
	case BarTop:    return 1;
	case BarBottom: return 0;
	case BarLeft:   return bar_up(m) || barvertmode == BarRotCCW;
	default:        return !bar_up(m) && barvertmode == BarRotCW;
	}
}

static void
barbuf_destroy(struct wlr_buffer *buffer)
{
	BarBuffer *bb = wl_container_of(buffer, bb, base);
	pixman_image_unref(bb->image);
	free(bb);
}

static bool
barbuf_begin(struct wlr_buffer *buffer, uint32_t flags, void **data,
		uint32_t *format, size_t *stride)
{
	BarBuffer *bb = wl_container_of(buffer, bb, base);
	if (flags & WLR_BUFFER_DATA_PTR_ACCESS_WRITE)
		return false;
	*data = pixman_image_get_data(bb->image);
	*format = DRM_FORMAT_ARGB8888;
	*stride = pixman_image_get_stride(bb->image);
	return true;
}

static void
barbuf_end(struct wlr_buffer *buffer)
{
}

static const struct wlr_buffer_impl barbuf_impl = {
	.destroy = barbuf_destroy,
	.begin_data_ptr_access = barbuf_begin,
	.end_data_ptr_access = barbuf_end,
};

static void
bar_pool_reset(Monitor *m)
{
	int i;

	for (i = 0; i < BAR_NBUF; i++) {
		if (m->bar_bufs[i]) {
			wlr_buffer_drop(m->bar_bufs[i]);
			m->bar_bufs[i] = NULL;
		}
	}
	m->bar_buf_w = m->bar_buf_h = 0;
	if (m->bar_scratch) {
		pixman_image_unref((pixman_image_t *)m->bar_scratch);
		m->bar_scratch = NULL;
	}
	m->bar_scratch_w = m->bar_scratch_h = 0;
}

static BarBuffer *
bar_newbuf(int w, int h, int pooled)
{
	BarBuffer *bb = ecalloc(1, sizeof(*bb));

	bb->image = pixman_image_create_bits(PIXMAN_a8r8g8b8, w, h, NULL, 0);
	if (!bb->image) {
		free(bb);
		return NULL;
	}
	bb->pooled = pooled;
	wlr_buffer_init(&bb->base, &barbuf_impl, w, h);
	return bb;
}

static BarBuffer *
bar_getbuf(Monitor *m, int w, int h)
{
	BarBuffer *bb;
	int i;

	if (m->bar_buf_w != w || m->bar_buf_h != h) {
		bar_pool_reset(m);
		m->bar_buf_w = w;
		m->bar_buf_h = h;
	}
	for (i = 0; i < BAR_NBUF; i++) {
		if (!m->bar_bufs[i]) {
			if (!(bb = bar_newbuf(w, h, 1)))
				return NULL;
			m->bar_bufs[i] = &bb->base;
		}
		if (m->bar_bufs[i]->n_locks == 0)
			return wl_container_of(m->bar_bufs[i], bb, base);
	}
	return bar_newbuf(w, h, 0);
}

static pixman_image_t *
bar_scratch(Monitor *m, int w, int h)
{
	if (m->bar_scratch && (m->bar_scratch_w != w || m->bar_scratch_h != h)) {
		pixman_image_unref((pixman_image_t *)m->bar_scratch);
		m->bar_scratch = NULL;
	}
	if (!m->bar_scratch) {
		m->bar_scratch = pixman_image_create_bits(PIXMAN_a8r8g8b8, w, h, NULL, 0);
		m->bar_scratch_w = w;
		m->bar_scratch_h = h;
	}
	return (pixman_image_t *)m->bar_scratch;
}

static pixman_color_t
bar_pcolor(uint32_t rgba)
{
	uint32_t r = (rgba >> 24) & 0xff, g = (rgba >> 16) & 0xff;
	uint32_t b = (rgba >> 8) & 0xff, a = rgba & 0xff;
	pixman_color_t c;
	c.red   = (uint16_t)((r * 257 * a) / 255);
	c.green = (uint16_t)((g * 257 * a) / 255);
	c.blue  = (uint16_t)((b * 257 * a) / 255);
	c.alpha = (uint16_t)(a * 257);
	return c;
}

static uint32_t
bar_premul(uint32_t rgba)
{
	uint32_t r = (rgba >> 24) & 0xff, g = (rgba >> 16) & 0xff;
	uint32_t b = (rgba >> 8) & 0xff, a = rgba & 0xff;
	return (a << 24) | ((r * a / 255) << 16) | ((g * a / 255) << 8) | (b * a / 255);
}

static uint32_t
bar_lerp(uint32_t a, uint32_t b, float t)
{
	uint32_t o = 0;
	int sh;

	if (t <= 0.f)
		return a;
	if (t >= 1.f)
		return b;
	for (sh = 0; sh < 32; sh += 8) {
		float ca = (float)((a >> sh) & 0xff), cb = (float)((b >> sh) & 0xff);
		o |= (uint32_t)(ca + (cb - ca) * t + 0.5f) << sh;
	}
	return o;
}

static float
bar_sqrtf(float v)
{
	float x;
	int i;
	if (v <= 0.f)
		return 0.f;
	x = v > 1.f ? v / 2 : 1.f;
	for (i = 0; i < 12; i++)
		x = 0.5f * (x + v / x);
	return x;
}

static void
bar_rect(pixman_image_t *img, int x, int y, int w, int h, uint32_t rgba)
{
	pixman_color_t c;
	pixman_box32_t box;
	int iw = pixman_image_get_width(img), ih = pixman_image_get_height(img);

	box.x1 = bar_imax(x, 0);
	box.y1 = bar_imax(y, 0);
	box.x2 = bar_imin(x + w, iw);
	box.y2 = bar_imin(y + h, ih);
	if (w <= 0 || h <= 0 || box.x1 >= box.x2 || box.y1 >= box.y2)
		return;
	c = bar_pcolor(rgba);
	pixman_image_fill_boxes(PIXMAN_OP_SRC, img, &c, 1, &box);
}

static void
bar_px_lerp(pixman_image_t *img, int x, int y, uint32_t pre, float cov)
{
	int w = pixman_image_get_width(img), h = pixman_image_get_height(img), sh;
	uint32_t *row, d, o = 0;

	if (x < 0 || y < 0 || x >= w || y >= h || cov <= 0.f)
		return;
	if (cov > 1.f)
		cov = 1.f;
	row = (uint32_t *)((uint8_t *)pixman_image_get_data(img)
			+ (size_t)y * (size_t)pixman_image_get_stride(img));
	d = row[x];
	for (sh = 0; sh < 32; sh += 8) {
		float dc = (float)((d >> sh) & 0xff), sc = (float)((pre >> sh) & 0xff);
		o |= (uint32_t)(dc + (sc - dc) * cov + 0.5f) << sh;
	}
	row[x] = o;
}

static void
bar_rrect(pixman_image_t *img, int x, int y, int w, int h, int r, uint32_t rgba)
{
	uint32_t pre;
	int cx, cy;

	if (w <= 0 || h <= 0)
		return;
	r = bar_imin(r, bar_imin(w, h) / 2);
	if (r <= 0) {
		bar_rect(img, x, y, w, h, rgba);
		return;
	}
	pre = bar_premul(rgba);
	bar_rect(img, x + r, y, w - 2 * r, h, rgba);
	bar_rect(img, x, y + r, r, h - 2 * r, rgba);
	bar_rect(img, x + w - r, y + r, r, h - 2 * r, rgba);
	for (cy = 0; cy < r; cy++) {
		for (cx = 0; cx < r; cx++) {
			float dx = (float)r - ((float)cx + 0.5f);
			float dy = (float)r - ((float)cy + 0.5f);
			float cov = (float)r - bar_sqrtf(dx * dx + dy * dy) + 0.5f;
			bar_px_lerp(img, x + cx, y + cy, pre, cov);
			bar_px_lerp(img, x + w - 1 - cx, y + cy, pre, cov);
			bar_px_lerp(img, x + cx, y + h - 1 - cy, pre, cov);
			bar_px_lerp(img, x + w - 1 - cx, y + h - 1 - cy, pre, cov);
		}
	}
}

static void
bar_edge(pixman_image_t *img, int up, int x, int y, int w, int h,
		int atstart, int th, uint32_t col)
{
	if (th <= 0)
		return;
	if (!up)
		bar_rect(img, x, atstart ? y : y + h - th, w, th, col);
	else
		bar_rect(img, atstart ? x : x + w - th, y, th, h, col);
}

static pixman_image_t *
bar_solid(uint32_t rgba)
{
	pixman_color_t c;
	int i;

	for (i = 0; i < BAR_NSOLID; i++)
		if (bar_solids[i].img && bar_solids[i].rgba == rgba)
			return bar_solids[i].img;
	i = bar_solid_next;
	bar_solid_next = (bar_solid_next + 1) % BAR_NSOLID;
	if (bar_solids[i].img)
		pixman_image_unref(bar_solids[i].img);
	c = bar_pcolor(rgba);
	bar_solids[i].rgba = rgba;
	bar_solids[i].img = pixman_image_create_solid_fill(&c);
	return bar_solids[i].img;
}

static uint32_t
bar_nextcp(const char **p)
{
	const unsigned char *s = (const unsigned char *)*p;
	uint32_t cp;
	int n, i;

	if (s[0] < 0x80) { cp = s[0]; n = 1; }
	else if ((s[0] & 0xE0) == 0xC0) { cp = s[0] & 0x1F; n = 2; }
	else if ((s[0] & 0xF0) == 0xE0) { cp = s[0] & 0x0F; n = 3; }
	else if ((s[0] & 0xF8) == 0xF0) { cp = s[0] & 0x07; n = 4; }
	else { *p += 1; return 0xFFFD; }
	for (i = 1; i < n; i++) {
		if ((s[i] & 0xC0) != 0x80) { *p += i; return 0xFFFD; }
		cp = (cp << 6) | (s[i] & 0x3F);
	}
	*p += n;
	return cp;
}

static int
bar_run(pixman_image_t *img, int x, int y0, int h, const char *s, size_t len,
		uint32_t fg, int xmax)
{
	const char *p = s, *end = s + len;
	uint32_t prev = 0;
	int x0 = x;
	pixman_image_t *solid = img ? bar_solid(fg) : NULL;
	int baseline = y0 + (h - bar_font->height) / 2 + bar_font->ascent;

	if (img && !solid)
		img = NULL;
	while (p < end) {
		uint32_t cp = bar_nextcp(&p);
		const struct fcft_glyph *g;
		long kx = 0;

		if (cp < 0x20)
			continue;
		if (!(g = fcft_rasterize_char_utf32(bar_font, cp, FCFT_SUBPIXEL_NONE)))
			continue;
		if (prev && fcft_kerning(bar_font, prev, cp, &kx, NULL))
			x += (int)kx;
		if (xmax >= 0 && x + g->advance.x > xmax)
			break;
		if (img) {
			int gx = x + g->x, gy = baseline - g->y;
			if (pixman_image_get_format(g->pix) == PIXMAN_a8r8g8b8)
				pixman_image_composite32(PIXMAN_OP_OVER, g->pix, NULL, img,
						0, 0, 0, 0, gx, gy, g->width, g->height);
			else
				pixman_image_composite32(PIXMAN_OP_OVER, solid, g->pix, img,
						0, 0, 0, 0, gx, gy, g->width, g->height);
		}
		x += g->advance.x;
		prev = cp;
	}
	return x - x0;
}

static int
bar_textw(const char *s, size_t len)
{
	return bar_run(NULL, 0, 0, 0, s, len, 0, -1);
}

static size_t
bar_fitlen(const char *s, size_t len, int maxw)
{
	const char *p = s, *end = s + len;
	uint32_t prev = 0;
	int x = 0;

	while (p < end) {
		const char *q = p;
		uint32_t cp = bar_nextcp(&q);
		const struct fcft_glyph *g;
		long kx = 0;

		if (cp < 0x20 || !(g = fcft_rasterize_char_utf32(bar_font, cp, FCFT_SUBPIXEL_NONE))) {
			p = q;
			continue;
		}
		if (prev && fcft_kerning(bar_font, prev, cp, &kx, NULL))
			x += (int)kx;
		if (x + g->advance.x > maxw)
			break;
		x += g->advance.x;
		prev = cp;
		p = q;
	}
	return (size_t)(p - s);
}

static void
bar_text_center(pixman_image_t *img, int x, int y0, int w, int h, const char *s,
		uint32_t fg)
{
	size_t len = strlen(s);
	int tw;

	if (w <= 0 || !len)
		return;
	tw = bar_textw(s, len);
	if (tw > w) {
		len = bar_fitlen(s, len, w);
		tw = bar_textw(s, len);
	}
	bar_run(img, x + (w - tw) / 2, y0, h, s, len, fg, -1);
}

static int
bar_status_run(pixman_image_t *img, int x, int y0, int h, const char *s,
		uint32_t dfg, uint32_t dbg, int xmax)
{
	uint32_t fg = dfg, bg = dbg;
	const char *p = s;
	int x0 = x, w;

	while (*p) {
		const char *seg;
		size_t len;

		if (p[0] == '^') {
			if (p[1] == 'd') {
				fg = dfg; bg = dbg; p += 2;
				continue;
			}
			if ((p[1] == 'f' || p[1] == 'b') && p[2] == 'g' && p[3] == '(') {
				char *e;
				unsigned long v = strtoul(p + 4, &e, 16);
				if (*e == ')') {
					v = (e - (p + 4) == 8) ? v : ((v << 8) | 0xff);
					if (p[1] == 'f') fg = (uint32_t)v; else bg = (uint32_t)v;
					p = e + 1;
					continue;
				}
			}
		}
		if (p[0] == '^' && p[1] == '^') {
			seg = p + 1;
			len = 1;
			p += 2;
		} else {
			const char *q = p + 1;
			while (*q && *q != '^')
				q++;
			seg = p;
			len = (size_t)(q - p);
			p = q;
		}
		w = bar_run(NULL, x, 0, 0, seg, len, fg, -1);
		if (img) {
			if (bg != dbg)
				bar_rect(img, x, y0, w, h, bg);
			bar_run(img, x, y0, h, seg, len, fg, xmax);
		}
		x += w;
	}
	return x - x0;
}

static int
bar_lines(const char *s)
{
	int n = 1;

	for (; *s; s++)
		if (*s == '\n')
			n++;
	return n;
}

static int
bar_thick(const Monitor *m)
{
	int t;

	if (barthickness > 0)
		return barthickness;
	if (bar_up(m))
		t = bar_maxtagw + bar_lrpad;
	else
		t = bar_font->height + 2 * barvpad;
	return t + 2 * bartaginset + 2 * barborderpx;
}

static int
bar_taglen(const Monitor *m, int i)
{
	const char *txt = bartags[i] ? bartags[i] : "";
	int l = bar_up(m) ? bar_font->height + bar_lrpad
			: bar_textw(txt, strlen(txt)) + bar_lrpad;
	return bar_imax(l, bartagminw);
}

static void
bar_tile(const BarCtx *k, int s, int l, int c, int cl, int *x, int *y, int *w, int *h)
{
	if (k->up) { *x = c; *y = s; *w = cl; *h = l; }
	else       { *x = s; *y = c; *w = l;  *h = cl; }
}

static void
bar_rotate(pixman_image_t *strip, pixman_image_t *dst, int L, int T, int cw)
{
	pixman_transform_t t;

	memset(&t, 0, sizeof(t));
	t.matrix[2][2] = pixman_fixed_1;
	if (cw) {
		t.matrix[0][1] = pixman_fixed_1;
		t.matrix[1][0] = -pixman_fixed_1;
		t.matrix[1][2] = pixman_int_to_fixed(T);
	} else {
		t.matrix[0][1] = -pixman_fixed_1;
		t.matrix[0][2] = pixman_int_to_fixed(L);
		t.matrix[1][0] = pixman_fixed_1;
	}
	pixman_image_set_transform(strip, &t);
	pixman_image_set_filter(strip, PIXMAN_FILTER_NEAREST, NULL, 0);
	pixman_image_composite32(PIXMAN_OP_SRC, strip, NULL, dst, 0, 0, 0, 0, 0, 0, T, L);
}

static void
bar_tagcol(const BarCtx *k, int i, uint32_t *fg, uint32_t *bg, uint32_t *ac)
{
	uint32_t bit = 1u << i;
	int urg = (k->urg & bit) != 0;
	int base = urg ? SchemeUrg : (baroccscheme && (k->occ & bit)) ? SchemeOcc : SchemeNorm;
	float f = urg ? 0.f : k->sw[i];
	uint32_t bfg = barcolors[base][0], bbg = barcolors[base][1];
	uint32_t bac = barcolors[base][2] ? barcolors[base][2] : bfg;
	uint32_t sfg = barcolors[SchemeSel][0], sbg = barcolors[SchemeSel][1];
	uint32_t sac = barcolors[SchemeSel][2] ? barcolors[SchemeSel][2] : sfg;

	*fg = bar_lerp(bfg, sfg, f);
	*ac = bar_lerp(bac, sac, f);
	*bg = k->slide ? bbg : bar_lerp(bbg, sbg, f);
}

static void
bar_tags_state(BarCtx *k, const int *px)
{
	Monitor *m = k->m;
	uint32_t ts = k->tagset & ((1u << TAGCOUNT) - 1);
	uint64_t now = bar_now_ms();
	double t, e = 1.0, tx = 0, tl = 0, x, l;
	int i, sel = -1, nsel = 0, slide;
	float tgt, w;

	for (i = 0; i < TAGCOUNT; i++) {
		if (ts & (1u << i)) {
			sel = i;
			nsel++;
		}
	}
	slide = baranimstyle == BarAnimSlide && nsel == 1;
	if (slide) {
		tx = px[sel];
		tl = k->tl[sel];
	}

	if (!m->bar_prevtags_ok) {
		m->bar_prevtags_ok = 1;
		m->bar_prevtags = ts;
		for (i = 0; i < TAGCOUNT; i++)
			m->bar_w[i] = (ts >> i) & 1u ? 1.f : 0.f;
	}
	if (ts != m->bar_prevtags) {
		m->bar_prevtags = ts;
		m->bar_anim_on = 0;
		if (baranimtag && baranimdur > 0.0) {
			if (baranimstyle == BarAnimSlide) {
				if (slide && m->bar_pill_ok) {
					m->bar_pill_fx = m->bar_pill_x;
					m->bar_pill_fl = m->bar_pill_l;
					m->bar_anim_on = 1;
				}
			} else {
				for (i = 0; i < TAGCOUNT; i++)
					m->bar_wfrom[i] = m->bar_w[i];
				m->bar_anim_on = 1;
			}
			m->bar_anim_ms = now;
		}
	}
	if (m->bar_anim_on) {
		t = (double)(now - m->bar_anim_ms) / (baranimdur * 1000.0);
		if (t >= 1.0) {
			m->bar_anim_on = 0;
			e = 1.0;
		} else {
			e = bezier_ease(baranimbezier, t < 0.0 ? 0.0 : t);
		}
	}

	k->slide = slide;
	if (slide) {
		if (m->bar_anim_on && m->bar_pill_ok) {
			x = m->bar_pill_fx + (tx - m->bar_pill_fx) * e;
			l = m->bar_pill_fl + (tl - m->bar_pill_fl) * e;
		} else {
			x = tx;
			l = tl;
		}
		m->bar_pill_x = k->pillx = x;
		m->bar_pill_l = k->pilll = l;
		m->bar_pill_ok = 1;
		for (i = 0; i < TAGCOUNT; i++) {
			double a, b, ov;
			if (!k->tl[i]) {
				k->sw[i] = 0.f;
				continue;
			}
			a = (double)px[i] > x ? (double)px[i] : x;
			b = (px[i] + k->tl[i]) < (x + l) ? (double)(px[i] + k->tl[i]) : x + l;
			ov = b - a;
			k->sw[i] = ov <= 0 ? 0.f : (float)(ov / k->tl[i]);
			if (k->sw[i] > 1.f)
				k->sw[i] = 1.f;
		}
	} else {
		m->bar_pill_ok = 0;
		for (i = 0; i < TAGCOUNT; i++) {
			tgt = (ts >> i) & 1u ? 1.f : 0.f;
			if (baranimstyle == BarAnimFade && m->bar_anim_on)
				w = m->bar_wfrom[i] + (tgt - m->bar_wfrom[i]) * (float)e;
			else
				w = tgt;
			if (w < 0.f)
				w = 0.f;
			if (w > 1.f)
				w = 1.f;
			m->bar_w[i] = k->sw[i] = w;
		}
	}
}

static void
bar_draw_tags(BarCtx *k, int pos)
{
	Monitor *m = k->m;
	pixman_image_t *img = k->img;
	int i, es = k->edge_start, p = pos, px[32];
	int tx, ty, tw, th;
	uint32_t fg, bg, ac;

	for (i = 0; i < TAGCOUNT; i++) {
		px[i] = p;
		if (k->tl[i])
			p += k->tl[i] + bartaggap;
	}
	bar_tags_state(k, px);

	for (i = 0; i < TAGCOUNT; i++) {
		if (!k->tl[i])
			continue;
		bar_tagcol(k, i, &fg, &bg, &ac);
		if (bg & 0xff) {
			bar_tile(k, px[i], k->tl[i], k->cs + bartaginset, k->ct - 2 * bartaginset,
					&tx, &ty, &tw, &th);
			bar_rect(img, tx, ty, tw, th, bg);
		}
	}

	if (k->slide) {
		uint32_t pbg = barcolors[SchemeSel][1];
		if (pbg & 0xff) {
			bar_tile(k, (int)(k->pillx + 0.5), (int)(k->pilll + 0.5),
					k->cs + bartaginset, k->ct - 2 * bartaginset, &tx, &ty, &tw, &th);
			bar_rect(img, tx, ty, tw, th, pbg);
		}
		for (i = 0; i < TAGCOUNT; i++) {
			if (!k->tl[i] || !(k->urg & (1u << i)))
				continue;
			bar_tagcol(k, i, &fg, &bg, &ac);
			if (bg & 0xff) {
				bar_tile(k, px[i], k->tl[i], k->cs + bartaginset,
						k->ct - 2 * bartaginset, &tx, &ty, &tw, &th);
				bar_rect(img, tx, ty, tw, th, bg);
			}
		}
	}

	for (i = 0; i < TAGCOUNT; i++) {
		uint32_t bit = 1u << i;
		const char *txt = bartags[i] ? bartags[i] : "";
		int active;

		if (!k->tl[i])
			continue;
		active = ((k->urg | k->tagset) & bit) != 0;
		bar_tagcol(k, i, &fg, &bg, &ac);
		bar_tile(k, px[i], k->tl[i], k->cs + bartaginset, k->ct - 2 * bartaginset,
				&tx, &ty, &tw, &th);
		bar_text_center(img, tx, ty, tw, th, txt, fg);

		if (bartagdeco != BarDecoNone && (active || bartagdecoall)) {
			int ln = bar_imin(bartagline, bar_imin(tw, th) / 2);
			switch (bartagdeco) {
			case BarDecoInner:
				bar_edge(img, k->up, tx, ty, tw, th, !es, ln, ac);
				break;
			case BarDecoOuter:
				bar_edge(img, k->up, tx, ty, tw, th, es, ln, ac);
				break;
			case BarDecoBoth:
				bar_edge(img, k->up, tx, ty, tw, th, 0, ln, ac);
				bar_edge(img, k->up, tx, ty, tw, th, 1, ln, ac);
				break;
			case BarDecoBorder:
				bar_rect(img, tx, ty, tw, ln, ac);
				bar_rect(img, tx, ty + th - ln, tw, ln, ac);
				bar_rect(img, tx, ty + ln, ln, th - 2 * ln, ac);
				bar_rect(img, tx + tw - ln, ty + ln, ln, th - 2 * ln, ac);
				break;
			}
		}

		if ((k->occ & bit) && baroccstyle != BarOccNone) {
			int sz = baroccsize > 0 ? baroccsize : bar_font->height / 6 + 1;
			int inner_start = !es;
			switch (baroccstyle) {
			case BarOccDwm: {
				int boxs = bar_font->height / 9;
				int bx = tx + boxs, by = ty + boxs;
				if (m == selmon && (k->selmask & bit))
					bar_rect(img, bx, by, sz, sz, ac);
				else {
					bar_rect(img, bx, by, sz, 1, ac);
					bar_rect(img, bx, by + sz - 1, sz, 1, ac);
					bar_rect(img, bx, by, 1, sz, ac);
					bar_rect(img, bx + sz - 1, by, 1, sz, ac);
				}
				break;
			}
			case BarOccDot: {
				int dx, dy;
				if (!k->up) {
					dx = tx + (tw - sz) / 2;
					dy = inner_start ? ty + 1 : ty + th - sz - 1;
				} else {
					dx = inner_start ? tx + 1 : tx + tw - sz - 1;
					dy = ty + (th - sz) / 2;
				}
				bar_rrect(img, dx, dy, sz, sz, sz / 2, ac);
				break;
			}
			case BarOccLine:
				if (!k->up)
					bar_rect(img, tx + tw / 4, inner_start ? ty : ty + th - sz,
							tw / 2, sz, ac);
				else
					bar_rect(img, inner_start ? tx : tx + tw - sz, ty + th / 4,
							sz, th / 2, ac);
				break;
			}
		}

		m->bar_tagx[i] = px[i];
		m->bar_tagw[i] = k->tl[i];
	}
}

static void
bar_draw_text(BarCtx *k, int start, int len, const char *txt, int sch)
{
	uint32_t fg = barcolors[sch][0], bg = barcolors[sch][1];
	int tx, ty, tw, th;

	bar_tile(k, start, len, k->cs, k->ct, &tx, &ty, &tw, &th);
	if (bg & 0xff)
		bar_rect(k->img, tx, ty, tw, th, bg);
	if (!k->up)
		bar_status_run(k->img, tx + bar_lrpad / 2, ty, th, txt, fg, bg, -1);
	else {
		char line[512];
		const char *p = txt, *nl;
		int fh = bar_font->height, row = ty + bar_lrpad / 2, sw, lx;
		size_t n;

		for (;;) {
			nl = strchr(p, '\n');
			n = nl ? (size_t)(nl - p) : strlen(p);
			if (n > sizeof(line) - 1)
				n = sizeof(line) - 1;
			memcpy(line, p, n);
			line[n] = '\0';
			sw = bar_status_run(NULL, 0, 0, 0, line, fg, bg, -1);
			lx = bar_imax(tx, tx + (tw - sw) / 2);
			bar_status_run(k->img, lx, row, fh, line, fg, bg, tx + tw);
			row += fh;
			if (!nl)
				break;
			p = nl + 1;
		}
	}
}

static void
bar_draw(Monitor *m)
{
	BarBuffer *bb;
	BarCtx k;
	BarSlot slots[BAR_MAXSLOTS];
	pixman_image_t *img, *strip = NULL;
	Client *c;
	int horiz, up, rot, L, T, Sw, Sh, b, ms, me, i, j, n;
	int ns = 0, nflex = 0, fixed = 0, flexspace, pos, nvis = 0, fi = 0;
	int showmods, showstatus, nl = 1, fh, sepw;

	if (!bar_font || !m->bar_scene || !m->bar_vis || m->bar_area.width <= 0)
		return;
	horiz = bar_horiz(m);
	up = bar_up(m);
	rot = bar_rot(m);
	b = barborderpx;
	fh = bar_font->height;
	L = horiz ? m->bar_area.width : m->bar_area.height;
	T = horiz ? m->bar_area.height : m->bar_area.width;
	Sw = up ? T : L;
	Sh = up ? L : T;
	if (rot && !(strip = bar_scratch(m, L, T)))
		return;
	if (!(bb = bar_getbuf(m, m->bar_area.width, m->bar_area.height)))
		return;
	img = rot ? strip : bb->image;

	memset(&k, 0, sizeof(k));
	k.m = m;
	k.img = img;
	k.up = up;
	k.edge_start = bar_edge_start(m);
	k.cs = b;
	k.ct = T - 2 * b;
	wl_list_for_each(c, &clients, link) {
		if (c->mon != m)
			continue;
		k.occ |= c->tags;
		if (c->isurgent)
			k.urg |= c->tags;
	}
	k.sel = focustop(m);
	if (k.sel)
		k.selmask = k.sel->tags;
	k.tagset = m->tagset[m->seltags];

	memset(m->bar_tagx, 0, sizeof(m->bar_tagx));
	memset(m->bar_tagw, 0, sizeof(m->bar_tagw));
	m->bar_statusx = m->bar_statusw = 0;

	if (b > 0)
		bar_rect(img, 0, 0, Sw, Sh, barcolors[SchemeBorder][1]);
	bar_rect(img, b, b, Sw - 2 * b, Sh - 2 * b, barcolors[SchemeBar][1]);

	for (i = 0; i < TAGCOUNT; i++) {
		uint32_t bit = 1u << i;
		k.tl[i] = 0;
		if (bartaghideempty && !((k.occ | k.tagset | k.urg) & bit))
			continue;
		k.tl[i] = bar_taglen(m, i);
		nvis++;
	}

	showmods = (m == selmon || barstatusallmons);
	showstatus = bar_status[0] && showmods;
	if (showstatus && up)
		nl = bar_lines(bar_status);

	n = (int)LENGTH(barorder);
	if (n > BAR_MAXSLOTS)
		n = BAR_MAXSLOTS;
	for (j = 0; j < n; j++) {
		int type = barorder[j], len = 0, flex = 0;

		switch (type) {
		case BarModTags:
			if (!nvis)
				continue;
			for (i = 0; i < TAGCOUNT; i++)
				if (k.tl[i])
					len += k.tl[i] + bartaggap;
			len -= bartaggap;
			break;
		case BarModStatus:
			if (!showstatus)
				continue;
			len = (up ? nl * fh
					: bar_status_run(NULL, 0, 0, 0, bar_status, 0, 0, -1)) + bar_lrpad;
			break;
		case BarModSpacer:
			flex = 1;
			break;
		default:
			if (type < BarModVolume || type >= BarModCount
					|| !showmods || !bar_modtxt[type][0])
				continue;
			len = (up ? bar_lines(bar_modtxt[type]) * fh
					: bar_status_run(NULL, 0, 0, 0, bar_modtxt[type], 0, 0, -1)) + bar_lrpad;
			break;
		}
		slots[ns].type = type;
		slots[ns].len = len;
		slots[ns].flex = flex;
		ns++;
		if (flex)
			nflex++;
		else
			fixed += len;
	}
	sepw = (barseptext && barseptext[0])
		? (up ? fh : bar_status_run(NULL, 0, 0, 0, barseptext, 0, 0, -1)) : 0;
	for (j = 0; j < ns; j++) {
		slots[j].gap = 0;
		slots[j].sep = 0;
		if (j + 1 >= ns)
			continue;
		slots[j].gap = barmodulegap;
		if (sepw > 0 && !slots[j].flex && !slots[j + 1].flex) {
			slots[j].sep = 1;
			slots[j].gap += sepw;
		}
		fixed += slots[j].gap;
	}

	ms = b + barpad;
	me = (up ? Sh : Sw) - b - barpad;
	flexspace = bar_imax(0, (me - ms) - fixed);
	pos = ms;
	if (!nflex && flexspace > 0)
		pos += barcontentalign == BarAlignCenter ? flexspace / 2
			: barcontentalign == BarAlignEnd ? flexspace : 0;
	for (j = 0; j < ns; j++) {
		if (slots[j].flex) {
			slots[j].len = flexspace / nflex;
			if (++fi == nflex)
				slots[j].len += flexspace % nflex;
		}
		slots[j].start = pos;
		pos += slots[j].len + slots[j].gap;
	}

	for (j = 0; j + 1 < ns; j++) {
		int tx, ty, tw, th, sw;

		if (!slots[j].sep)
			continue;
		bar_tile(&k, slots[j].start + slots[j].len + barmodulegap / 2, sepw,
				k.cs, k.ct, &tx, &ty, &tw, &th);
		sw = bar_status_run(NULL, 0, 0, 0, barseptext, 0, 0, -1);
		bar_status_run(img, tx + bar_imax(0, (tw - sw) / 2), ty, th, barseptext,
				barcolors[SchemeSep][0], 0, up ? tx + tw : -1);
	}

	if (barsepwidth > 0 && barmodulegap > 0) {
		int sl = k.ct * bar_imax(1, bar_imin(barseppct, 100)) / 100;
		int cpos = k.cs + (k.ct - sl) / 2;
		for (j = 0; j + 1 < ns; j++) {
			int spos = slots[j].start + slots[j].len + (slots[j].gap - barsepwidth) / 2;
			if (!up)
				bar_rect(img, spos, cpos, barsepwidth, sl, barcolors[SchemeSep][1]);
			else
				bar_rect(img, cpos, spos, sl, barsepwidth, barcolors[SchemeSep][1]);
		}
	}

	for (j = 0; j < ns; j++) {
		switch (slots[j].type) {
		case BarModTags:
			bar_draw_tags(&k, slots[j].start);
			break;
		case BarModSpacer:
			break;
		case BarModStatus:
			bar_draw_text(&k, slots[j].start, slots[j].len, bar_status, SchemeStatus);
			m->bar_statusx = slots[j].start;
			m->bar_statusw = slots[j].len;
			break;
		default:
			bar_draw_text(&k, slots[j].start, slots[j].len,
					bar_modtxt[slots[j].type], bar_modsch[slots[j].type]);
			break;
		}
	}

	if (rot)
		bar_rotate(strip, bb->image, L, T, barvertmode == BarRotCW);

	wlr_scene_buffer_set_buffer(m->bar_scene, &bb->base);
	if (!bb->pooled)
		wlr_buffer_drop(&bb->base);

	if (m->bar_anim_on)
		wlr_output_schedule_frame(m->wlr_output);
}

static void
bar_draw_all(void)
{
	Monitor *m;
	wl_list_for_each(m, &mons, link)
		bar_draw(m);
}

static void
bar_anim_step(Monitor *m)
{
	if (m->bar_anim_on && m->bar_vis)
		bar_draw(m);
}

static void
bar_hanim_apply(Monitor *m)
{
	double p = m->bar_prog, q = 1.0 - p;
	int x = m->bar_area.x, y = m->bar_area.y;
	float a = 1.0f;

	if (!m->bar_scene)
		return;
	if (barhidestyle != BarHideFade) {
		switch (m->bar_pos) {
		case BarBottom:
			y += (int)llround(q * (m->bar_area.height + barmargin[2]));
			break;
		case BarLeft:
			x -= (int)llround(q * (m->bar_area.width + barmargin[3]));
			break;
		case BarRight:
			x += (int)llround(q * (m->bar_area.width + barmargin[1]));
			break;
		default:
			y -= (int)llround(q * (m->bar_area.height + barmargin[0]));
			break;
		}
	}
	if (barhidestyle != BarHideSlide)
		a = p < 0.0 ? 0.0f : p > 1.0 ? 1.0f : (float)p;
	wlr_scene_node_set_position(&m->bar_scene->node, x, y);
	wlr_scene_buffer_set_opacity(m->bar_scene, a);
}

static void
bar_hanim_start(Monitor *m)
{
	double dur = m->showbar ? barshowdur : barhidedur;

	m->bar_hanim_to = m->showbar;
	if (!barhideanim || dur <= 0.0 || !bar_font || !m->bar_scene) {
		m->bar_hanim_on = 0;
		m->bar_prog = m->showbar ? 1.0 : 0.0;
		return;
	}
	m->bar_hanim_from = m->bar_prog;
	m->bar_hanim_ms = bar_now_ms();
	m->bar_hanim_on = 1;
	wlr_output_schedule_frame(m->wlr_output);
}

static void
bar_hanim_step(Monitor *m)
{
	double t, e, to, dur;

	if (!m->bar_hanim_on)
		return;
	to = m->bar_hanim_to ? 1.0 : 0.0;
	dur = m->bar_hanim_to ? barshowdur : barhidedur;
	t = (double)(bar_now_ms() - m->bar_hanim_ms) / (dur * 1000.0);
	if (t >= 1.0) {
		m->bar_hanim_on = 0;
		m->bar_prog = to;
	} else {
		e = bezier_ease(m->bar_hanim_to ? barshowbezier : barhidebezier,
				t < 0.0 ? 0.0 : t);
		m->bar_prog = m->bar_hanim_from + (to - m->bar_hanim_from) * e;
	}
	bar_hanim_apply(m);
	if (m->bar_hanim_on) {
		wlr_output_schedule_frame(m->wlr_output);
	} else if (!m->bar_vis && m->bar_scene) {
		wlr_scene_node_set_enabled(&m->bar_scene->node, 0);
		m->bar_area = (struct wlr_box){0};
	}
}

static void
bar_mon_create(Monitor *m)
{
	m->showbar = showbar;
	m->bar_prog = showbar ? 1.0 : 0.0;
	m->bar_pos = barpos;
	m->bar_float = !barreserve;
	m->bar_above = barabovewindows;
	m->bar_scene = wlr_scene_buffer_create(layers[m->bar_above ? LyrTop : LyrBottom], NULL);
	wlr_scene_node_set_enabled(&m->bar_scene->node, 0);
}

static void
bar_mon_destroy(Monitor *m)
{
	if (m->bar_scene)
		wlr_scene_node_destroy(&m->bar_scene->node);
	m->bar_scene = NULL;
	m->bar_vis = 0;
	m->bar_anim_on = 0;
	m->bar_hanim_on = 0;
	bar_pool_reset(m);
	if (bar_hover == m)
		bar_hover = NULL;
}

static void
bar_mon_layout(Monitor *m)
{
	int horiz = bar_horiz(m), span = 0, L, T, off, x, y, w, h, ok;

	if (!m->bar_scene)
		return;
	ok = bar_font && m->wlr_output->enabled && m->m.width > 0;
	if (ok) {
		span = horiz ? m->m.width - barmargin[1] - barmargin[3]
				: m->m.height - barmargin[0] - barmargin[2];
		if (span <= 0)
			ok = 0;
	}
	m->bar_vis = ok && m->showbar;
	if (!ok || (!m->bar_vis && !m->bar_hanim_on)) {
		m->bar_hanim_on = 0;
		m->bar_prog = m->showbar ? 1.0 : 0.0;
		wlr_scene_node_set_enabled(&m->bar_scene->node, 0);
		m->bar_area = (struct wlr_box){0};
		return;
	}
	if (!m->bar_hanim_on)
		m->bar_prog = 1.0;
	wlr_scene_node_set_enabled(&m->bar_scene->node, 1);

	T = bar_thick(m);
	L = barlength > 0 ? barlength : barlengthpct > 0 ? span * barlengthpct / 100 : span;
	L = bar_imax(1, bar_imin(L, span));
	off = baralign == BarAlignCenter ? (span - L) / 2 : baralign == BarAlignEnd ? span - L : 0;

	switch (m->bar_pos) {
	case BarBottom:
		x = m->m.x + barmargin[3] + off;
		y = m->m.y + m->m.height - barmargin[2] - T;
		w = L; h = T;
		break;
	case BarLeft:
		x = m->m.x + barmargin[3];
		y = m->m.y + barmargin[0] + off;
		w = T; h = L;
		break;
	case BarRight:
		x = m->m.x + m->m.width - barmargin[1] - T;
		y = m->m.y + barmargin[0] + off;
		w = T; h = L;
		break;
	default:
		x = m->m.x + barmargin[3] + off;
		y = m->m.y + barmargin[0];
		w = L; h = T;
		break;
	}
	m->bar_area.x = x;
	m->bar_area.y = y;
	m->bar_area.width = w;
	m->bar_area.height = h;
	bar_hanim_apply(m);
	if (m->bar_vis)
		bar_draw(m);
}

static void
bar_reserve(Monitor *m, struct wlr_box *usable)
{
	int r;

	if (!m->bar_vis || m->bar_float)
		return;
	switch (m->bar_pos) {
	case BarBottom:
		r = barmargin[2] + m->bar_area.height + bargap;
		usable->height -= r;
		break;
	case BarLeft:
		r = barmargin[3] + m->bar_area.width + bargap;
		usable->x += r;
		usable->width -= r;
		break;
	case BarRight:
		r = barmargin[1] + m->bar_area.width + bargap;
		usable->width -= r;
		break;
	default:
		r = barmargin[0] + m->bar_area.height + bargap;
		usable->y += r;
		usable->height -= r;
		break;
	}
}

static Monitor *
bar_from_node(struct wlr_scene_node *node)
{
	Monitor *m;
	wl_list_for_each(m, &mons, link)
		if (m->bar_scene && &m->bar_scene->node == node)
			return m;
	return NULL;
}

static void
bar_click(Monitor *m, double cx, double cy, unsigned int button, uint32_t mods)
{
	const Button *b;
	unsigned int click = ClkTitle;
	Arg tagarg = {0};
	int rx = (int)(cx - m->bar_area.x), ry = (int)(cy - m->bar_area.y), u, i;

	if (bar_horiz(m))
		u = rx;
	else if (bar_up(m) || barvertmode == BarRotCW)
		u = ry;
	else
		u = m->bar_area.height - 1 - ry;

	for (i = 0; i < TAGCOUNT; i++) {
		if (m->bar_tagw[i] > 0 && u >= m->bar_tagx[i]
				&& u < m->bar_tagx[i] + m->bar_tagw[i]) {
			click = ClkTagBar;
			tagarg.ui = 1u << i;
			break;
		}
	}
	if (click == ClkTitle && m->bar_statusw > 0 && u >= m->bar_statusx
			&& u < m->bar_statusx + m->bar_statusw)
		click = ClkStatus;

	for (b = buttons; b < END(buttons); b++) {
		if (b->click == click && b->button == button && b->func
				&& CLEANMASK(mods) == CLEANMASK(b->mod)) {
			b->func((click == ClkTagBar && !b->arg.ui) ? &tagarg : &b->arg);
			return;
		}
	}
}

static void
bar_relayout(Monitor *m)
{
	bar_mon_layout(m);
	arrangelayers(m);
}

void
togglebar(const Arg *arg)
{
	if (!selmon)
		return;
	selmon->showbar = !selmon->showbar;
	bar_hanim_start(selmon);
	bar_relayout(selmon);
}

void
barsetpos(const Arg *arg)
{
	if (!selmon || arg->ui > BarRight)
		return;
	selmon->bar_pos = (int)arg->ui;
	bar_relayout(selmon);
}

void
barcyclepos(const Arg *arg)
{
	static const int order[] = { BarTop, BarRight, BarBottom, BarLeft };
	int i;

	if (!selmon)
		return;
	for (i = 0; i < 4 && order[i] != selmon->bar_pos; i++)
		;
	selmon->bar_pos = order[(i + (arg->i < 0 ? 3 : 1)) % 4];
	bar_relayout(selmon);
}

void
togglebarfloat(const Arg *arg)
{
	if (!selmon)
		return;
	selmon->bar_float = !selmon->bar_float;
	bar_relayout(selmon);
}

void
togglebarlayer(const Arg *arg)
{
	if (!selmon || !selmon->bar_scene)
		return;
	selmon->bar_above = !selmon->bar_above;
	wlr_scene_node_reparent(&selmon->bar_scene->node,
			layers[selmon->bar_above ? LyrTop : LyrBottom]);
}

void
barspawn(const Arg *arg)
{
	spawn(arg);
	if (bar_kick_src)
		wl_event_source_timer_update(bar_kick_src, barkickms);
}

static ssize_t
bar_slurp(const char *path, char *buf, size_t n)
{
	int fd = open(path, O_RDONLY | O_CLOEXEC);
	ssize_t r;

	if (fd < 0)
		return -1;
	r = read(fd, buf, n - 1);
	close(fd);
	if (r < 0)
		return -1;
	buf[r] = '\0';
	return r;
}

static void
bar_chomp(char *s)
{
	size_t n = strlen(s);

	while (n && (s[n - 1] == '\n' || s[n - 1] == '\r' || s[n - 1] == ' '))
		s[--n] = '\0';
}

static void
bar_format(char *out, size_t n, const char *fmt, const char *icon, int val,
		const char *s, double used, double total)
{
	size_t o = 0;
	const char *p, *ins;
	char tmp[32];

	for (p = fmt; *p && o + 1 < n; p++) {
		if (*p != '%' || !p[1]) {
			out[o++] = *p;
			continue;
		}
		p++;
		switch (*p) {
		case 'i': ins = icon ? icon : ""; break;
		case 'v': snprintf(tmp, sizeof(tmp), "%d", val); ins = tmp; break;
		case 's': ins = s ? s : ""; break;
		case 'u': snprintf(tmp, sizeof(tmp), "%.1f", used); ins = tmp; break;
		case 't': snprintf(tmp, sizeof(tmp), "%.1f", total); ins = tmp; break;
		case '%': ins = "%"; break;
		default:
			out[o++] = '%';
			if (o + 1 < n)
				out[o++] = *p;
			continue;
		}
		while (*ins && o + 1 < n)
			out[o++] = *ins++;
	}
	out[o] = '\0';
}

static int
bar_setmod(int mod, int sch, const char *txt)
{
	if (bar_modsch[mod] == sch && !strcmp(bar_modtxt[mod], txt))
		return 0;
	bar_modsch[mod] = sch;
	snprintf(bar_modtxt[mod], BAR_TXT, "%s", txt);
	return 1;
}

static int
bar_upd_cpu(void)
{
	static unsigned long long pb, pt;
	static int have;
	char buf[1024], t[BAR_TXT];
	unsigned long long v[8] = {0}, tot, idle, busy;
	int pct = 0;

	if (bar_slurp("/proc/stat", buf, sizeof(buf)) <= 0
			|| sscanf(buf, "cpu %llu %llu %llu %llu %llu %llu %llu %llu",
				&v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6], &v[7]) < 4)
		return bar_setmod(BarModCpu, SchemeCpu, "");
	idle = v[3] + v[4];
	tot = v[0] + v[1] + v[2] + v[3] + v[4] + v[5] + v[6] + v[7];
	busy = tot - idle;
	if (have && tot > pt && busy >= pb)
		pct = (int)(100 * (busy - pb) / (tot - pt));
	pb = busy;
	pt = tot;
	have = 1;
	if (pct > 100)
		pct = 100;
	bar_format(t, sizeof(t), barfmt_cpu, baricon_cpu, pct, NULL, 0, 0);
	return bar_setmod(BarModCpu, pct >= barcpuwarn ? SchemeCpuWarn : SchemeCpu, t);
}

static int
bar_upd_ram(void)
{
	char buf[4096], t[BAR_TXT];
	unsigned long long total = 0, avail = 0, used;
	const char *p;
	int pct;

	if (bar_slurp("/proc/meminfo", buf, sizeof(buf)) <= 0)
		return bar_setmod(BarModRam, SchemeRam, "");
	if ((p = strstr(buf, "MemTotal:")))
		total = strtoull(p + 9, NULL, 10);
	if ((p = strstr(buf, "MemAvailable:")))
		avail = strtoull(p + 13, NULL, 10);
	if (!total)
		return bar_setmod(BarModRam, SchemeRam, "");
	used = total - (avail < total ? avail : total);
	pct = (int)(used * 100 / total);
	bar_format(t, sizeof(t), barfmt_ram, baricon_ram, pct, NULL,
			(double)used / 1048576.0, (double)total / 1048576.0);
	return bar_setmod(BarModRam, pct >= barramwarn ? SchemeRamWarn : SchemeRam, t);
}

static int
bar_upd_temp(void)
{
	char buf[32], t[BAR_TXT];
	int v;

	if (!bar_temp_ok || bar_slurp(bar_temppath, buf, sizeof(buf)) <= 0)
		return bar_setmod(BarModTemp, SchemeTemp, "");
	v = (int)((atol(buf) + 500) / 1000);
	bar_format(t, sizeof(t), barfmt_temp, baricon_temp, v, NULL, 0, 0);
	return bar_setmod(BarModTemp, v >= bartempwarn ? SchemeTempWarn : SchemeTemp, t);
}

static int
bar_upd_bat(void)
{
	char cap[32], st[32], t[BAR_TXT];
	int pct, chg, sch;
	const char *icon;

	if (!bar_bat_ok || bar_slurp(bar_batcap, cap, sizeof(cap)) <= 0)
		return bar_setmod(BarModBattery, SchemeBattery, "");
	if (bar_slurp(bar_batstat, st, sizeof(st)) <= 0)
		st[0] = '\0';
	bar_chomp(st);
	pct = atoi(cap);
	if (pct < 0)
		pct = 0;
	if (pct > 100)
		pct = 100;
	chg = !strcmp(st, "Charging");
	icon = chg ? baricons_bat[0] : baricons_bat[1 + bar_imin(pct / 20, 4)];
	sch = chg ? SchemeBatChg : pct <= barbatwarn ? SchemeBatLow : SchemeBattery;
	bar_format(t, sizeof(t), barfmt_bat, icon, pct, st, 0, 0);
	return bar_setmod(BarModBattery, sch, t);
}

static int
bar_upd_bri(void)
{
	char buf[32], t[BAR_TXT];
	long cur;
	int pct;

	if (!bar_bl_ok || bar_slurp(bar_blcur, buf, sizeof(buf)) <= 0)
		return bar_setmod(BarModBrightness, SchemeBrightness, "");
	cur = atol(buf);
	pct = (int)((cur * 100 + bar_blmax / 2) / bar_blmax);
	if (pct < 0)
		pct = 0;
	if (pct > 100)
		pct = 100;
	bar_format(t, sizeof(t), barfmt_bri, baricon_bri, pct, NULL, 0, 0);
	return bar_setmod(BarModBrightness, SchemeBrightness, t);
}

static int
bar_upd_vol(void)
{
	char t[BAR_TXT];
	const char *p = strstr(bar_vol_buf, "Volume:");
	int pct, muted;
	const char *icon;

	if (!p)
		return bar_setmod(BarModVolume, SchemeVolume, "");
	pct = (int)(strtod(p + 7, NULL) * 100.0 + 0.5);
	muted = strstr(bar_vol_buf, "MUTED") != NULL;
	icon = muted ? baricons_vol[0] : pct < 50 ? baricons_vol[1] : baricons_vol[2];
	bar_format(t, sizeof(t), barfmt_vol, icon, pct, muted ? "muted" : "", 0, 0);
	return bar_setmod(BarModVolume, muted ? SchemeMuted : SchemeVolume, t);
}

static void bar_vol_start(void);

static int
bar_vol_cb(int fd, uint32_t mask, void *data)
{
	ssize_t n = read(fd, bar_vol_buf + bar_vol_len, sizeof(bar_vol_buf) - bar_vol_len - 1);

	if (n < 0 && (errno == EAGAIN || errno == EINTR))
		return 0;
	if (n > 0)
		bar_vol_len += (size_t)n;
	if (n > 0 && bar_vol_len < sizeof(bar_vol_buf) - 1)
		return 0;
	bar_vol_buf[bar_vol_len] = '\0';
	wl_event_source_remove(bar_vol_src);
	bar_vol_src = NULL;
	close(fd);
	bar_vol_fd = -1;
	if (bar_upd_vol())
		bar_draw_all();
	if (bar_vol_again) {
		bar_vol_again = 0;
		bar_vol_start();
	}
	return 0;
}

static void
bar_vol_start(void)
{
	int fds[2];
	pid_t pid;

	if (!barvolcmd[0])
		return;
	if (bar_vol_fd >= 0) {
		bar_vol_again = 1;
		return;
	}
	if (pipe(fds) < 0)
		return;
	fcntl(fds[0], F_SETFD, FD_CLOEXEC);
	fcntl(fds[1], F_SETFD, FD_CLOEXEC);
	if ((pid = fork()) < 0) {
		close(fds[0]);
		close(fds[1]);
		return;
	}
	if (pid == 0) {
		dup2(fds[1], STDOUT_FILENO);
		execvp(barvolcmd[0], (char *const *)barvolcmd);
		_exit(127);
	}
	close(fds[1]);
	fd_set_nonblock(fds[0]);
	bar_vol_len = 0;
	bar_vol_src = wl_event_loop_add_fd(event_loop, fds[0], WL_EVENT_READABLE,
			bar_vol_cb, NULL);
	if (!bar_vol_src) {
		close(fds[0]);
		return;
	}
	bar_vol_fd = fds[0];
}

static void
bar_layout_update(struct wlr_keyboard *kb)
{
	xkb_layout_index_t idx, nl;
	const char *name;
	char t[BAR_TXT];

	if (!bar_modused[BarModLayout] || !kb || !kb->keymap || !kb->xkb_state)
		return;
	nl = xkb_keymap_num_layouts(kb->keymap);
	for (idx = 0; idx < nl; idx++)
		if (xkb_state_layout_index_is_active(kb->xkb_state, idx, XKB_STATE_LAYOUT_EFFECTIVE))
			break;
	if (idx >= nl)
		idx = 0;
	if (idx < LENGTH(barlayoutnames) && barlayoutnames[idx])
		name = barlayoutnames[idx];
	else
		name = xkb_keymap_layout_get_name(kb->keymap, idx);
	bar_format(t, sizeof(t), barfmt_layout, baricon_layout, (int)idx, name, 0, 0);
	if (bar_setmod(BarModLayout, SchemeLayout, t))
		bar_draw_all();
}

static int
bar_sys_update(void)
{
	int d = 0;

	if (bar_modused[BarModCpu])        d |= bar_upd_cpu();
	if (bar_modused[BarModRam])        d |= bar_upd_ram();
	if (bar_modused[BarModTemp])       d |= bar_upd_temp();
	if (bar_modused[BarModBattery])    d |= bar_upd_bat();
	if (bar_modused[BarModBrightness]) d |= bar_upd_bri();
	return d;
}

static int
bar_sys_tick(void *data)
{
	static int volacc;
	int iv = barsysinterval > 0 ? barsysinterval : 2000;

	if (bar_sys_update())
		bar_draw_all();
	if (bar_modused[BarModVolume] && (volacc += iv) >= barvolinterval) {
		volacc = 0;
		bar_vol_start();
	}
	wl_event_source_timer_update(bar_sys_src, iv);
	return 0;
}

static int
bar_kick_tick(void *data)
{
	if (bar_modused[BarModBrightness] && bar_upd_bri())
		bar_draw_all();
	if (bar_modused[BarModVolume])
		bar_vol_start();
	return 0;
}

static void
bar_bat_find(void)
{
	DIR *d = opendir("/sys/class/power_supply");
	struct dirent *e;

	if (!d)
		return;
	while ((e = readdir(d))) {
		if (barbatname && barbatname[0] ? strcmp(e->d_name, barbatname)
				: strncmp(e->d_name, "BAT", 3))
			continue;
		snprintf(bar_batcap, sizeof(bar_batcap), "/sys/class/power_supply/%s/capacity", e->d_name);
		snprintf(bar_batstat, sizeof(bar_batstat), "/sys/class/power_supply/%s/status", e->d_name);
		if (access(bar_batcap, R_OK) == 0) {
			bar_bat_ok = 1;
			break;
		}
	}
	closedir(d);
}

static void
bar_bl_find(void)
{
	DIR *d = opendir("/sys/class/backlight");
	struct dirent *e;
	char path[320], buf[32];

	if (!d)
		return;
	while ((e = readdir(d))) {
		if (e->d_name[0] == '.')
			continue;
		if (barblname && barblname[0] && strcmp(e->d_name, barblname))
			continue;
		snprintf(path, sizeof(path), "/sys/class/backlight/%s/max_brightness", e->d_name);
		if (bar_slurp(path, buf, sizeof(buf)) <= 0 || atol(buf) <= 0)
			continue;
		bar_blmax = atol(buf);
		snprintf(bar_blcur, sizeof(bar_blcur), "/sys/class/backlight/%s/brightness", e->d_name);
		bar_bl_ok = 1;
		break;
	}
	closedir(d);
}

static void
bar_temp_find(void)
{
	static const char *names[] = { "coretemp", "k10temp", "zenpower", "cpu_thermal", NULL };
	DIR *d;
	struct dirent *e;
	char path[320], buf[64];
	int i;

	if (bartemppath && bartemppath[0]) {
		snprintf(bar_temppath, sizeof(bar_temppath), "%s", bartemppath);
		bar_temp_ok = access(bar_temppath, R_OK) == 0;
		return;
	}
	if ((d = opendir("/sys/class/hwmon"))) {
		while (!bar_temp_ok && (e = readdir(d))) {
			if (strncmp(e->d_name, "hwmon", 5))
				continue;
			snprintf(path, sizeof(path), "/sys/class/hwmon/%s/name", e->d_name);
			if (bar_slurp(path, buf, sizeof(buf)) <= 0)
				continue;
			bar_chomp(buf);
			for (i = 0; names[i]; i++) {
				if (strcmp(buf, names[i]))
					continue;
				snprintf(bar_temppath, sizeof(bar_temppath),
						"/sys/class/hwmon/%s/temp1_input", e->d_name);
				if (access(bar_temppath, R_OK) == 0) {
					bar_temp_ok = 1;
					break;
				}
			}
		}
		closedir(d);
	}
	if (!bar_temp_ok) {
		snprintf(bar_temppath, sizeof(bar_temppath), "/sys/class/thermal/thermal_zone0/temp");
		bar_temp_ok = access(bar_temppath, R_OK) == 0;
	}
}

static int
bar_stdin_cb(int fd, uint32_t mask, void *data)
{
	static char buf[4096];
	static size_t len;
	ssize_t n;
	char *start, *nl, *last = NULL;

	n = read(fd, buf + len, sizeof(buf) - len - 1);
	if (n < 0 && (errno == EAGAIN || errno == EINTR))
		return 0;
	if (n <= 0) {
		wl_event_source_remove(bar_stdin_src);
		bar_stdin_src = NULL;
		return 0;
	}
	len += (size_t)n;
	buf[len] = '\0';

	start = buf;
	while ((nl = memchr(start, '\n', (size_t)(buf + len - start)))) {
		*nl = '\0';
		last = start;
		start = nl + 1;
	}
	if (last) {
		snprintf(bar_status, sizeof(bar_status), "%s", last);
		bar_status_stdin = 1;
		bar_draw_all();
	}
	len = (size_t)(buf + len - start);
	memmove(buf, start, len);
	if (len >= sizeof(buf) - 1)
		len = 0;
	return 0;
}

static int
bar_clock_tick(void *data)
{
	char buf[sizeof(bar_status)];
	time_t t = time(NULL);
	struct tm tm;
	int ms;

	if (bar_status_stdin || !bar_clock_src)
		return 0;
	localtime_r(&t, &tm);
	if (!strftime(buf, sizeof(buf), barclockfmt, &tm))
		buf[0] = '\0';
	if (strcmp(buf, bar_status)) {
		memcpy(bar_status, buf, sizeof(bar_status));
		bar_draw_all();
	}
	ms = (strstr(barclockfmt, "%S") || strstr(barclockfmt, "%T"))
		? 1000 : (60 - tm.tm_sec) * 1000 + 20;
	wl_event_source_timer_update(bar_clock_src, ms);
	return 0;
}

static void
bar_init(void)
{
	int i;

	if (!fcft_init(FCFT_LOG_COLORIZE_NEVER, false, FCFT_LOG_CLASS_ERROR)) {
		fprintf(stderr, "lilith: bar: fcft_init failed, bar disabled\n");
		return;
	}
	bar_font = fcft_from_name(LENGTH(barfonts), barfonts, NULL);
	if (!bar_font) {
		fprintf(stderr, "lilith: bar: can't load font '%s', bar disabled\n", barfonts[0]);
		fcft_fini();
		return;
	}
	bar_lrpad = barlrpad > 0 ? barlrpad : bar_font->height;
	for (i = 0; i < TAGCOUNT; i++) {
		const char *txt = bartags[i] ? bartags[i] : "";
		bar_maxtagw = bar_imax(bar_maxtagw, bar_textw(txt, strlen(txt)));
	}

	if (barstatusstdin && !isatty(STDIN_FILENO)) {
		fd_set_nonblock(STDIN_FILENO);
		bar_stdin_src = wl_event_loop_add_fd(event_loop, STDIN_FILENO,
				WL_EVENT_READABLE, bar_stdin_cb, NULL);
	}
	if (barclockfmt && barclockfmt[0]) {
		bar_clock_src = wl_event_loop_add_timer(event_loop, bar_clock_tick, NULL);
		bar_clock_tick(NULL);
	}

	for (i = 0; i < (int)LENGTH(barorder); i++)
		if (barorder[i] >= 0 && barorder[i] < BarModCount)
			bar_modused[barorder[i]] = 1;
	if (bar_modused[BarModBattery])
		bar_bat_find();
	if (bar_modused[BarModBrightness])
		bar_bl_find();
	if (bar_modused[BarModTemp])
		bar_temp_find();
	bar_sys_update();
	if (bar_modused[BarModVolume])
		bar_vol_start();
	if (bar_modused[BarModCpu] || bar_modused[BarModRam] || bar_modused[BarModTemp]
			|| bar_modused[BarModBattery] || bar_modused[BarModBrightness]
			|| bar_modused[BarModVolume]) {
		bar_sys_src = wl_event_loop_add_timer(event_loop, bar_sys_tick, NULL);
		wl_event_source_timer_update(bar_sys_src, barsysinterval > 0 ? barsysinterval : 2000);
	}
	if (bar_modused[BarModBrightness] || bar_modused[BarModVolume])
		bar_kick_src = wl_event_loop_add_timer(event_loop, bar_kick_tick, NULL);
	if (kb_group)
		bar_layout_update(&kb_group->wlr_group->keyboard);
}

static void
bar_cleanup(void)
{
	int i;

	if (bar_stdin_src)
		wl_event_source_remove(bar_stdin_src);
	if (bar_clock_src)
		wl_event_source_remove(bar_clock_src);
	if (bar_sys_src)
		wl_event_source_remove(bar_sys_src);
	if (bar_kick_src)
		wl_event_source_remove(bar_kick_src);
	if (bar_vol_src)
		wl_event_source_remove(bar_vol_src);
	if (bar_vol_fd >= 0)
		close(bar_vol_fd);
	bar_stdin_src = bar_clock_src = bar_sys_src = bar_kick_src = bar_vol_src = NULL;
	bar_vol_fd = -1;
	for (i = 0; i < BAR_NSOLID; i++) {
		if (bar_solids[i].img)
			pixman_image_unref(bar_solids[i].img);
		bar_solids[i].img = NULL;
	}
	if (bar_font) {
		fcft_destroy(bar_font);
		bar_font = NULL;
		fcft_fini();
	}
}
