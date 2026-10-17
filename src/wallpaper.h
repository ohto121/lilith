#include <drm_fourcc.h>
#include <wlr/interfaces/wlr_buffer.h>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#pragma GCC diagnostic ignored "-Wextra"
#pragma GCC diagnostic ignored "-Wshadow"
#pragma GCC diagnostic ignored "-Wunused-macros"
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wfloat-conversion"
#pragma GCC diagnostic ignored "-Wdeclaration-after-statement"
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#include "stb_image.h"
#pragma GCC diagnostic pop

#define WP_MAXSIDE 8192

typedef struct {
	struct wlr_buffer base;
	uint32_t *px;
} WpBuffer;

static struct wlr_scene_tree *wallpaper_layer;
static struct wlr_buffer *wp_image;

static void
wp_buf_destroy(struct wlr_buffer *b)
{
	WpBuffer *w = wl_container_of(b, w, base);
	free(w->px);
	free(w);
}

static bool
wp_buf_begin(struct wlr_buffer *b, uint32_t flags, void **data,
		uint32_t *format, size_t *stride)
{
	WpBuffer *w = wl_container_of(b, w, base);
	if (flags & WLR_BUFFER_DATA_PTR_ACCESS_WRITE)
		return false;
	*data = w->px;
	*format = DRM_FORMAT_ARGB8888;
	*stride = (size_t)b->width * 4;
	return true;
}

static void
wp_buf_end(struct wlr_buffer *b)
{
}

static const struct wlr_buffer_impl wp_buf_impl = {
	.destroy = wp_buf_destroy,
	.begin_data_ptr_access = wp_buf_begin,
	.end_data_ptr_access = wp_buf_end,
};

static unsigned char *
wp_shrink(const unsigned char *src, int sw, int sh, int dw, int dh)
{
	unsigned char *dst = malloc((size_t)dw * (size_t)dh * 4);
	int x, y, i, j, k;

	if (!dst)
		return NULL;
	for (y = 0; y < dh; y++) {
		int y0 = (int)((long long)y * sh / dh);
		int y1 = (int)((long long)(y + 1) * sh / dh);
		if (y1 <= y0)
			y1 = y0 + 1;
		for (x = 0; x < dw; x++) {
			int x0 = (int)((long long)x * sw / dw);
			int x1 = (int)((long long)(x + 1) * sw / dw);
			unsigned long acc[4] = {0, 0, 0, 0};
			unsigned long n;
			if (x1 <= x0)
				x1 = x0 + 1;
			for (j = y0; j < y1; j++)
				for (i = x0; i < x1; i++)
					for (k = 0; k < 4; k++)
						acc[k] += src[((size_t)j * sw + i) * 4 + k];
			n = (unsigned long)(x1 - x0) * (unsigned long)(y1 - y0);
			for (k = 0; k < 4; k++)
				dst[((size_t)y * dw + x) * 4 + k] = (unsigned char)(acc[k] / n);
		}
	}
	return dst;
}

static struct wlr_buffer *
wp_load(const char *path)
{
	int w, h, n, nw, nh;
	size_t i, cnt;
	unsigned char *px, *small;
	WpBuffer *b;

	if (!(px = stbi_load(path, &w, &h, &n, 4))) {
		fprintf(stderr, "lilith: wallpaper %s: %s\n", path, stbi_failure_reason());
		return NULL;
	}

	cnt = (size_t)w * (size_t)h;
	for (i = 0; i < cnt; i++) {
		unsigned char *p = px + i * 4;
		unsigned int a = p[3];
		if (a == 255)
			continue;
		p[0] = (unsigned char)((p[0] * a + 127) / 255);
		p[1] = (unsigned char)((p[1] * a + 127) / 255);
		p[2] = (unsigned char)((p[2] * a + 127) / 255);
	}

	nw = w;
	nh = h;
	if (w > WP_MAXSIDE || h > WP_MAXSIDE) {
		double k = (double)WP_MAXSIDE / (double)MAX(w, h);
		nw = MAX(1, (int)(w * k));
		nh = MAX(1, (int)(h * k));
		small = wp_shrink(px, w, h, nw, nh);
		free(px);
		if (!(px = small)) {
			fprintf(stderr, "lilith: wallpaper %s: out of memory\n", path);
			return NULL;
		}
	}

	cnt = (size_t)nw * (size_t)nh;
	for (i = 0; i < cnt; i++) {
		unsigned char *p = px + i * 4;
		uint32_t v = (uint32_t)p[3] << 24 | (uint32_t)p[0] << 16
				| (uint32_t)p[1] << 8 | (uint32_t)p[2];
		memcpy(p, &v, 4);
	}

	b = ecalloc(1, sizeof(*b));
	b->px = (uint32_t *)px;
	wlr_buffer_init(&b->base, &wp_buf_impl, nw, nh);
	return &b->base;
}

static void
wallpaper_init(void)
{
	if (!wallpaper_path || !*wallpaper_path)
		return;
	wp_image = wp_load(wallpaper_path);
}

static void
wallpaper_mon_create(Monitor *m)
{
	m->wp_bg = wlr_scene_rect_create(wallpaper_layer, 0, 0, wallpaper_bgcolor);
	if (wp_image && (m->wp_img = wlr_scene_buffer_create(wallpaper_layer, wp_image)))
		wlr_scene_buffer_set_filter_mode(m->wp_img, WLR_SCALE_FILTER_BILINEAR);
	m->wp_valid = 0;
}

static void
wallpaper_mon_destroy(Monitor *m)
{
	if (m->wp_img)
		wlr_scene_node_destroy(&m->wp_img->node);
	if (m->wp_bg)
		wlr_scene_node_destroy(&m->wp_bg->node);
	m->wp_img = NULL;
	m->wp_bg = NULL;
}

static void
wallpaper_cleanup(void)
{
	Monitor *m;

	wl_list_for_each(m, &mons, link)
		wallpaper_mon_destroy(m);
	if (wp_image) {
		wlr_buffer_drop(wp_image);
		wp_image = NULL;
	}
}

static void
wp_warn_small(Monitor *m, double pw, double ph)
{
	if (m->wp_warned || ((double)wp_image->width * ZOOMMIN >= pw
			&& (double)wp_image->height * ZOOMMIN >= ph))
		return;
	m->wp_warned = 1;
	fprintf(stderr, "lilith: wallpaper %dx%d is too small for %s at zoom %.2f, "
			"use at least %dx%d\n", wp_image->width, wp_image->height,
			m->wlr_output->name, (double)ZOOMMIN,
			(int)ceil(pw / ZOOMMIN), (int)ceil(ph / ZOOMMIN));
}

static void
wallpaper_update(Monitor *m)
{
	struct wlr_box *mb = &m->m;
	struct wlr_fbox src;
	double sc, zoom, s, pw, ph, iw, ih, kx, ky, wd, hd, x0, y0, tx, ty;
	int dx0, dy0, dx1, dy1;

	if (!m->wp_bg || mb->width <= 0 || mb->height <= 0)
		return;

	sc = (double)m->wlr_output->scale;
	zoom = 1.0;
	tx = ty = 0.0;
	if (wallpaper_mode == 1) {
		zoom = m->zoom_scale;
		if (m->canvas && wallpaper_pan_factor > 0.0) {
			int ti = getcurrenttag(m);
			double fx = m->w.x + m->w.width / 2.0;
			double fy = m->w.y + m->w.height / 2.0;
			tx = wallpaper_pan_factor * ((m->canvas[ti].cx + m->pan_off_x) * zoom
					+ (m->zoom_pan_x - fx * (1.0 - zoom)));
			ty = wallpaper_pan_factor * ((m->canvas[ti].cy + m->pan_off_y) * zoom
					+ (m->zoom_pan_y - fy * (1.0 - zoom)));
		}
	}
	if (m->wp_valid && fabs(zoom - m->wp_zoom) < 1e-9
			&& fabs(sc - m->wp_scale) < 1e-9
			&& fabs(tx - m->wp_tx) < 1e-3 && fabs(ty - m->wp_ty) < 1e-3
			&& !memcmp(mb, &m->wp_box, sizeof(*mb)))
		return;
	m->wp_valid = 1;
	m->wp_zoom = zoom;
	m->wp_scale = sc;
	m->wp_tx = tx;
	m->wp_ty = ty;
	m->wp_box = *mb;

	wlr_scene_node_set_position(&m->wp_bg->node, mb->x, mb->y);
	wlr_scene_rect_set_size(m->wp_bg, mb->width, mb->height);
	m->wp_k = 1.0;
	m->wp_ox = m->wp_oy = 0.0;
	if (!m->wp_img)
		return;

	iw = (double)wp_image->width;
	ih = (double)wp_image->height;
	pw = (double)mb->width * sc;
	ph = (double)mb->height * sc;

	if (wallpaper_mode == 1) {
		s = pow(zoom, wallpaper_zoom_strength);
		if (wallpaper_edge)
			s = MAX(s, MAX(pw / iw, ph / ih));
		kx = ky = s;
		wp_warn_small(m, pw, ph);
	} else {
		kx = pw / iw;
		ky = ph / ih;
		if (wallpaper_fit == 0)
			kx = ky = MAX(kx, ky);
		else if (wallpaper_fit == 1)
			kx = ky = MIN(kx, ky);
	}

	wd = iw * kx;
	hd = ih * ky;
	x0 = (pw - wd) / 2.0 + tx * sc;
	y0 = (ph - hd) / 2.0 + ty * sc;
	if (wallpaper_mode == 1 && wallpaper_edge) {
		x0 = MIN(0.0, MAX(pw - wd, x0));
		y0 = MIN(0.0, MAX(ph - hd, y0));
	}

	m->wp_k = kx;
	m->wp_ox = x0 / sc;
	m->wp_oy = y0 / sc;

	dx0 = (int)lround(MAX(0.0, x0) / sc);
	dy0 = (int)lround(MAX(0.0, y0) / sc);
	dx1 = (int)lround(MIN(pw, x0 + wd) / sc);
	dy1 = (int)lround(MIN(ph, y0 + hd) / sc);
	if (dx1 <= dx0 || dy1 <= dy0) {
		wlr_scene_node_set_enabled(&m->wp_img->node, 0);
		return;
	}
	src.x = MAX(0.0, ((double)dx0 * sc - x0) / kx);
	src.y = MAX(0.0, ((double)dy0 * sc - y0) / ky);
	src.width  = MIN(iw, ((double)dx1 * sc - x0) / kx) - src.x;
	src.height = MIN(ih, ((double)dy1 * sc - y0) / ky) - src.y;

	wlr_scene_node_set_enabled(&m->wp_img->node, 1);
	wlr_scene_node_set_position(&m->wp_img->node, mb->x + dx0, mb->y + dy0);
	wlr_scene_buffer_set_source_box(m->wp_img, &src);
	wlr_scene_buffer_set_dest_size(m->wp_img, dx1 - dx0, dy1 - dy0);
}

static inline void
wallpaper_transform(Monitor *m, float *scale, float *ox, float *oy)
{
	*scale = (float)m->wp_k;
	*ox = (float)m->wp_ox;
	*oy = (float)m->wp_oy;
}
