#include <getopt.h>
#include <libinput.h>
#include <linux/input-event-codes.h>
#include <math.h>
#include <string.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <wayland-server-core.h>
#include <wlr/backend.h>
#include <wlr/backend/libinput.h>
#include <wlr/render/allocator.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_alpha_modifier_v1.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_cursor_shape_v1.h>
#include <wlr/types/wlr_data_control_v1.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_drm.h>
#include <wlr/types/wlr_export_dmabuf_v1.h>
#include <wlr/types/wlr_ext_data_control_v1.h>
#include <wlr/types/wlr_fractional_scale_v1.h>
#include <wlr/types/wlr_gamma_control_v1.h>
#include <wlr/types/wlr_idle_inhibit_v1.h>
#include <wlr/types/wlr_idle_notify_v1.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_keyboard_group.h>
#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/types/wlr_linux_dmabuf_v1.h>
#include <wlr/types/wlr_linux_drm_syncobj_v1.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_output_management_v1.h>
#include <wlr/types/wlr_output_power_management_v1.h>
#include <wlr/types/wlr_pointer.h>
#include <wlr/types/wlr_pointer_constraints_v1.h>
#include <wlr/types/wlr_pointer_gestures_v1.h>
#include <wlr/types/wlr_presentation_time.h>
#include <wlr/types/wlr_primary_selection.h>
#include <wlr/types/wlr_primary_selection_v1.h>
#include <wlr/types/wlr_relative_pointer_v1.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_screencopy_v1.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_server_decoration.h>
#include <wlr/types/wlr_session_lock_v1.h>
#include <wlr/types/wlr_single_pixel_buffer_v1.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/types/wlr_viewporter.h>
#include <wlr/types/wlr_virtual_keyboard_v1.h>
#include <wlr/types/wlr_virtual_pointer_v1.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/types/wlr_xdg_activation_v1.h>
#include <wlr/types/wlr_xdg_decoration_v1.h>
#include <wlr/types/wlr_xdg_output_v1.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/log.h>
#include <wlr/util/region.h>
#include <xkbcommon/xkbcommon.h>
#ifdef XWAYLAND
#include <wlr/xwayland.h>
#include <xcb/xcb.h>
#include <xcb/xcb_icccm.h>
#endif
#include <wlr/types/wlr_tablet_pad.h>
#include <wlr/types/wlr_tablet_tool.h>
#include <wlr/types/wlr_tablet_v2.h>

#include "util.h"

#define MAX(A, B)               ((A) > (B) ? (A) : (B))
#define MIN(A, B)               ((A) < (B) ? (A) : (B))
#define CLEANMASK(mask)         (mask & ~WLR_MODIFIER_CAPS)
#define VISIBLEON(C, M)         ((M) && (C)->mon == (M) && ((C)->tags & (M)->tagset[(M)->seltags]))
#define LENGTH(X)               (sizeof X / sizeof X[0])
#define END(A)                  ((A) + LENGTH(A))
#define TAGMASK                 ((1u << TAGCOUNT) - 1)
#define LISTEN(E, L, H)         wl_signal_add((E), ((L)->notify = (H), (L)))
#define LISTEN_STATIC(E, H)     do { struct wl_listener *_l = ecalloc(1, sizeof(*_l)); _l->notify = (H); wl_signal_add((E), _l); } while (0)

enum { CurNormal, CurPressed, CurMove, CurResize, CurSpawnRegion };
enum { SnapLeft, SnapRight, SnapTop, SnapBottom,
	SnapTopLeft, SnapTopRight, SnapBottomLeft, SnapBottomRight };
enum { XDGShell, LayerShell, X11 };
enum { LyrBg, LyrBottom, LyrTile, LyrFloat, LyrTop, LyrFS, LyrOverlay, LyrBlock, NUM_LAYERS };
enum { ClkClient, ClkLayerShell, ClkRoot, ClkTagBar, ClkTitle, ClkStatus };
enum { SchemeNorm, SchemeSel, SchemeUrg };
typedef union {
	int i;
	uint32_t ui;
	float f;
	const void *v;
} Arg;

typedef struct {
	unsigned int click;
	unsigned int mod;
	unsigned int button;
	void (*func)(const Arg *);
	const Arg arg;
} Button;

typedef struct Monitor Monitor;
typedef struct {
	unsigned int type;

	Monitor *mon;
	struct wlr_scene_tree *scene;

	struct wlr_scene_rect *inner_border[4];
	struct wlr_scene_rect *outer_border[4];

	struct wlr_scene_tree *scene_surface;
	struct wl_list link;
	struct wl_list flink;
	struct wlr_box geom;
	struct wlr_box prev;
	struct wlr_box bounds;
	union {
		struct wlr_xdg_surface *xdg;
		struct wlr_xwayland_surface *xwayland;
	} surface;
	struct wlr_xdg_toplevel_decoration_v1 *decoration;
	struct wl_listener commit;
	struct wl_listener map;
	struct wl_listener maximize;
	struct wl_listener unmap;
	struct wl_listener destroy;
	struct wl_listener set_title;
	struct wl_listener fullscreen;
	struct wl_listener set_decoration_mode;
	struct wl_listener destroy_decoration;
#ifdef XWAYLAND
	struct wl_listener activate;
	struct wl_listener associate;
	struct wl_listener dissociate;
	struct wl_listener configure;
	struct wl_listener set_hints;
#endif
	unsigned int bw;
	unsigned int ibw;
	unsigned int obw;
	uint32_t tags;
	int isfloating, isurgent, isfullscreen, isfakefullscreen;
	uint32_t resize;
	int saved_cx, saved_cy;
	int saved_cw, saved_ch;
	int was_on_canvas;
	int home_cx, home_cy;
	int inertia;
	double ivx, ivy;
	double irx, iry;
	uint64_t inertia_ms;
	int opening;
	int was_vis;
	struct wlr_scene_tree *open_tree;
} Client;

typedef struct {
	int cx, cy;
	int saved_cx, saved_cy;
} CanvasOffset;

typedef struct {
	uint32_t mod;
	xkb_keysym_t keysym;
	void (*func)(const Arg *);
	const Arg arg;
} Key;

typedef struct {
	struct wlr_keyboard_group *wlr_group;

	int nsyms;
	const xkb_keysym_t *keysyms;
	xkb_keysym_t symbuf[16];
	uint32_t mods;
	struct wl_event_source *key_repeat_source;

	struct wl_listener modifiers;
	struct wl_listener key;
	struct wl_listener destroy;
} KeyboardGroup;

typedef struct {
	unsigned int type;

	Monitor *mon;
	struct wlr_scene_tree *scene;
	struct wlr_scene_tree *popups;
	struct wlr_scene_layer_surface_v1 *scene_layer;
	struct wl_list link;
	int mapped;
	struct wlr_layer_surface_v1 *layer_surface;

	struct wl_listener destroy;
	struct wl_listener unmap;
	struct wl_listener surface_commit;
} LayerSurface;

typedef struct {
	struct wl_list link;
	char name[64];
	unsigned int seltags;
	uint32_t tagset[2];
	CanvasOffset *canvas;
	double zoom_scale;
	double zoom_target;
	double zoom_pan_x;
	double zoom_pan_y;
} MonitorState;

struct Monitor {
	struct wl_list link;
	struct wlr_output *wlr_output;
	struct wlr_scene_output *scene_output;
	struct wlr_scene_rect *fullscreen_bg;
	struct wl_listener frame;
	struct wl_listener destroy;
	struct wl_listener request_state;
	struct wl_listener destroy_lock_surface;
	struct wlr_session_lock_surface_v1 *lock_surface;
	struct wlr_box m;
	struct wlr_box w;
	struct wl_list layers[4];
	unsigned int seltags;
	uint32_t tagset[2];
	int gamma_lut_changed;
	int asleep;
	CanvasOffset *canvas;
	double zoom_scale;
	double zoom_target;
	double zoom_pan_x, zoom_pan_y;
	double zoom_anchor_fx, zoom_anchor_fy;
	double zoom_anchor_lx, zoom_anchor_ly;
	double zoom_anim_t;
	double zoom_start_scale;
	double zoom_start_pan_x, zoom_start_pan_y;
	int zoom_popups_dirty;
	double pan_off_x, pan_off_y;
	double pan_from_x, pan_from_y;
	uint64_t pan_start_ms;
	int pan_anim;
	double tag_off_x, tag_from_x;
	uint64_t tag_start_ms;
	int tag_anim;
	struct wlr_scene_rect *wp_bg;
	struct wlr_scene_buffer *wp_img;
	int wp_valid, wp_warned;
	double wp_zoom, wp_scale, wp_tx, wp_ty;
	struct wlr_box wp_box;
	double wp_k, wp_ox, wp_oy;
	struct wlr_scene_buffer *bar_scene;
	struct wlr_box bar_area;
	int showbar;
	int bar_vis;
	int bar_pos;
	int bar_float;
	int bar_above;
	int bar_tagx[32];
	int bar_tagw[32];
	int bar_statusx;
	int bar_statusw;
	struct wlr_buffer *bar_bufs[3];
	int bar_buf_w, bar_buf_h;
	void *bar_scratch;
	int bar_scratch_w, bar_scratch_h;
	uint32_t bar_prevtags;
	int bar_prevtags_ok;
	int bar_anim_on;
	uint64_t bar_anim_ms;
	double bar_pill_x, bar_pill_l;
	double bar_pill_fx, bar_pill_fl;
	double bar_prog;
	double bar_hanim_from;
	int bar_hanim_to;
	int bar_hanim_on;
	uint64_t bar_hanim_ms;
	int bar_pill_ok;
	float bar_w[32], bar_wfrom[32];
};

typedef struct {
	const char *name;
	float scale;
	enum wl_output_transform rr;
	int x, y;
	int resx;
	int resy;
	float rate;
	int mode;
	int adaptive;
} MonitorRule;

typedef struct {
	struct wlr_pointer_constraint_v1 *constraint;
	struct wl_listener destroy;
} PointerConstraint;

typedef struct {
	const char *id;
	const char *title;
	uint32_t tags;
	int isfloating;
	int monitor;
} Rule;

typedef struct {
	struct wlr_scene_tree *scene;

	struct wlr_session_lock_v1 *lock;
	struct wl_listener new_surface;
	struct wl_listener unlock;
	struct wl_listener destroy;
} SessionLock;

typedef struct {
	uint32_t button;
	uint32_t state;
	void (*func)(const Arg *);
	const Arg arg;
} TabletPadButton;

typedef struct {
	uint32_t button;
	uint32_t state;
	void (*func)(const Arg *);
	const Arg arg;
} TabletButton;

static void applybounds(Client *c, struct wlr_box *bbox);
static void applyrules(Client *c);
static void arrange(Monitor *m);
static void arrangelayer(Monitor *m, struct wl_list *list,
		struct wlr_box *usable_area, int exclusive);
static void arrangelayers(Monitor *m);
static void axisnotify(struct wl_listener *listener, void *data);
static void buttonpress(struct wl_listener *listener, void *data);
static void chvt(const Arg *arg);
static void checkidleinhibitor(struct wlr_surface *exclude);
static void cleanup(void);
static void cleanupmon(struct wl_listener *listener, void *data);
static void cleanuplisteners(void);
static void closemon(Monitor *m);
static void commitlayersurfacenotify(struct wl_listener *listener, void *data);
static void commitnotify(struct wl_listener *listener, void *data);
static void commitpopup(struct wl_listener *listener, void *data);
static void createdecoration(struct wl_listener *listener, void *data);
static void createidleinhibitor(struct wl_listener *listener, void *data);
static void createkeyboard(struct wlr_keyboard *keyboard);
static KeyboardGroup *createkeyboardgroup(void);
static void createlayersurface(struct wl_listener *listener, void *data);
static void createlocksurface(struct wl_listener *listener, void *data);
static void createmon(struct wl_listener *listener, void *data);
static void createnotify(struct wl_listener *listener, void *data);
static void createpointer(struct wlr_pointer *pointer);
static void createpointerconstraint(struct wl_listener *listener, void *data);
static void createpopup(struct wl_listener *listener, void *data);
static void cursorconstrain(struct wlr_pointer_constraint_v1 *constraint);
static void cursorframe(struct wl_listener *listener, void *data);
static void cursorwarptohint(void);
static void destroydecoration(struct wl_listener *listener, void *data);
static void destroydragicon(struct wl_listener *listener, void *data);
static void destroyidleinhibitor(struct wl_listener *listener, void *data);
static void destroylayersurfacenotify(struct wl_listener *listener, void *data);
static void destroylock(SessionLock *lock, int unlocked);
static void destroylocksurface(struct wl_listener *listener, void *data);
static void destroynotify(struct wl_listener *listener, void *data);
static void destroypointerconstraint(struct wl_listener *listener, void *data);
static void destroysessionlock(struct wl_listener *listener, void *data);
static void destroykeyboardgroup(struct wl_listener *listener, void *data);
static Monitor *dirtomon(enum wlr_direction dir);
static void focusclient(Client *c, int lift);
static void focusmon(const Arg *arg);
static void focusstack(const Arg *arg);
static void focusstackcenter(const Arg *arg);
static void focusdir(const Arg *arg);
static Client *focustop(Monitor *m);
static void fullscreennotify(struct wl_listener *listener, void *data);
static void gpureset(struct wl_listener *listener, void *data);
static void handlesig(int signo);
static void inputdevice(struct wl_listener *listener, void *data);
static int keybinding(uint32_t mods, xkb_keysym_t sym);
static void keypress(struct wl_listener *listener, void *data);
static void keypressmod(struct wl_listener *listener, void *data);
static int keyrepeat(void *data);
static void killclient(const Arg *arg);
static void locksession(struct wl_listener *listener, void *data);
static void mapnotify(struct wl_listener *listener, void *data);
static void maximizenotify(struct wl_listener *listener, void *data);
static void motionabsolute(struct wl_listener *listener, void *data);
static void motionnotify(uint32_t time, struct wlr_input_device *device, double sx,
		double sy, double sx_unaccel, double sy_unaccel);
static void motionrelative(struct wl_listener *listener, void *data);
static void moveresize(const Arg *arg);
static void outputmgrapply(struct wl_listener *listener, void *data);
static void outputmgrapplyortest(struct wlr_output_configuration_v1 *config, int test);
static void outputmgrtest(struct wl_listener *listener, void *data);
static void pointerfocus(Client *c, struct wlr_surface *surface,
		double sx, double sy, uint32_t time);
static void printstatus(void);
static void powermgrsetmode(struct wl_listener *listener, void *data);
static void quit(const Arg *arg);
static void rendermon(struct wl_listener *listener, void *data);
static void requestdecorationmode(struct wl_listener *listener, void *data);
static void requeststartdrag(struct wl_listener *listener, void *data);
static void requestmonstate(struct wl_listener *listener, void *data);
static void resize(Client *c, struct wlr_box geo, int interact);
static void run(char *startup_cmd);
static void setcursor(struct wl_listener *listener, void *data);
static void setcursorshape(struct wl_listener *listener, void *data);
static void setfloating(Client *c, int floating);
static void setfullscreen(Client *c, int fullscreen);
static void setmon(Client *c, Monitor *m, uint32_t newtags);
static void setpsel(struct wl_listener *listener, void *data);
static void setsel(struct wl_listener *listener, void *data);
static void setup(void);
static void spawn(const Arg *arg);
static void startdrag(struct wl_listener *listener, void *data);
static void tag(const Arg *arg);
static void tagmon(const Arg *arg);
static void togglefullscreen(const Arg *arg);
static void togglefakefullscreen(const Arg *arg);
static void snaptoposition(const Arg *arg);
static void toggletag(const Arg *arg);
static void togglebar(const Arg *arg);
static void barsetpos(const Arg *arg);
static void barcyclepos(const Arg *arg);
static void togglebarfloat(const Arg *arg);
static void togglebarlayer(const Arg *arg);
static void barspawn(const Arg *arg);
static void toggleview(const Arg *arg);
static void unlocksession(struct wl_listener *listener, void *data);
static void unmaplayersurfacenotify(struct wl_listener *listener, void *data);
static void unmapnotify(struct wl_listener *listener, void *data);
static void updatemons(struct wl_listener *listener, void *data);
static void updatetitle(struct wl_listener *listener, void *data);
static void urgent(struct wl_listener *listener, void *data);
static void view(const Arg *arg);
static void virtualkeyboard(struct wl_listener *listener, void *data);
static void virtualpointer(struct wl_listener *listener, void *data);
static Monitor *xytomon(double x, double y);
static void xytonode(double x, double y, struct wlr_surface **psurface,
		Client **pc, LayerSurface **pl, double *nx, double *ny);
static int getcurrenttag(Monitor *m);
static void canvas_move_window(Client *c, int x, int y);
static void snap_edges(Client *c, Monitor *m, int *nx, int *ny);
static void resize_magnetically(Client *c, Monitor *m, struct wlr_box *geo);
static void homecanvas(const Arg *arg);
static void movecanvas(const Arg *arg);
static void movewinbykeys(const Arg *arg);
static void resizewinbykeys(const Arg *arg);
static void movecanvasmouse(const Arg *arg);
static void centerwindow(const Arg *arg);
static void bringintoview(Client *c);
static void save_canvas_positions(Monitor *m);
static void restore_canvas_positions(Monitor *m);
static void zoom(const Arg *arg);
static void zoomreset(const Arg *arg);
static void zoomfit(const Arg *arg);
static void zoom_apply(Monitor *m);
static void zoom_update_scene_client(Client *c);
static void zoom_scale_buffers(struct wlr_scene_buffer *buf, int sx, int sy, void *data);
static void zoom_frame(Monitor *m);
static double bezier_ease(const char *spec, double t);
static void canvas_shift(Monitor *m, int dx, int dy, int animate);
static void pan_anim_cancel(Monitor *m);
static void pan_anim_step(Monitor *m);
static void closeanim_start(Client *c);
static void closeanim_step(Monitor *m);
static void closeanim_flush(Monitor *m);
static void openanim_reveal(Client *c);
static void openanim_start(Client *c);
static void openanim_abort(Client *c);
static void tagopen_start(Monitor *m);
static void tagslide_capture(Monitor *m, int dir, uint32_t newtags);
static void tagslide_start(Monitor *m, int dir);
static void tagslide_step(Monitor *m);
static void inertia_grab_begin(Client *c);
static void inertia_sample(int x, int y);
static void inertia_release(Client *c);
static void inertia_step(Monitor *m);
static void canvas_inertia_grab_begin(void);
static void canvas_inertia_release(Monitor *m);
static void canvas_inertia_step(Monitor *m);
static void popup_zoom_commit(struct wl_listener *listener, void *data);
static void popup_zoom_destroy(struct wl_listener *listener, void *data);
static void spawnregion(const Arg *arg);
static void spawnregion_update_rect(void);
static void spawnregion_finish(void);
static void createtablet(struct wlr_input_device *device);
static void destroytablet(struct wl_listener *listener, void *data);
static void destroytabletpad(struct wl_listener *listener, void *data);
static void destroytabletsurfacenotify(struct wl_listener *listener, void *data);
static void destroytablettool(struct wl_listener *listener, void *data);
static void tablettoolmotion(struct wlr_tablet_v2_tablet_tool *tool, bool change_x, bool change_y, double x, double y, double dx, double dy);
static void tablettoolproximity(struct wl_listener *listener, void *data);
static void tabletpadbutton(struct wl_listener *listener, void *data);
static void tablettoolaxis(struct wl_listener *listener, void *data);
static void tablettoolbutton(struct wl_listener *listener, void *data);
static void tablettooltip(struct wl_listener *listener, void *data);

static pid_t child_pid = -1;
static int locked;
static void *exclusive_focus;
static struct wl_display *dpy;
static struct wl_event_loop *event_loop;
static struct wlr_backend *backend;
static struct wlr_scene *scene;
static struct wlr_scene_tree *layers[NUM_LAYERS];
static struct wlr_scene_tree *drag_icon;
static const int layermap[] = { LyrBg, LyrBottom, LyrTop, LyrOverlay };
static struct wlr_renderer *drw;
static struct wlr_allocator *alloc;
static struct wlr_compositor *compositor;
static struct wlr_session *session;

static struct wlr_tablet_manager_v2 *tablet_mgr;
static struct wlr_tablet_v2_tablet *tablet = NULL;
static struct wlr_tablet_v2_tablet_tool *tablet_tool = NULL;
static struct wlr_tablet_v2_tablet_pad *tablet_pad = NULL;
static struct wlr_surface *tablet_curr_surface = NULL;
static struct wl_listener destroy_tablet_surface_listener = {.notify = destroytabletsurfacenotify};

static struct wl_listener tablet_device_destroy = {.notify = destroytablet};
static struct wl_listener tablet_pad_destroy = {.notify = destroytabletpad};
static struct wl_listener tablet_pad_button = {.notify = tabletpadbutton};
static struct wl_listener tablet_tool_axis = {.notify = tablettoolaxis};
static struct wl_listener tablet_tool_button = {.notify = tablettoolbutton};
static struct wl_listener tablet_tool_destroy = {.notify = destroytablettool};
static struct wl_listener tablet_tool_proximity = {.notify = tablettoolproximity};
static struct wl_listener tablet_tool_tip = {.notify = tablettooltip};

static struct wlr_xdg_shell *xdg_shell;
static struct wlr_xdg_activation_v1 *activation;
static struct wlr_xdg_decoration_manager_v1 *xdg_decoration_mgr;
static struct wl_list clients;
static struct wl_list fstack;
static struct wlr_idle_notifier_v1 *idle_notifier;
static struct wlr_idle_inhibit_manager_v1 *idle_inhibit_mgr;
static struct wlr_layer_shell_v1 *layer_shell;
static struct wlr_output_manager_v1 *output_mgr;
static struct wlr_virtual_keyboard_manager_v1 *virtual_keyboard_mgr;
static struct wlr_virtual_pointer_manager_v1 *virtual_pointer_mgr;
static struct wlr_cursor_shape_manager_v1 *cursor_shape_mgr;
static struct wlr_output_power_manager_v1 *power_mgr;

static struct wlr_pointer_constraints_v1 *pointer_constraints;
static struct wlr_pointer_gestures_v1 *pointer_gestures;
static struct wlr_relative_pointer_manager_v1 *relative_pointer_mgr;
static struct wlr_pointer_constraint_v1 *active_constraint;

static struct wlr_cursor *cursor;
static struct wlr_xcursor_manager *cursor_mgr;

static struct wlr_scene_rect *root_bg;
static struct wlr_session_lock_manager_v1 *session_lock_mgr;
static struct wlr_scene_rect *locked_bg;
static struct wlr_session_lock_v1 *cur_lock;

static struct wlr_seat *seat;
static KeyboardGroup *kb_group;
static unsigned int cursor_mode;
static Client *grabc;
static int grabcx, grabcy;
static double resize_grab_world_x = 0.0;
static double resize_grab_world_y = 0.0;
static int resize_snap_x_target = 0;
static int resize_snap_y_target = 0;
typedef struct {
	struct wl_list link;
	struct wl_listener commit;
	struct wl_listener destroy;
	Client *client;
	struct wlr_xdg_popup *popup;
	struct wlr_scene_node *node;
} PopupZoomData;
static struct wl_list popup_zoom_list;

static int canvas_panning = 0;
static double canvas_pan_last_x = 0, canvas_pan_last_y = 0;
static float canvas_pan_multiplier = 1.0f;
static float canvas_pan_accum_x = 0.0f, canvas_pan_accum_y = 0.0f;
static int spawnregion_active = 0;
static double spawnregion_x1 = 0, spawnregion_y1 = 0;
static double spawnregion_x2 = 0, spawnregion_y2 = 0;
static struct wlr_scene_rect *spawnregion_rect = NULL;
static struct wlr_box pending_spawn_geo = {0};
static int has_pending_spawn_geo = 0;
Client *waiting_geo_client = NULL;

static struct wlr_output_layout *output_layout;
static struct wlr_box sgeom;
static struct wl_list mons;
static Monitor *selmon;
static struct wl_list savedstates;

static struct wl_listener cursor_axis = {.notify = axisnotify};
static struct wl_listener cursor_button = {.notify = buttonpress};
static struct wl_listener cursor_frame = {.notify = cursorframe};
static struct wl_listener cursor_motion = {.notify = motionrelative};
static struct wl_listener cursor_motion_absolute = {.notify = motionabsolute};
static struct wl_listener gpu_reset = {.notify = gpureset};
static struct wl_listener layout_change = {.notify = updatemons};
static struct wl_listener new_idle_inhibitor = {.notify = createidleinhibitor};
static struct wl_listener new_input_device = {.notify = inputdevice};
static struct wl_listener new_virtual_keyboard = {.notify = virtualkeyboard};
static struct wl_listener new_virtual_pointer = {.notify = virtualpointer};
static struct wl_listener new_pointer_constraint = {.notify = createpointerconstraint};
static struct wl_listener new_output = {.notify = createmon};
static struct wl_listener new_xdg_toplevel = {.notify = createnotify};
static struct wl_listener new_xdg_popup = {.notify = createpopup};
static struct wl_listener new_xdg_decoration = {.notify = createdecoration};
static struct wl_listener new_layer_surface = {.notify = createlayersurface};
static struct wl_listener output_mgr_apply = {.notify = outputmgrapply};
static struct wl_listener output_mgr_test = {.notify = outputmgrtest};
static struct wl_listener output_power_mgr_set_mode = {.notify = powermgrsetmode};
static struct wl_listener request_activate = {.notify = urgent};
static struct wl_listener request_cursor = {.notify = setcursor};
static struct wl_listener request_set_psel = {.notify = setpsel};
static struct wl_listener request_set_sel = {.notify = setsel};
static struct wl_listener request_set_cursor_shape = {.notify = setcursorshape};
static struct wl_listener request_start_drag = {.notify = requeststartdrag};
static struct wl_listener start_drag = {.notify = startdrag};
static struct wl_listener new_session_lock = {.notify = locksession};

#ifdef XWAYLAND
static void activatex11(struct wl_listener *listener, void *data);
static void associatex11(struct wl_listener *listener, void *data);
static void configurex11(struct wl_listener *listener, void *data);
static void createnotifyx11(struct wl_listener *listener, void *data);
static void dissociatex11(struct wl_listener *listener, void *data);
static void sethints(struct wl_listener *listener, void *data);
static void xwaylandready(struct wl_listener *listener, void *data);
static struct wl_listener new_xwayland_surface = {.notify = createnotifyx11};
static struct wl_listener xwayland_ready = {.notify = xwaylandready};
static struct wlr_xwayland *xwayland;
#endif

#define SCROLL_UP 0x14b
#define SCROLL_DOWN 0x14c

#include "config.h"

#if SPAWNREGION_STYLE == 2
static struct wlr_scene_rect *spawnregion_border[8] = {NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL};
#endif

#ifndef tagslide
#define tagslide 1.0
#endif
#ifndef tagfadeout
#define tagfadeout 1.0
#endif

#ifndef zoomnearest
#define zoomnearest 1
#endif

#include "client.h"
#include "wallpaper.h"
#include "bar.h"

void
applybounds(Client *c, struct wlr_box *bbox)
{
	c->geom.width = MAX(1 + 2 * (int)c->bw, c->geom.width);
	c->geom.height = MAX(1 + 2 * (int)c->bw, c->geom.height);

	if (c->geom.x >= bbox->x + bbox->width)
		c->geom.x = bbox->x + bbox->width - c->geom.width;
	if (c->geom.y >= bbox->y + bbox->height)
		c->geom.y = bbox->y + bbox->height - c->geom.height;
	if (c->geom.x + c->geom.width <= bbox->x)
		c->geom.x = bbox->x;
	if (c->geom.y + c->geom.height <= bbox->y)
		c->geom.y = bbox->y;
}

void
applyrules(Client *c)
{
	const char *appid, *title;
	uint32_t newtags = 0;
	int i;
	const Rule *r;
	Monitor *mon = selmon, *m;

	appid = client_get_appid(c);
	title = client_get_title(c);

	for (r = rules; r < END(rules); r++) {
		if ((!r->title || strstr(title, r->title))
				&& (!r->id || strstr(appid, r->id))) {
			c->isfloating = r->isfloating;
			newtags |= r->tags;
			i = 0;
			wl_list_for_each(m, &mons, link) {
				if (r->monitor == i++)
					mon = m;
			}
		}
	}

	c->isfloating |= client_is_float_type(c);
	setmon(c, mon, newtags);
}

void
arrange(Monitor *m)
{
	Client *c;

	if (!m->wlr_output->enabled)
		return;

	wl_list_for_each(c, &clients, link) {
		if (c->mon == m) {
			wlr_scene_node_set_enabled(&c->scene->node, VISIBLEON(c, m) && c != waiting_geo_client && !c->opening);
			client_set_suspended(c, !VISIBLEON(c, m));
		}
	}

	wlr_scene_node_set_enabled(&m->fullscreen_bg->node,
			(c = focustop(m)) && c->isfullscreen);

	wl_list_for_each(c, &clients, link) {
		if (c->mon != m || c->scene->node.parent == layers[LyrFS])
			continue;

		if (c->isfloating)
			wlr_scene_node_reparent(&c->scene->node, layers[LyrTile]);
	}

	zoom_apply(m);
	motionnotify(0, NULL, 0, 0, 0, 0);
	checkidleinhibitor(NULL);
}

void
arrangelayer(Monitor *m, struct wl_list *list, struct wlr_box *usable_area, int exclusive)
{
	LayerSurface *l;
	struct wlr_box full_area = m->m;

	wl_list_for_each(l, list, link) {
		struct wlr_layer_surface_v1 *layer_surface = l->layer_surface;

		if (!layer_surface->initialized)
			continue;

		if (exclusive != (layer_surface->current.exclusive_zone > 0))
			continue;

		wlr_scene_layer_surface_v1_configure(l->scene_layer, &full_area, usable_area);
		wlr_scene_node_set_position(&l->popups->node, l->scene->node.x, l->scene->node.y);
	}
}

void
arrangelayers(Monitor *m)
{
	int i;
	struct wlr_box usable_area = m->m;
	LayerSurface *l;
	uint32_t layers_above_shell[] = {
		ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY,
		ZWLR_LAYER_SHELL_V1_LAYER_TOP,
	};
	if (!m->wlr_output->enabled)
		return;

	bar_reserve(m, &usable_area);

	for (i = 3; i >= 0; i--)
		arrangelayer(m, &m->layers[i], &usable_area, 1);

	if (!wlr_box_equal(&usable_area, &m->w)) {
		m->w = usable_area;
		arrange(m);
	}

	for (i = 3; i >= 0; i--)
		arrangelayer(m, &m->layers[i], &usable_area, 0);

	for (i = 0; i < (int)LENGTH(layers_above_shell); i++) {
		wl_list_for_each_reverse(l, &m->layers[layers_above_shell[i]], link) {
			if (locked || !l->layer_surface->current.keyboard_interactive || !l->mapped)
				continue;
			focusclient(NULL, 0);
			exclusive_focus = l;
			client_notify_enter(l->layer_surface->surface, wlr_seat_get_keyboard(seat));
			return;
		}
	}
}

void
axisnotify(struct wl_listener *listener, void *data)
{
	unsigned int click;
	Client *c;
	LayerSurface *l;
	struct wlr_pointer_axis_event *event = data;
	struct wlr_keyboard *keyboard;
	uint32_t mods;
	const Button *b;

	wlr_idle_notifier_v1_notify_activity(idle_notifier, seat);

	click = ClkRoot;
	xytonode(cursor->x, cursor->y, NULL, &c, &l, NULL, NULL);
	if (c)
		click = ClkClient;
	else if (l)
		click = ClkLayerShell;

	keyboard = wlr_seat_get_keyboard(seat);
	mods = keyboard ? wlr_keyboard_get_modifiers(keyboard) : 0;

	if (bar_hover && event->orientation == WL_POINTER_AXIS_VERTICAL_SCROLL) {
		unsigned int bbtn = (event->delta > 0 || event->delta_discrete > 0)
				? SCROLL_DOWN : SCROLL_UP;
		bar_click(bar_hover, cursor->x, cursor->y, bbtn, mods);
		return;
	}

	if (event->orientation == WL_POINTER_AXIS_VERTICAL_SCROLL) {
		unsigned int btn = (event->delta > 0 || event->delta_discrete > 0)
				? SCROLL_DOWN : SCROLL_UP;
		for (b = buttons; b < END(buttons); b++) {
			if (CLEANMASK(mods) == CLEANMASK(b->mod)
					&& btn == b->button && click == b->click && b->func) {
				b->func(&b->arg);
				return;
			}
		}
	}

	wlr_seat_pointer_notify_axis(seat,
			event->time_msec, event->orientation, event->delta,
			event->delta_discrete, event->source, event->relative_direction);
}

void
buttonpress(struct wl_listener *listener, void *data)
{
	int inertia_go =0;
	unsigned int click;
	struct wlr_pointer_button_event *event = data;
	struct wlr_keyboard *keyboard;
	uint32_t mods;
	Client *c;
	LayerSurface *l;
	const Button *b;
	Monitor *barmon;

	wlr_idle_notifier_v1_notify_activity(idle_notifier, seat);

	click = ClkRoot;
	xytonode(cursor->x, cursor->y, NULL, &c, &l, NULL, NULL);
	barmon = bar_hover;
	if (c)
		click = ClkClient;
	else if (l)
		click = ClkLayerShell;
	wlr_log(WLR_DEBUG, "buttonpress: c=%p l=%p click=%u (0=Client,1=LayerShell,2=Root)", (void*)c, (void*)l, click);

	switch (event->state) {
	case WL_POINTER_BUTTON_STATE_PRESSED:
		cursor_mode = CurPressed;
		selmon = xytomon(cursor->x, cursor->y);
		if (locked)
			break;

		if (barmon) {
			keyboard = wlr_seat_get_keyboard(seat);
			mods = keyboard ? wlr_keyboard_get_modifiers(keyboard) : 0;
			bar_click(barmon, cursor->x, cursor->y, event->button, mods);
			return;
		}

		if (spawnregion_active && event->button == BTN_LEFT) {
			spawnregion_x1 = cursor->x;
			spawnregion_y1 = cursor->y;
			spawnregion_x2 = cursor->x;
			spawnregion_y2 = cursor->y;
#if SPAWNREGION_STYLE == 2
			{
				int i;
				for (i = 0; i < 4; i++) {
					if (!spawnregion_border[i]) {
						const float *oc = spawnregion_outer_border_color;
						spawnregion_border[i] = wlr_scene_rect_create(layers[LyrBlock],
							1, 1, (float[4]){oc[0], oc[1], oc[2], oc[3]});
					}
				}
				for (i = 4; i < 8; i++) {
					if (!spawnregion_border[i]) {
						const float *ic = spawnregion_inner_border_color;
						spawnregion_border[i] = wlr_scene_rect_create(layers[LyrBlock],
							1, 1, (float[4]){ic[0], ic[1], ic[2], ic[3]});
					}
				}
			}
#else
			if (!spawnregion_rect)
#  if SPAWNREGION_STYLE == 1
				spawnregion_rect = wlr_scene_rect_create(layers[LyrBlock],
					1, 1, (float[4])SPAWNREGION_COLOR);
#  else
				spawnregion_rect = wlr_scene_rect_create(layers[LyrBlock],
					1, 1, (float[4]){0.2f, 0.5f, 1.0f, 0.0f});
#  endif
			wlr_scene_node_set_position(&spawnregion_rect->node,
				(int)spawnregion_x1, (int)spawnregion_y1);
			wlr_scene_rect_set_size(spawnregion_rect, 1, 1);
			wlr_scene_node_set_enabled(&spawnregion_rect->node, 1);
#endif
			cursor_mode = CurSpawnRegion;
			return;
		}

		xytonode(cursor->x, cursor->y, NULL, &c, NULL, NULL, NULL);
		if (click == ClkClient && (!client_is_unmanaged(c) || client_wants_focus(c)))
			focusclient(c, 1);

		keyboard = wlr_seat_get_keyboard(seat);
		mods = keyboard ? wlr_keyboard_get_modifiers(keyboard) : 0;
		for (b = buttons; b < END(buttons); b++) {
			if (CLEANMASK(mods) == CLEANMASK(b->mod) &&
					event->button == b->button && click == b->click && b->func) {
				b->func(&b->arg);
				return;
			}
		}
		break;
	case WL_POINTER_BUTTON_STATE_RELEASED:
		if (!locked && cursor_mode != CurNormal && cursor_mode != CurPressed) {
			if (canvas_panning) {
				canvas_panning = 0;
				wlr_cursor_set_xcursor(cursor, cursor_mgr, "default");
				cursor_mode = CurNormal;
				canvas_inertia_release(selmon);
				return;
			}
			if (cursor_mode == CurSpawnRegion) {
				spawnregion_finish();
				return;
			}
			inertia_go = (cursor_mode == CurMove);
			wlr_cursor_set_xcursor(cursor, cursor_mgr, "default");
			cursor_mode = CurNormal;
			if (grabc && grabc->mon) {
				int tagidx = getcurrenttag(grabc->mon);
				grabc->home_cx = grabc->mon->canvas[tagidx].cx;
				grabc->home_cy = grabc->mon->canvas[tagidx].cy;
			}
			selmon = xytomon(cursor->x, cursor->y);
			setmon(grabc, selmon, 0);
			if (inertia_go)
				inertia_release(grabc);
			grabc = NULL;
			return;
		}
		cursor_mode = CurNormal;
		break;
	}
	wlr_seat_pointer_notify_button(seat,
			event->time_msec, event->button, event->state);
}

void
chvt(const Arg *arg)
{
	wlr_session_change_vt(session, arg->ui);
}

void
checkidleinhibitor(struct wlr_surface *exclude)
{
	int inhibited = 0, unused_lx, unused_ly;
	struct wlr_idle_inhibitor_v1 *inhibitor;
	wl_list_for_each(inhibitor, &idle_inhibit_mgr->inhibitors, link) {
		struct wlr_surface *surface = wlr_surface_get_root_surface(inhibitor->surface);
		struct wlr_scene_tree *tree = surface->data;
		if (exclude != surface && (bypass_surface_visibility || (!tree
				|| wlr_scene_node_coords(&tree->node, &unused_lx, &unused_ly)))) {
			inhibited = 1;
			break;
		}
	}

	wlr_idle_notifier_v1_set_inhibited(idle_notifier, inhibited);
}

void
cleanup(void)
{
	cleanuplisteners();
	bar_cleanup();
#ifdef XWAYLAND
	wlr_xwayland_destroy(xwayland);
	xwayland = NULL;
#endif
	wl_display_destroy_clients(dpy);
	if (child_pid > 0) {
		kill(-child_pid, SIGTERM);
		waitpid(child_pid, NULL, 0);
	}
	wlr_xcursor_manager_destroy(cursor_mgr);
	wallpaper_cleanup();

	destroykeyboardgroup(&kb_group->destroy, NULL);

	wlr_backend_destroy(backend);

	wl_display_destroy(dpy);
	wlr_scene_node_destroy(&scene->tree.node);
}

void
cleanupmon(struct wl_listener *listener, void *data)
{
	Monitor *m = wl_container_of(listener, m, destroy);
	LayerSurface *l, *tmp;
	size_t i;

	for (i = 0; i < LENGTH(m->layers); i++) {
		wl_list_for_each_safe(l, tmp, &m->layers[i], link)
			wlr_layer_surface_v1_destroy(l->layer_surface);
	}

	closeanim_flush(m);
	wl_list_remove(&m->destroy.link);
	wl_list_remove(&m->frame.link);
	wl_list_remove(&m->link);
	wl_list_remove(&m->request_state.link);
	if (m->lock_surface)
		destroylocksurface(&m->destroy_lock_surface, NULL);
	m->wlr_output->data = NULL;
	wlr_output_layout_remove(output_layout, m->wlr_output);
	wlr_scene_output_destroy(m->scene_output);

	closemon(m);
	wlr_scene_node_destroy(&m->fullscreen_bg->node);
	bar_mon_destroy(m);
	wallpaper_mon_destroy(m);
	{
		MonitorState *ms, *tmp_ms;
		wl_list_for_each_safe(ms, tmp_ms, &savedstates, link) {
			if (strcmp(ms->name, m->wlr_output->name) == 0) {
				wl_list_remove(&ms->link);
				free(ms->canvas);
				free(ms);
				break;
			}
		}
		ms = ecalloc(1, sizeof(MonitorState));
		strncpy(ms->name, m->wlr_output->name, sizeof(ms->name) - 1);
		ms->seltags = m->seltags;
		ms->tagset[0] = m->tagset[0];
		ms->tagset[1] = m->tagset[1];
		ms->canvas = m->canvas;
		m->canvas = NULL;
		ms->zoom_scale = m->zoom_scale;
		ms->zoom_target = m->zoom_target;
		ms->zoom_pan_x = m->zoom_pan_x;
		ms->zoom_pan_y = m->zoom_pan_y;
		wl_list_insert(&savedstates, &ms->link);
	}
	free(m);
}

void
cleanuplisteners(void)
{
	wl_list_remove(&cursor_axis.link);
	wl_list_remove(&cursor_button.link);
	wl_list_remove(&cursor_frame.link);
	wl_list_remove(&cursor_motion.link);
	wl_list_remove(&cursor_motion_absolute.link);
	wl_list_remove(&gpu_reset.link);
	wl_list_remove(&new_idle_inhibitor.link);
	wl_list_remove(&layout_change.link);
	wl_list_remove(&new_input_device.link);
	wl_list_remove(&new_virtual_keyboard.link);
	wl_list_remove(&new_virtual_pointer.link);
	wl_list_remove(&new_pointer_constraint.link);
	wl_list_remove(&new_output.link);
	wl_list_remove(&new_xdg_toplevel.link);
	wl_list_remove(&new_xdg_decoration.link);
	wl_list_remove(&new_xdg_popup.link);
	wl_list_remove(&new_layer_surface.link);
	wl_list_remove(&output_mgr_apply.link);
	wl_list_remove(&output_mgr_test.link);
	wl_list_remove(&output_power_mgr_set_mode.link);
	wl_list_remove(&request_activate.link);
	wl_list_remove(&request_cursor.link);
	wl_list_remove(&request_set_psel.link);
	wl_list_remove(&request_set_sel.link);
	wl_list_remove(&request_set_cursor_shape.link);
	wl_list_remove(&request_start_drag.link);
	wl_list_remove(&start_drag.link);
	wl_list_remove(&new_session_lock.link);
#ifdef XWAYLAND
	wl_list_remove(&new_xwayland_surface.link);
	wl_list_remove(&xwayland_ready.link);
#endif
}

void
closemon(Monitor *m)
{
	Client *c;
	int i = 0, nmons = wl_list_length(&mons);
	if (!nmons) {
		selmon = NULL;
	} else if (m == selmon) {
		do
			selmon = wl_container_of(mons.next, selmon, link);
		while (!selmon->wlr_output->enabled && i++ < nmons);

		if (!selmon->wlr_output->enabled)
			selmon = NULL;
	}

	wl_list_for_each(c, &clients, link) {
		if (c->isfloating && c->geom.x > m->m.width)
			resize(c, (struct wlr_box){.x = c->geom.x - m->w.width, .y = c->geom.y,
					.width = c->geom.width, .height = c->geom.height}, 0);
		if (c->mon == m)
			setmon(c, selmon, c->tags);
	}
	focusclient(focustop(selmon), 1);
	printstatus();
}

void
commitlayersurfacenotify(struct wl_listener *listener, void *data)
{
	LayerSurface *l = wl_container_of(listener, l, surface_commit);
	struct wlr_layer_surface_v1 *layer_surface = l->layer_surface;
	struct wlr_scene_tree *scene_layer = layers[layermap[layer_surface->current.layer]];
	struct wlr_layer_surface_v1_state old_state;

	if (l->layer_surface->initial_commit) {
		client_set_scale(layer_surface->surface, l->mon->wlr_output->scale);

		old_state = l->layer_surface->current;
		l->layer_surface->current = l->layer_surface->pending;
		arrangelayers(l->mon);
		l->layer_surface->current = old_state;
		return;
	}

	if (layer_surface->current.committed == 0 && l->mapped == layer_surface->surface->mapped)
		return;
	l->mapped = layer_surface->surface->mapped;

	if (scene_layer != l->scene->node.parent) {
		wlr_scene_node_reparent(&l->scene->node, scene_layer);
		wl_list_remove(&l->link);
		wl_list_insert(&l->mon->layers[layer_surface->current.layer], &l->link);
		wlr_scene_node_reparent(&l->popups->node, (layer_surface->current.layer
				< ZWLR_LAYER_SHELL_V1_LAYER_TOP ? layers[LyrTop] : scene_layer));
	}

	arrangelayers(l->mon);
}

void
commitnotify(struct wl_listener *listener, void *data)
{
	Client *c = wl_container_of(listener, c, commit);

	if (c->surface.xdg->initial_commit) {
		applyrules(c);
		if (c->mon) {
			client_set_scale(client_surface(c), c->mon->wlr_output->scale);
		}
		setmon(c, NULL, 0);

		wlr_xdg_toplevel_set_wm_capabilities(c->surface.xdg->toplevel,
				WLR_XDG_TOPLEVEL_WM_CAPABILITIES_FULLSCREEN);
		if (c->decoration)
			requestdecorationmode(&c->set_decoration_mode, c->decoration);
		wlr_xdg_toplevel_set_size(c->surface.xdg->toplevel, 0, 0);
		return;
	}

	resize(c, c->geom, (c->isfloating && !c->isfullscreen));

	if (c->resize && c->resize <= c->surface.xdg->current.configure_serial)
		c->resize = 0;

	if (c->mon && fabs(c->mon->zoom_scale - 1.0) >= 1e-9)
		wlr_scene_node_for_each_buffer(&c->scene->node, zoom_scale_buffers, c);
}

void
commitpopup(struct wl_listener *listener, void *data)
{
	struct wlr_surface *surface = data;
	struct wlr_xdg_popup *popup = wlr_xdg_popup_try_from_wlr_surface(surface);
	LayerSurface *l = NULL;
	Client *c = NULL;
	struct wlr_box box;
	int type = -1;

	if (!popup->base->initial_commit)
		return;

	type = toplevel_from_wlr_surface(popup->base->surface, &c, &l);
	if (!popup->parent || type < 0)
		return;
	popup->base->surface->data = wlr_scene_xdg_surface_create(
			popup->parent->data, popup->base);
	if ((l && !l->mon) || (c && !c->mon)) {
		wlr_xdg_popup_destroy(popup);
		return;
	}
	box = type == LayerShell ? l->mon->m : c->mon->w;
	box.x -= (type == LayerShell ? l->scene->node.x : c->geom.x);
	box.y -= (type == LayerShell ? l->scene->node.y : c->geom.y);
	if (type != LayerShell && c->mon && fabs(c->mon->zoom_scale - 1.0) >= 1e-9) {
		Monitor *_m = c->mon;
		double inv_s = 1.0 / _m->zoom_scale;
		box.x      = (int)llround(-_m->zoom_pan_x * inv_s) - c->geom.x;
		box.y      = (int)llround(-_m->zoom_pan_y * inv_s) - c->geom.y;
		box.width  = MAX(1, (int)llround((double)_m->m.width  * inv_s));
		box.height = MAX(1, (int)llround((double)_m->m.height * inv_s));
	}
	wlr_xdg_popup_unconstrain_from_box(popup, &box);
	if (c && c->mon) {
		struct wlr_scene_tree *ptree = popup->parent->data;
		struct wlr_scene_node *pnode;
		PopupZoomData *pzd;
		pnode = wl_container_of(ptree->children.prev, pnode, link);
		pzd = ecalloc(1, sizeof(*pzd));
		pzd->client = c;
		pzd->popup  = popup;
		pzd->node   = pnode;
		pzd->commit.notify  = popup_zoom_commit;
		pzd->destroy.notify = popup_zoom_destroy;
		wl_list_insert(&popup_zoom_list, &pzd->link);
		wl_signal_add(&popup->base->surface->events.commit, &pzd->commit);
		wl_signal_add(&popup->base->events.destroy, &pzd->destroy);
		popup_zoom_commit(&pzd->commit, NULL);
	}
	wl_list_remove(&listener->link);
	free(listener);
}

void
createdecoration(struct wl_listener *listener, void *data)
{
	struct wlr_xdg_toplevel_decoration_v1 *deco = data;
	Client *c = deco->toplevel->base->data;
	c->decoration = deco;

	LISTEN(&deco->events.request_mode, &c->set_decoration_mode, requestdecorationmode);
	LISTEN(&deco->events.destroy, &c->destroy_decoration, destroydecoration);

	requestdecorationmode(&c->set_decoration_mode, deco);
}

void
createidleinhibitor(struct wl_listener *listener, void *data)
{
	struct wlr_idle_inhibitor_v1 *idle_inhibitor = data;
	LISTEN_STATIC(&idle_inhibitor->events.destroy, destroyidleinhibitor);

	checkidleinhibitor(NULL);
}

void
createkeyboard(struct wlr_keyboard *keyboard)
{
	wlr_keyboard_set_keymap(keyboard, kb_group->wlr_group->keyboard.keymap);

	wlr_keyboard_group_add_keyboard(kb_group->wlr_group, keyboard);
}

KeyboardGroup *
createkeyboardgroup(void)
{
	KeyboardGroup *group = ecalloc(1, sizeof(*group));
	struct xkb_context *context;
	struct xkb_keymap *keymap;

	group->wlr_group = wlr_keyboard_group_create();
	group->wlr_group->data = group;

	context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
	if (!(keymap = xkb_keymap_new_from_names(context, &xkb_rules,
				XKB_KEYMAP_COMPILE_NO_FLAGS)))
		die("failed to compile keymap");

	wlr_keyboard_set_keymap(&group->wlr_group->keyboard, keymap);
	xkb_keymap_unref(keymap);
	xkb_context_unref(context);

	wlr_keyboard_set_repeat_info(&group->wlr_group->keyboard, repeat_rate, repeat_delay);

	LISTEN(&group->wlr_group->keyboard.events.key, &group->key, keypress);
	LISTEN(&group->wlr_group->keyboard.events.modifiers, &group->modifiers, keypressmod);

	group->key_repeat_source = wl_event_loop_add_timer(event_loop, keyrepeat, group);

	wlr_seat_set_keyboard(seat, &group->wlr_group->keyboard);
	return group;
}

void
createlayersurface(struct wl_listener *listener, void *data)
{
	struct wlr_layer_surface_v1 *layer_surface = data;
	LayerSurface *l;
	struct wlr_surface *surface = layer_surface->surface;
	struct wlr_scene_tree *scene_layer = layers[layermap[layer_surface->pending.layer]];

	if (!layer_surface->output
			&& !(layer_surface->output = selmon ? selmon->wlr_output : NULL)) {
		wlr_layer_surface_v1_destroy(layer_surface);
		return;
	}

	l = layer_surface->data = ecalloc(1, sizeof(*l));
	l->type = LayerShell;
	LISTEN(&surface->events.commit, &l->surface_commit, commitlayersurfacenotify);
	LISTEN(&surface->events.unmap, &l->unmap, unmaplayersurfacenotify);
	LISTEN(&layer_surface->events.destroy, &l->destroy, destroylayersurfacenotify);

	l->layer_surface = layer_surface;
	l->mon = layer_surface->output->data;
	l->scene_layer = wlr_scene_layer_surface_v1_create(scene_layer, layer_surface);
	l->scene = l->scene_layer->tree;
	l->popups = surface->data = wlr_scene_tree_create(layer_surface->current.layer
			< ZWLR_LAYER_SHELL_V1_LAYER_TOP ? layers[LyrTop] : scene_layer);
	l->scene->node.data = l->popups->node.data = l;

	wl_list_insert(&l->mon->layers[layer_surface->pending.layer],&l->link);
	wlr_surface_send_enter(surface, layer_surface->output);
}

void
createlocksurface(struct wl_listener *listener, void *data)
{
	SessionLock *lock = wl_container_of(listener, lock, new_surface);
	struct wlr_session_lock_surface_v1 *lock_surface = data;
	Monitor *m = lock_surface->output->data;
	struct wlr_scene_tree *scene_tree = lock_surface->surface->data
			= wlr_scene_subsurface_tree_create(lock->scene, lock_surface->surface);
	m->lock_surface = lock_surface;

	wlr_scene_node_set_position(&scene_tree->node, m->m.x, m->m.y);
	wlr_session_lock_surface_v1_configure(lock_surface, m->m.width, m->m.height);

	LISTEN(&lock_surface->events.destroy, &m->destroy_lock_surface, destroylocksurface);

	if (m == selmon)
		client_notify_enter(lock_surface->surface, wlr_seat_get_keyboard(seat));
}

void
createmon(struct wl_listener *listener, void *data)
{
	struct wlr_output *wlr_output = data;
	struct wlr_output_mode *mode = wl_container_of(wlr_output->modes.next, mode, link);
	const MonitorRule *r;
	size_t i;
	struct wlr_output_state state;
	Monitor *m;

	if (!wlr_output_init_render(wlr_output, alloc, drw))
		return;

	m = wlr_output->data = ecalloc(1, sizeof(*m));
	m->wlr_output = wlr_output;

	for (i = 0; i < LENGTH(m->layers); i++)
		wl_list_init(&m->layers[i]);

	wlr_output_state_init(&state);
	m->tagset[0] = m->tagset[1] = 1;
	for (r = monrules; r < END(monrules); r++) {
		if (!r->name || strstr(wlr_output->name, r->name)) {
			m->m.x = r->x;
			m->m.y = r->y;
			wlr_output_state_set_scale(&state, r->scale);
			wlr_output_state_set_transform(&state, r->rr);

			wlr_output_state_set_adaptive_sync_enabled(&state, r->adaptive);

			if(r->mode == -1)
				wlr_output_state_set_custom_mode(&state, r->resx, r->resy,
				(int) (r->rate > 0 ? r->rate * 1000 : 0));
			else if (!wl_list_empty(&wlr_output->modes)) {
				for (int j = 0; j < r->mode; j++) {
					mode = wl_container_of(mode->link.next, mode, link);
				}
				wlr_output_state_set_mode(&state, mode);
			}

			break;
		}
	}

	{
		int restored = 0;
		MonitorState *ms = NULL, *iter;
		unsigned int _i;
		wl_list_for_each(iter, &savedstates, link) {
			if (strcmp(iter->name, wlr_output->name) == 0) {
				ms = iter;
				break;
			}
		}
		if (ms) {
			m->seltags = ms->seltags;
			m->tagset[0] = ms->tagset[0];
			m->tagset[1] = ms->tagset[1];
			m->canvas = ms->canvas;
			ms->canvas = NULL;
			m->zoom_scale = ms->zoom_scale;
			m->zoom_target = ms->zoom_target;
			m->zoom_pan_x = ms->zoom_pan_x;
			m->zoom_pan_y = ms->zoom_pan_y;
			wl_list_remove(&ms->link);
			free(ms);
			restored = 1;
		} else {
			m->canvas = ecalloc(TAGCOUNT, sizeof(CanvasOffset));
			for (_i = 0; _i < (unsigned int)TAGCOUNT; _i++) {
				m->canvas[_i].cx = 0;
				m->canvas[_i].cy = 0;
			}
		}
		if (!restored) {
			m->zoom_scale = 1.0;
			m->zoom_target = 1.0;
			m->zoom_pan_x = 0.0;
			m->zoom_pan_y = 0.0;
		}
		m->zoom_anim_t = 1.0;
		m->zoom_start_scale = 1.0;
		m->zoom_start_pan_x = 0.0;
		m->zoom_start_pan_y = 0.0;
		m->zoom_popups_dirty = 0;
	}
	wlr_output_state_set_mode(&state, wlr_output_preferred_mode(wlr_output));

	LISTEN(&wlr_output->events.frame, &m->frame, rendermon);
	LISTEN(&wlr_output->events.destroy, &m->destroy, cleanupmon);
	LISTEN(&wlr_output->events.request_state, &m->request_state, requestmonstate);

	wlr_output_state_set_enabled(&state, 1);
	wlr_output_commit_state(wlr_output, &state);
	wlr_output_state_finish(&state);

	wl_list_insert(&mons, &m->link);
	printstatus();

	m->fullscreen_bg = wlr_scene_rect_create(layers[LyrFS], 0, 0, fullscreen_bg);
	wlr_scene_node_set_enabled(&m->fullscreen_bg->node, 0);

	wallpaper_mon_create(m);
	bar_mon_create(m);
	m->scene_output = wlr_scene_output_create(scene, wlr_output);
	if (m->m.x == -1 && m->m.y == -1)
		wlr_output_layout_add_auto(output_layout, wlr_output);
	else
		wlr_output_layout_add(output_layout, wlr_output, m->m.x, m->m.y);
}

void
createnotify(struct wl_listener *listener, void *data)
{
	struct wlr_xdg_toplevel *toplevel = data;
	Client *c = NULL;

	c = toplevel->base->data = ecalloc(1, sizeof(*c));
	c->surface.xdg = toplevel->base;

	c->ibw = innerborderpx;
	c->obw = outerborderpx;
	c->bw = c->ibw + c->obw;

	LISTEN(&toplevel->base->surface->events.commit, &c->commit, commitnotify);
	LISTEN(&toplevel->base->surface->events.map, &c->map, mapnotify);
	LISTEN(&toplevel->base->surface->events.unmap, &c->unmap, unmapnotify);
	LISTEN(&toplevel->events.destroy, &c->destroy, destroynotify);
	LISTEN(&toplevel->events.request_fullscreen, &c->fullscreen, fullscreennotify);
	LISTEN(&toplevel->events.request_maximize, &c->maximize, maximizenotify);
	LISTEN(&toplevel->events.set_title, &c->set_title, updatetitle);
}

typedef struct {
	struct wl_listener pinch_begin;
	struct wl_listener pinch_update;
	struct wl_listener pinch_end;
	struct wl_listener destroy;
	struct wlr_pointer *pointer;
} PinchListeners;

static void
pinchbegin(struct wl_listener *listener, void *data)
{
	struct wlr_pointer_pinch_begin_event *event = data;
	(void)listener;
	if (!pointer_gestures || !event)
		return;
	wlr_pointer_gestures_v1_send_pinch_begin(pointer_gestures, seat,
		event->time_msec, event->fingers);
}

static void
pinchupdate(struct wl_listener *listener, void *data)
{
	struct wlr_pointer_pinch_update_event *event = data;
	(void)listener;
	if (!pointer_gestures || !event)
		return;
	wlr_pointer_gestures_v1_send_pinch_update(pointer_gestures, seat,
		event->time_msec, event->dx, event->dy,
		event->scale, event->rotation);
}

static void
pinchend(struct wl_listener *listener, void *data)
{
	struct wlr_pointer_pinch_end_event *event = data;
	(void)listener;
	if (!pointer_gestures || !event)
		return;
	wlr_pointer_gestures_v1_send_pinch_end(pointer_gestures, seat,
		event->time_msec, event->cancelled);
}

static void
pinchdestroy(struct wl_listener *listener, void *data)
{
	PinchListeners *pl = wl_container_of(listener, pl, destroy);
	wl_list_remove(&pl->pinch_begin.link);
	wl_list_remove(&pl->pinch_update.link);
	wl_list_remove(&pl->pinch_end.link);
	wl_list_remove(&pl->destroy.link);
	free(pl);
}

void
createpointer(struct wlr_pointer *pointer)
{
	struct libinput_device *device;
	PinchListeners *pl;
	if (wlr_input_device_is_libinput(&pointer->base)
			&& (device = wlr_libinput_get_device_handle(&pointer->base))) {

		if (libinput_device_config_tap_get_finger_count(device)) {
			libinput_device_config_tap_set_enabled(device, tap_to_click);
			libinput_device_config_tap_set_drag_enabled(device, tap_and_drag);
			libinput_device_config_tap_set_drag_lock_enabled(device, drag_lock);
			libinput_device_config_tap_set_button_map(device, button_map);
		}

		if (libinput_device_config_scroll_has_natural_scroll(device))
			libinput_device_config_scroll_set_natural_scroll_enabled(device, natural_scrolling);

		if (libinput_device_config_dwt_is_available(device))
			libinput_device_config_dwt_set_enabled(device, disable_while_typing);

		if (libinput_device_config_left_handed_is_available(device))
			libinput_device_config_left_handed_set(device, left_handed);

		if (libinput_device_config_middle_emulation_is_available(device))
			libinput_device_config_middle_emulation_set_enabled(device, middle_button_emulation);

		if (libinput_device_config_scroll_get_methods(device) != LIBINPUT_CONFIG_SCROLL_NO_SCROLL)
			libinput_device_config_scroll_set_method(device, scroll_method);

		if (libinput_device_config_click_get_methods(device) != LIBINPUT_CONFIG_CLICK_METHOD_NONE)
			libinput_device_config_click_set_method(device, click_method);

		if (libinput_device_config_send_events_get_modes(device))
			libinput_device_config_send_events_set_mode(device, send_events_mode);

		if (libinput_device_config_accel_is_available(device)) {
			libinput_device_config_accel_set_profile(device, accel_profile);
			libinput_device_config_accel_set_speed(device, accel_speed);
		}
	}

	wlr_cursor_attach_input_device(cursor, &pointer->base);

	pl = ecalloc(1, sizeof(*pl));
	pl->pointer = pointer;
	pl->pinch_begin.notify = pinchbegin;
	pl->pinch_update.notify = pinchupdate;
	pl->pinch_end.notify = pinchend;
	pl->destroy.notify = pinchdestroy;
	wl_signal_add(&pointer->events.pinch_begin, &pl->pinch_begin);
	wl_signal_add(&pointer->events.pinch_update, &pl->pinch_update);
	wl_signal_add(&pointer->events.pinch_end, &pl->pinch_end);
	wl_signal_add(&pointer->base.events.destroy, &pl->destroy);
}

void
createpointerconstraint(struct wl_listener *listener, void *data)
{
	PointerConstraint *pointer_constraint = ecalloc(1, sizeof(*pointer_constraint));
	pointer_constraint->constraint = data;
	LISTEN(&pointer_constraint->constraint->events.destroy,
			&pointer_constraint->destroy, destroypointerconstraint);
}

void
createpopup(struct wl_listener *listener, void *data)
{
	struct wlr_xdg_popup *popup = data;
	LISTEN_STATIC(&popup->base->surface->events.commit, commitpopup);
}

void
cursorconstrain(struct wlr_pointer_constraint_v1 *constraint)
{
	if (active_constraint == constraint)
		return;

	if (active_constraint)
		wlr_pointer_constraint_v1_send_deactivated(active_constraint);

	active_constraint = constraint;
	wlr_pointer_constraint_v1_send_activated(constraint);
}

void
cursorframe(struct wl_listener *listener, void *data)
{
	wlr_seat_pointer_notify_frame(seat);
}

void
cursorwarptohint(void)
{
	Client *c = NULL;
	double sx = active_constraint->current.cursor_hint.x;
	double sy = active_constraint->current.cursor_hint.y;

	toplevel_from_wlr_surface(active_constraint->surface, &c, NULL);
	if (c && active_constraint->current.cursor_hint.enabled) {
		wlr_cursor_warp(cursor, NULL, sx + c->geom.x + c->bw, sy + c->geom.y + c->bw);
		wlr_seat_pointer_warp(active_constraint->seat, sx, sy);
	}
}

void
destroydecoration(struct wl_listener *listener, void *data)
{
	Client *c = wl_container_of(listener, c, destroy_decoration);

	wl_list_remove(&c->destroy_decoration.link);
	wl_list_remove(&c->set_decoration_mode.link);
}

void
destroydragicon(struct wl_listener *listener, void *data)
{
	focusclient(focustop(selmon), 1);
	motionnotify(0, NULL, 0, 0, 0, 0);
	wl_list_remove(&listener->link);
	free(listener);
}

void
destroyidleinhibitor(struct wl_listener *listener, void *data)
{
	checkidleinhibitor(wlr_surface_get_root_surface(data));
	wl_list_remove(&listener->link);
	free(listener);
}

void
destroylayersurfacenotify(struct wl_listener *listener, void *data)
{
	LayerSurface *l = wl_container_of(listener, l, destroy);

	wl_list_remove(&l->link);
	wl_list_remove(&l->destroy.link);
	wl_list_remove(&l->unmap.link);
	wl_list_remove(&l->surface_commit.link);
	wlr_scene_node_destroy(&l->scene->node);
	wlr_scene_node_destroy(&l->popups->node);
	free(l);
}

void
destroylock(SessionLock *lock, int unlock)
{
	wlr_seat_keyboard_notify_clear_focus(seat);
	if ((locked = !unlock))
		goto destroy;

	wlr_scene_node_set_enabled(&locked_bg->node, 0);

	focusclient(focustop(selmon), 0);
	motionnotify(0, NULL, 0, 0, 0, 0);

destroy:
	wl_list_remove(&lock->new_surface.link);
	wl_list_remove(&lock->unlock.link);
	wl_list_remove(&lock->destroy.link);

	wlr_scene_node_destroy(&lock->scene->node);
	cur_lock = NULL;
	free(lock);
}

void
destroylocksurface(struct wl_listener *listener, void *data)
{
	Monitor *m = wl_container_of(listener, m, destroy_lock_surface);
	struct wlr_session_lock_surface_v1 *surface, *lock_surface = m->lock_surface;

	m->lock_surface = NULL;
	wl_list_remove(&m->destroy_lock_surface.link);

	if (lock_surface->surface != seat->keyboard_state.focused_surface)
		return;

	if (locked && cur_lock && !wl_list_empty(&cur_lock->surfaces)) {
		surface = wl_container_of(cur_lock->surfaces.next, surface, link);
		client_notify_enter(surface->surface, wlr_seat_get_keyboard(seat));
	} else if (!locked) {
		focusclient(focustop(selmon), 1);
	} else {
		wlr_seat_keyboard_clear_focus(seat);
	}
}

void
destroynotify(struct wl_listener *listener, void *data)
{
	Client *c = wl_container_of(listener, c, destroy);
	wl_list_remove(&c->destroy.link);
	wl_list_remove(&c->set_title.link);
	wl_list_remove(&c->fullscreen.link);
#ifdef XWAYLAND
	if (c->type != XDGShell) {
		wl_list_remove(&c->activate.link);
		wl_list_remove(&c->associate.link);
		wl_list_remove(&c->configure.link);
		wl_list_remove(&c->dissociate.link);
		wl_list_remove(&c->set_hints.link);
	} else
#endif
	{
		wl_list_remove(&c->commit.link);
		wl_list_remove(&c->map.link);
		wl_list_remove(&c->unmap.link);
		wl_list_remove(&c->maximize.link);
	}
	free(c);
}

void
destroypointerconstraint(struct wl_listener *listener, void *data)
{
	PointerConstraint *pointer_constraint = wl_container_of(listener, pointer_constraint, destroy);

	if (active_constraint == pointer_constraint->constraint) {
		cursorwarptohint();
		active_constraint = NULL;
	}

	wl_list_remove(&pointer_constraint->destroy.link);
	free(pointer_constraint);
}

void
destroysessionlock(struct wl_listener *listener, void *data)
{
	SessionLock *lock = wl_container_of(listener, lock, destroy);
	destroylock(lock, 0);
}

void
destroykeyboardgroup(struct wl_listener *listener, void *data)
{
	KeyboardGroup *group = wl_container_of(listener, group, destroy);
	wl_event_source_remove(group->key_repeat_source);
	wl_list_remove(&group->key.link);
	wl_list_remove(&group->modifiers.link);
	wl_list_remove(&group->destroy.link);
	wlr_keyboard_group_destroy(group->wlr_group);
	free(group);
}

Monitor *
dirtomon(enum wlr_direction dir)
{
	struct wlr_output *next;
	if (!wlr_output_layout_get(output_layout, selmon->wlr_output))
		return selmon;
	if ((next = wlr_output_layout_adjacent_output(output_layout,
			dir, selmon->wlr_output, selmon->m.x, selmon->m.y)))
		return next->data;
	if ((next = wlr_output_layout_farthest_output(output_layout,
			dir ^ (WLR_DIRECTION_LEFT|WLR_DIRECTION_RIGHT),
			selmon->wlr_output, selmon->m.x, selmon->m.y)))
		return next->data;
	return selmon;
}

void
focusclient(Client *c, int lift)
{
	struct wlr_surface *old = seat->keyboard_state.focused_surface;
	int unused_lx, unused_ly, old_client_type;
	Client *old_c = NULL;
	LayerSurface *old_l = NULL;

	if (locked)
		return;

	if (c && lift)
		wlr_scene_node_raise_to_top(&c->scene->node);

	if (c && client_surface(c) == old)
		return;

	if ((old_client_type = toplevel_from_wlr_surface(old, &old_c, &old_l)) == XDGShell) {
		struct wlr_xdg_popup *popup, *tmp;
		wl_list_for_each_safe(popup, tmp, &old_c->surface.xdg->popups, link)
			wlr_xdg_popup_destroy(popup);
	}

	if (c && !client_is_unmanaged(c)) {
		wl_list_remove(&c->flink);
		wl_list_insert(&fstack, &c->flink);
		selmon = c->mon;
		c->isurgent = 0;

		if (!exclusive_focus && !seat->drag)
			client_set_border_color(c, focused_inner_border_color, focused_outer_border_color);
	}

	if (old && (!c || client_surface(c) != old)) {
		if (old_client_type == LayerShell && wlr_scene_node_coords(
					&old_l->scene->node, &unused_lx, &unused_ly)
				&& old_l->layer_surface->current.layer >= ZWLR_LAYER_SHELL_V1_LAYER_TOP) {
			return;
		} else if (old_c && old_c == exclusive_focus && client_wants_focus(old_c)) {
			return;
		} else if (old_c && !client_is_unmanaged(old_c) && (!c || !client_wants_focus(c))) {
			client_set_border_color(old_c, unfocused_inner_border_color, unfocused_outer_border_color);

			client_activate_surface(old, 0);
		}
	}
	printstatus();

	if (!c) {
		wlr_seat_keyboard_notify_clear_focus(seat);
		return;
	}

	motionnotify(0, NULL, 0, 0, 0, 0);

	client_notify_enter(client_surface(c), wlr_seat_get_keyboard(seat));

	client_activate_surface(client_surface(c), 1);
}

void
focusmon(const Arg *arg)
{
	int i = 0, nmons = wl_list_length(&mons);
	if (nmons) {
		do
			selmon = dirtomon(arg->i);
		while (!selmon->wlr_output->enabled && i++ < nmons);
	}
	focusclient(focustop(selmon), 1);
}

void
focusstack(const Arg *arg)
{
	Client *c, *sel = focustop(selmon);
	if (!sel || (sel->isfullscreen && !client_has_children(sel)))
		return;
	if (arg->i > 0) {
		wl_list_for_each(c, &sel->link, link) {
			if (&c->link == &clients)
				continue;
			if (VISIBLEON(c, selmon))
				break;
		}
	} else {
		wl_list_for_each_reverse(c, &sel->link, link) {
			if (&c->link == &clients)
				continue;
			if (VISIBLEON(c, selmon))
				break;
		}
	}
	focusclient(c, 1);
	if (selmon)
		bringintoview(c);
}

void
focusstackcenter(const Arg *arg)
{
	Monitor *m;
	Client *c;
	int dx, dy;
	double s, scr_cx, scr_cy;

	focusstack(arg);

	m = selmon;
	if (!m)
		return;
	if (!(c = focustop(m)) || c->isfullscreen)
		return;

	s = m->zoom_scale;
	if (fabs(s) < 1e-9)
		s = 1.0;

	scr_cx = ((m->w.x + m->w.width  / 2.0) - m->zoom_pan_x) / s;
	scr_cy = ((m->w.y + m->w.height / 2.0) - m->zoom_pan_y) / s;

	dx = (int)llround(scr_cx - (c->geom.x + c->geom.width  / 2.0));
	dy = (int)llround(scr_cy - (c->geom.y + c->geom.height / 2.0));
	if (dx == 0 && dy == 0)
		return;

	canvas_shift(m, dx, dy, animatecanvas);
}

void
focusdir(const Arg *arg)
{
	Monitor *m;
	Client *c, *sel, *best = NULL;
	double scx, scy, dx, dy, prim, perp, score, bestscore = 0;
	double s, scr_cx, scr_cy;
	int cdx, cdy;

	sel = focustop(selmon);
	if (!sel || (sel->isfullscreen && !client_has_children(sel)))
		return;
	scx = sel->geom.x + sel->geom.width / 2.0;
	scy = sel->geom.y + sel->geom.height / 2.0;

	wl_list_for_each(c, &clients, link) {
		if (c == sel || !VISIBLEON(c, selmon))
			continue;
		dx = c->geom.x + c->geom.width / 2.0 - scx;
		dy = c->geom.y + c->geom.height / 2.0 - scy;

		switch (arg->ui) {
		case 0: prim = -dy; perp = fabs(dx); break;
		case 1: prim = -dx; perp = fabs(dy); break;
		case 2: prim =  dy; perp = fabs(dx); break;
		case 3: prim =  dx; perp = fabs(dy); break;
		default: return;
		}
		if (prim <= 0)
			continue;

		score = prim + 2 * perp;
		if (!best || score < bestscore) {
			best = c;
			bestscore = score;
		}
	}
	if (!best)
		return;

	focusclient(best, 1);

	m = selmon;
	if (!m)
		return;
	if (!(c = focustop(m)) || c->isfullscreen)
		return;

	s = m->zoom_scale;
	if (fabs(s) < 1e-9)
		s = 1.0;

	scr_cx = ((m->w.x + m->w.width  / 2.0) - m->zoom_pan_x) / s;
	scr_cy = ((m->w.y + m->w.height / 2.0) - m->zoom_pan_y) / s;

	cdx = (int)llround(scr_cx - (c->geom.x + c->geom.width  / 2.0));
	cdy = (int)llround(scr_cy - (c->geom.y + c->geom.height / 2.0));
	if (cdx == 0 && cdy == 0)
		return;

	canvas_shift(m, cdx, cdy, animatecanvas);
}

Client *
focustop(Monitor *m)
{
	Client *c;
	wl_list_for_each(c, &fstack, flink) {
		if (VISIBLEON(c, m))
			return c;
	}
	return NULL;
}

void
fullscreennotify(struct wl_listener *listener, void *data)
{
	Client *c = wl_container_of(listener, c, fullscreen);
	setfullscreen(c, client_wants_fullscreen(c));
}

void
gpureset(struct wl_listener *listener, void *data)
{
	struct wlr_renderer *old_drw = drw;
	struct wlr_allocator *old_alloc = alloc;
	struct Monitor *m;
	if (!(drw = wlr_renderer_autocreate(backend)))
		die("couldn't recreate renderer");

	if (!(alloc = wlr_allocator_autocreate(backend, drw)))
		die("couldn't recreate allocator");

	wl_list_remove(&gpu_reset.link);
	wl_signal_add(&drw->events.lost, &gpu_reset);

	wlr_compositor_set_renderer(compositor, drw);

	wl_list_for_each(m, &mons, link) {
		wlr_output_init_render(m->wlr_output, alloc, drw);
	}

	wlr_allocator_destroy(old_alloc);
	wlr_renderer_destroy(old_drw);
}

void
handlesig(int signo)
{
	if (signo == SIGCHLD)
		while (waitpid(-1, NULL, WNOHANG) > 0);
	else if (signo == SIGINT || signo == SIGTERM)
		quit(NULL);
}

void
inputdevice(struct wl_listener *listener, void *data)
{
	struct wlr_input_device *device = data;
	struct wlr_tablet_pad *pad = NULL;
	uint32_t caps;

	switch (device->type) {
	case WLR_INPUT_DEVICE_KEYBOARD:
		createkeyboard(wlr_keyboard_from_input_device(device));
		break;
	case WLR_INPUT_DEVICE_POINTER:
		createpointer(wlr_pointer_from_input_device(device));
		break;
		case WLR_INPUT_DEVICE_TABLET:
		createtablet(device);
		break;
	case WLR_INPUT_DEVICE_TABLET_PAD:
		pad = wlr_tablet_pad_from_input_device(device);
		tablet_pad = wlr_tablet_pad_create(tablet_mgr, seat, device);
		wl_signal_add(&pad->events.button,  &tablet_pad_button);
		wl_signal_add(&device->events.destroy, &tablet_pad_destroy);
		break;
	default:
		break;
	}

	caps = WL_SEAT_CAPABILITY_POINTER;
	if (!wl_list_empty(&kb_group->wlr_group->devices))
		caps |= WL_SEAT_CAPABILITY_KEYBOARD;
	wlr_seat_set_capabilities(seat, caps);
}

int
keybinding(uint32_t mods, xkb_keysym_t sym)
{
	const Key *k;
	for (k = keys; k < END(keys); k++) {
		if (CLEANMASK(mods) == CLEANMASK(k->mod)
				&& xkb_keysym_to_lower(sym) == xkb_keysym_to_lower(k->keysym)
				&& k->func) {
			k->func(&k->arg);
			return 1;
		}
	}
	return 0;
}

void
keypress(struct wl_listener *listener, void *data)
{
	int i;
	KeyboardGroup *group = wl_container_of(listener, group, key);
	struct wlr_keyboard_key_event *event = data;

	uint32_t keycode = event->keycode + 8;
	const xkb_keysym_t *syms;
	int nsyms = xkb_state_key_get_syms(
			group->wlr_group->keyboard.xkb_state, keycode, &syms);

			{
				struct xkb_keymap *km = group->wlr_group->keyboard.keymap;
				struct xkb_state *st = group->wlr_group->keyboard.xkb_state;
				xkb_layout_index_t nl = xkb_keymap_num_layouts_for_key(km, keycode), l;
				int n = 0, j, k;

				for (j = 0; j < nsyms && n < (int)LENGTH(group->symbuf); j++)
					group->symbuf[n++] = syms[j];
				for (l = 0; l < nl; l++) {
					const xkb_keysym_t *ls;
					xkb_level_index_t lvl = xkb_state_key_get_level(st, keycode, l);
					int nls = xkb_keymap_key_get_syms_by_level(km, keycode, l, lvl, &ls);
					for (j = 0; j < nls; j++) {
						for (k = 0; k < n; k++)
							if (group->symbuf[k] == ls[j])
								break;
						if (k == n && n < (int)LENGTH(group->symbuf))
							group->symbuf[n++] = ls[j];
					}
				}
				syms = group->symbuf;
				nsyms = n;
			}

	int handled = 0;
	uint32_t mods = wlr_keyboard_get_modifiers(&group->wlr_group->keyboard);

	wlr_idle_notifier_v1_notify_activity(idle_notifier, seat);

	if (!locked && event->state == WL_KEYBOARD_KEY_STATE_PRESSED) {
		for (i = 0; i < nsyms; i++)
			handled = keybinding(mods, syms[i]) || handled;
	}

	if (handled && group->wlr_group->keyboard.repeat_info.delay > 0) {
		group->mods = mods;
		group->keysyms = syms;
		group->nsyms = nsyms;
		wl_event_source_timer_update(group->key_repeat_source,
				group->wlr_group->keyboard.repeat_info.delay);
	} else {
		group->nsyms = 0;
		wl_event_source_timer_update(group->key_repeat_source, 0);
	}

	if (handled)
		return;

	wlr_seat_set_keyboard(seat, &group->wlr_group->keyboard);
	wlr_seat_keyboard_notify_key(seat, event->time_msec,
			event->keycode, event->state);
}

void
keypressmod(struct wl_listener *listener, void *data)
{
	KeyboardGroup *group = wl_container_of(listener, group, modifiers);

	wlr_seat_set_keyboard(seat, &group->wlr_group->keyboard);
	wlr_seat_keyboard_notify_modifiers(seat,
			&group->wlr_group->keyboard.modifiers);
	bar_layout_update(&group->wlr_group->keyboard);
}

int
keyrepeat(void *data)
{
	KeyboardGroup *group = data;
	int i;
	if (!group->nsyms || group->wlr_group->keyboard.repeat_info.rate <= 0)
		return 0;

	wl_event_source_timer_update(group->key_repeat_source,
			1000 / group->wlr_group->keyboard.repeat_info.rate);

	for (i = 0; i < group->nsyms; i++)
		keybinding(group->mods, group->keysyms[i]);

	return 0;
}

void
killclient(const Arg *arg)
{
	Client *sel = focustop(selmon);
	if (sel)
		client_send_close(sel);
}

void
locksession(struct wl_listener *listener, void *data)
{
	struct wlr_session_lock_v1 *session_lock = data;
	SessionLock *lock;
	wlr_scene_node_set_enabled(&locked_bg->node, 1);
	if (cur_lock) {
		wlr_session_lock_v1_destroy(session_lock);
		return;
	}
	lock = session_lock->data = ecalloc(1, sizeof(*lock));
	focusclient(NULL, 0);

	lock->scene = wlr_scene_tree_create(layers[LyrBlock]);
	cur_lock = lock->lock = session_lock;
	locked = 1;

	LISTEN(&session_lock->events.new_surface, &lock->new_surface, createlocksurface);
	LISTEN(&session_lock->events.destroy, &lock->destroy, destroysessionlock);
	LISTEN(&session_lock->events.unlock, &lock->unlock, unlocksession);

	wlr_session_lock_v1_send_locked(session_lock);
}

static uint64_t
lilith_monotonic_ms(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)ts.tv_nsec / 1000000ULL;
}

typedef struct {
	struct wlr_scene_node *node;
	int type;
	int parent;
	int ax, ay;
	int w, h;
	float opacity;
	float color[4];
	int sx, sy;
} CloseNode;

typedef struct {
	struct wl_list link;
	struct wl_listener destroy;
	Monitor *mon;
	struct wlr_scene_tree *tree;
	CloseNode *nodes;
	int n, cap;
	int x, y, w, h;
	uint64_t start_ms;
	int slide, dir;
	int dx;
	double dur, dist, endalpha;
	const char *bezier;
	int open;
	Client *client;
	double k0, a0;
	double z0, px0, py0, zr;
	int x0, y0;
	int zused;
} CloseAnim;

static struct wl_list closeanims = { &closeanims, &closeanims };

static bool
closeanim_no_input(struct wlr_scene_buffer *buf, double *sx, double *sy)
{
	(void)buf; (void)sx; (void)sy;
	return false;
}

static void
closeanim_destroy_notify(struct wl_listener *listener, void *data)
{
	CloseAnim *a = wl_container_of(listener, a, destroy);
	(void)data;
	if (a->open && a->client) {
		a->client->opening = 0;
		a->client->open_tree = NULL;
	}
	wl_list_remove(&a->destroy.link);
	wl_list_remove(&a->link);
	free(a->nodes);
	free(a);
}

static void
closeanim_buf_size(struct wlr_scene_buffer *b, int *w, int *h)
{
	if (b->dst_width > 0 && b->dst_height > 0) {
		*w = b->dst_width;
		*h = b->dst_height;
		return;
	}
	if (b->src_box.width > 0 && b->src_box.height > 0) {
		*w = (int)llround(b->src_box.width);
		*h = (int)llround(b->src_box.height);
	} else {
		*w = b->buffer->width;
		*h = b->buffer->height;
	}
	if (b->transform & WL_OUTPUT_TRANSFORM_90) {
		int t = *w; *w = *h; *h = t;
	}
}

static CloseNode *
closeanim_push(CloseAnim *a)
{
	if (a->n == a->cap) {
		int nc = a->cap ? a->cap * 2 : 16;
		CloseNode *nn = realloc(a->nodes, (size_t)nc * sizeof(*nn));
		if (!nn)
			return NULL;
		a->nodes = nn;
		a->cap = nc;
	}
	memset(&a->nodes[a->n], 0, sizeof(CloseNode));
	return &a->nodes[a->n++];
}

static int
closeanim_copy(CloseAnim *a, struct wlr_scene_node *src,
		struct wlr_scene_tree *dstparent, int parent, int pax, int pay)
{
	struct wlr_scene_node *dn = NULL;
	CloseNode *cn;
	float opacity = 1.0f, color[4] = {0};
	int idx, w = 0, h = 0, ax = pax + src->x, ay = pay + src->y;

	if (!src->enabled)
		return 1;

	switch (src->type) {
	case WLR_SCENE_NODE_TREE: {
		struct wlr_scene_tree *t = wlr_scene_tree_create(dstparent);
		if (!t)
			return 0;
		dn = &t->node;
		break;
	}
	case WLR_SCENE_NODE_RECT: {
		struct wlr_scene_rect *sr = wlr_scene_rect_from_node(src), *dr;
		w = sr->width;
		h = sr->height;
		memcpy(color, sr->color, sizeof(color));
		dr = wlr_scene_rect_create(dstparent, w, h, color);
		if (!dr)
			return 0;
		dn = &dr->node;
		break;
	}
	case WLR_SCENE_NODE_BUFFER: {
		struct wlr_scene_buffer *sb = wlr_scene_buffer_from_node(src), *db;
		if (!sb->buffer)
			return 1;
		closeanim_buf_size(sb, &w, &h);
		db = wlr_scene_buffer_create(dstparent, sb->buffer);
		if (!db)
			return 0;
		wlr_scene_buffer_set_source_box(db, &sb->src_box);
		wlr_scene_buffer_set_transform(db, sb->transform);
		wlr_scene_buffer_set_filter_mode(db, sb->filter_mode);
		wlr_scene_buffer_set_dest_size(db, w, h);
		wlr_scene_buffer_set_opacity(db, sb->opacity);
		db->point_accepts_input = closeanim_no_input;
		opacity = sb->opacity;
		dn = &db->node;
		break;
	}
	default:
		return 1;
	}

	wlr_scene_node_set_position(dn, src->x, src->y);

	idx = a->n;
	if (!(cn = closeanim_push(a))) {
		wlr_scene_node_destroy(dn);
		return 0;
	}
	cn->node = dn;
	cn->type = src->type;
	cn->parent = parent;
	cn->ax = ax;
	cn->ay = ay;
	cn->w = w;
	cn->h = h;
	cn->opacity = opacity;
	memcpy(cn->color, color, sizeof(color));

	if (src->type == WLR_SCENE_NODE_TREE) {
		struct wlr_scene_node *ch;
		struct wlr_scene_tree *st = wlr_scene_tree_from_node(src);
		struct wlr_scene_tree *dt = wlr_scene_tree_from_node(dn);
		wl_list_for_each(ch, &st->children, link) {
			if (!closeanim_copy(a, ch, dt, idx, ax, ay))
				return 0;
		}
	}
	return 1;
}

static void
closeanim_apply(CloseAnim *a, double k, double alpha)
{
	int i, j;
	double zr = a->zr > 1e-9 ? a->zr : 1.0;
	double kk = k * zr;

	k = kk;
	for (i = 0; i < a->n; i++) {
		CloseNode *cn = &a->nodes[i];
		CloseNode *p = cn->parent >= 0 ? &a->nodes[cn->parent] : NULL;
		int ex, ey;

		cn->sx = (int)llround((double)cn->ax * k);
		cn->sy = (int)llround((double)cn->ay * k);

		if (!p) {
			wlr_scene_node_set_position(cn->node,
				a->x + a->dx + (int)llround((double)a->w * (zr - k) / 2.0),
				a->y + (int)llround((double)a->h * (zr - k) / 2.0));
			continue;
		}
		wlr_scene_node_set_position(cn->node, cn->sx - p->sx, cn->sy - p->sy);

		ex = (int)llround((double)(cn->ax + cn->w) * k);
		ey = (int)llround((double)(cn->ay + cn->h) * k);

		if (cn->type == WLR_SCENE_NODE_RECT) {
			float col[4];
			for (j = 0; j < 4; j++)
				col[j] = cn->color[j] * (float)alpha;
			wlr_scene_rect_set_size(wlr_scene_rect_from_node(cn->node),
				MAX(0, ex - cn->sx), MAX(0, ey - cn->sy));
			wlr_scene_rect_set_color(wlr_scene_rect_from_node(cn->node), col);
		} else if (cn->type == WLR_SCENE_NODE_BUFFER) {
			struct wlr_scene_buffer *b = wlr_scene_buffer_from_node(cn->node);
			wlr_scene_buffer_set_dest_size(b,
				MAX(1, ex - cn->sx), MAX(1, ey - cn->sy));
			wlr_scene_buffer_set_opacity(b, cn->opacity * (float)alpha);
		}
	}
}

static void
closeanim_zoom_init(CloseAnim *a, Monitor *m)
{
	a->z0 = fabs(m->zoom_scale) > 1e-9 ? m->zoom_scale : 1.0;
	a->px0 = m->zoom_pan_x;
	a->py0 = m->zoom_pan_y;
	a->x0 = a->x;
	a->y0 = a->y;
	a->zr = 1.0;
	a->zused = 0;
}

static void
closeanim_zoom_sync(CloseAnim *a, Monitor *m)
{
	double r = fabs(m->zoom_scale) > 1e-9 ? m->zoom_scale / a->z0 : 1.0;

	a->zr = r;
	if (fabs(r - 1.0) > 1e-9)
		a->zused = 1;
	if (!a->open) {
		if (a->zused) {
			a->x = (int)llround(((double)a->x0 - a->px0) * r + m->zoom_pan_x);
			a->y = (int)llround(((double)a->y0 - a->py0) * r + m->zoom_pan_y);
		} else {
			a->x = a->x0;
			a->y = a->y0;
		}
	}
}

static void
closeanim_spawn(Client *c, int slide, int dir)
{
	Monitor *m = c->mon;
	CloseAnim *a;
	double s;

	if (slide == 1 ? (!animatetag || tagduration <= 0.0)
	    : slide == 2 ? (!animatetag || closeduration <= 0.0)
	    : (!animateclose || closeduration <= 0.0))
		return;
	if (!m || m->asleep || client_is_unmanaged(c) || c->isfullscreen
			|| !c->scene || !c->scene->node.parent
			|| !c->scene->node.enabled || !VISIBLEON(c, m))
		return;

	a = calloc(1, sizeof(*a));
	if (!a)
		return;

	s = m->zoom_scale;
	a->mon = m;
	a->x = c->scene->node.x;
	a->y = c->scene->node.y;
	a->w = MAX(1, (int)llround((double)c->geom.width  * s));
	a->h = MAX(1, (int)llround((double)c->geom.height * s));
	a->slide = (slide == 1);
	a->dir = dir;
	closeanim_zoom_init(a, m);
	if (slide == 1) {
		a->dur = tagduration;
		a->bezier = tagbezier;
		if (tagstyle == 1) {
			a->endalpha = 0.0;
			a->dist = 0.0;
		} else {
			a->endalpha = tagfadeout;
			a->dist = (double)m->m.width * tagslide;
		}
	} else {
		a->dur = closeduration;
		a->bezier = closebezier;
		a->endalpha = closealpha;
	}

	if (fabs(m->zoom_scale - 1.0) >= 1e-9)
		wlr_scene_node_for_each_buffer(&c->scene->node, zoom_scale_buffers, c);

	if (!closeanim_copy(a, &c->scene->node,
			(slide == 1 && tagstyle == 1) ? layers[LyrFloat] : c->scene->node.parent,
			-1, -c->scene->node.x, -c->scene->node.y) || a->n == 0) {
		if (a->n > 0)
			wlr_scene_node_destroy(a->nodes[0].node);
		free(a->nodes);
		free(a);
		return;
	}

	a->tree = wlr_scene_tree_from_node(a->nodes[0].node);
	if (slide == 1 && tagstyle == 1)
		wlr_scene_node_raise_to_top(&a->tree->node);
	else
		wlr_scene_node_place_below(&a->tree->node, &c->scene->node);

	a->destroy.notify = closeanim_destroy_notify;
	wl_signal_add(&a->tree->node.events.destroy, &a->destroy);
	a->start_ms = lilith_monotonic_ms();
	wl_list_insert(&closeanims, &a->link);
	wlr_output_schedule_frame(m->wlr_output);
}

void
closeanim_start(Client *c)
{
	closeanim_spawn(c, 0, 0);
}

void
closeanim_step(Monitor *m)
{
	CloseAnim *a, *tmp;
	uint64_t now;
	int busy = 0;

	if (wl_list_empty(&closeanims))
		return;

	now = lilith_monotonic_ms();
	wl_list_for_each_safe(a, tmp, &closeanims, link) {
		double t, e, k, alpha;

		if (a->mon != m)
			continue;
		t = (double)(now - a->start_ms) / (a->dur * 1000.0);
		if (a->open) {
			Client *oc = a->client;

			if (t >= 1.0 || !oc || !oc->scene || oc->mon != m) {
				wlr_scene_node_destroy(&a->tree->node);
				if (oc)
					openanim_reveal(oc);
				continue;
			}
			e = bezier_ease(a->bezier, t);
			k = a->k0 + (1.0 - a->k0) * e;
			if (k < 0.01)
				k = 0.01;
			alpha = a->a0 + (1.0 - a->a0) * e;
			if (alpha < 0.0)
				alpha = 0.0;
			if (alpha > 1.0)
				alpha = 1.0;
			a->x = oc->scene->node.x;
			a->y = oc->scene->node.y;
			closeanim_zoom_sync(a, m);
			wlr_scene_node_set_enabled(&a->tree->node, VISIBLEON(oc, m));
			closeanim_apply(a, k, alpha);
			busy = 1;
			continue;
		}
		if (t >= 1.0) {
			wlr_scene_node_destroy(&a->tree->node);
			continue;
		}
		closeanim_zoom_sync(a, m);
		e = bezier_ease(a->bezier, t);
		alpha = 1.0 - (1.0 - a->endalpha) * e;
		if (alpha < 0.0)
			alpha = 0.0;
		if (alpha > 1.0)
			alpha = 1.0;
		if (a->slide) {
			a->dx = (int)llround(-(double)a->dir * a->dist * e);
			if (a->endalpha >= 1.0 && !a->zused)
				wlr_scene_node_set_position(a->nodes[0].node, a->x + a->dx, a->y);
			else
				closeanim_apply(a, 1.0, alpha);
		} else {
			k = 1.0 + (closescale - 1.0) * e;
			if (k < 0.01)
				k = 0.01;
			closeanim_apply(a, k, alpha);
		}
		busy = 1;
	}
	if (busy)
		wlr_output_schedule_frame(m->wlr_output);
}

void
tagslide_capture(Monitor *m, int dir, uint32_t newtags)
{
	Client *c;

	wl_list_for_each(c, &clients, link) {
		if (c->mon != m)
			continue;
		c->was_vis = VISIBLEON(c, m);
		openanim_abort(c);
		if (tagstyle == 2) {
			if (!(c->tags & newtags))
				closeanim_spawn(c, 2, dir);
		} else
			closeanim_spawn(c, 1, dir);
	}
}

void
tagslide_start(Monitor *m, int dir)
{
	if (!animatetag || tagduration <= 0.0 || tagstyle != 0)
		return;
	m->tag_from_x = (double)dir * (double)m->m.width * tagslide;
	m->tag_off_x = m->tag_from_x;
	m->tag_start_ms = lilith_monotonic_ms();
	m->tag_anim = 1;
	wlr_output_schedule_frame(m->wlr_output);
}

void
tagslide_step(Monitor *m)
{
	double t;

	if (!m->tag_anim)
		return;
	t = (double)(lilith_monotonic_ms() - m->tag_start_ms) / (tagduration * 1000.0);
	if (t >= 1.0) {
		m->tag_anim = 0;
		m->tag_off_x = 0.0;
	} else {
		m->tag_off_x = m->tag_from_x * (1.0 - bezier_ease(tagbezier, t));
	}
	zoom_apply(m);
	if (m->tag_anim)
		wlr_output_schedule_frame(m->wlr_output);
}

void
closeanim_flush(Monitor *m)
{
	CloseAnim *a, *tmp;

	wl_list_for_each_safe(a, tmp, &closeanims, link) {
		if (a->mon == m) {
			Client *oc = a->open ? a->client : NULL;
			wlr_scene_node_destroy(&a->tree->node);
			if (oc)
				openanim_reveal(oc);
		}
	}
}

static void
openanim_reveal(Client *c)
{
	if (c->scene && c->mon)
		wlr_scene_node_set_enabled(&c->scene->node,
				VISIBLEON(c, c->mon) && c != waiting_geo_client);
}

static void
openanim_spawn(Client *c)
{
	Monitor *m = c ? c->mon : NULL;
	CloseAnim *a;
	double k, alpha;

	if (!animateopen || openduration <= 0.0)
		return;
	if (!m || m->asleep || c->opening || client_is_unmanaged(c) || c->isfullscreen
			|| !client_surface(c)->mapped || !c->scene || !c->scene->node.parent
			|| !c->scene->node.enabled || !VISIBLEON(c, m))
		return;

	a = calloc(1, sizeof(*a));
	if (!a)
		return;

	a->mon = m;
	a->x = c->scene->node.x;
	a->y = c->scene->node.y;
	a->w = MAX(1, (int)llround((double)c->geom.width  * m->zoom_scale));
	a->h = MAX(1, (int)llround((double)c->geom.height * m->zoom_scale));
	a->open = 1;
	a->client = c;
	a->dur = openduration;
	a->bezier = openbezier;
	a->k0 = openscale;
	a->a0 = openalpha;
	closeanim_zoom_init(a, m);

	if (fabs(m->zoom_scale - 1.0) >= 1e-9)
		wlr_scene_node_for_each_buffer(&c->scene->node, zoom_scale_buffers, c);

	if (!closeanim_copy(a, &c->scene->node, c->scene->node.parent,
			-1, -c->scene->node.x, -c->scene->node.y) || a->n == 0) {
		if (a->n > 0)
			wlr_scene_node_destroy(a->nodes[0].node);
		free(a->nodes);
		free(a);
		return;
	}

	a->tree = wlr_scene_tree_from_node(a->nodes[0].node);
	wlr_scene_node_place_above(&a->tree->node, &c->scene->node);

	k = a->k0 < 0.01 ? 0.01 : a->k0;
	alpha = a->a0 < 0.0 ? 0.0 : (a->a0 > 1.0 ? 1.0 : a->a0);
	closeanim_apply(a, k, alpha);

	a->destroy.notify = closeanim_destroy_notify;
	wl_signal_add(&a->tree->node.events.destroy, &a->destroy);
	a->start_ms = lilith_monotonic_ms();
	wl_list_insert(&closeanims, &a->link);

	c->opening = 1;
	c->open_tree = a->tree;
	wlr_scene_node_set_enabled(&c->scene->node, 0);
	wlr_output_schedule_frame(m->wlr_output);
}

void
openanim_start(Client *c)
{
	openanim_spawn(c);
}

void
openanim_abort(Client *c)
{
	if (!c->opening || !c->open_tree)
		return;
	wlr_scene_node_destroy(&c->open_tree->node);
	openanim_reveal(c);
}

void
tagopen_start(Monitor *m)
{
	Client *c;

	if (tagstyle != 2 || !animatetag || tagduration <= 0.0)
		return;
	wl_list_for_each(c, &clients, link) {
		if (c->mon == m && !c->was_vis && VISIBLEON(c, m))
			openanim_spawn(c);
	}
}

void
mapnotify(struct wl_listener *listener, void *data)
{
	Client *p = NULL;
	Client *w, *c = wl_container_of(listener, c, map);
	Monitor *m;
	int i;

	c->scene = client_surface(c)->data = wlr_scene_tree_create(layers[LyrTile]);
	wlr_scene_node_set_enabled(&c->scene->node, client_is_unmanaged(c));
	c->scene_surface = c->type == XDGShell
			? wlr_scene_xdg_surface_create(c->scene, c->surface.xdg)
			: wlr_scene_subsurface_tree_create(c->scene, client_surface(c));
	c->scene->node.data = c->scene_surface->node.data = c;

	client_get_geometry(c, &c->geom);

	if (client_is_unmanaged(c)) {
		wlr_scene_node_reparent(&c->scene->node, layers[LyrFloat]);
		wlr_scene_node_set_position(&c->scene->node, c->geom.x, c->geom.y);
		client_set_size(c, c->geom.width, c->geom.height);
		if (client_wants_focus(c)) {
			focusclient(c, 1);
			exclusive_focus = c;
		}
		goto unset_fullscreen;
	}

	for (i = 0; i < 4; i++) {
		c->inner_border[i] = wlr_scene_rect_create(c->scene, 0, 0,
			c->isurgent ? urgent_inner_border_color : unfocused_inner_border_color);
		c->inner_border[i]->node.data = c;

		c->outer_border[i] = wlr_scene_rect_create(c->scene, 0, 0,
			c->isurgent ? urgent_outer_border_color : unfocused_outer_border_color);
		c->outer_border[i]->node.data = c;
	}

	client_set_tiled(c, WLR_EDGE_TOP | WLR_EDGE_BOTTOM | WLR_EDGE_LEFT | WLR_EDGE_RIGHT);

	c->geom.width += 2 * c->bw;
	c->geom.height += 2 * c->bw;

	wl_list_insert(&clients, &c->link);
	wl_list_insert(&fstack, &c->flink);

	if ((p = client_get_parent(c))) {
		c->isfloating = 1;
		setmon(c, p->mon, p->tags);
	} else {
		applyrules(c);
	}
	printstatus();

	if (!client_is_unmanaged(c)) {
		if (c->isfullscreen || waiting_geo_client) {
			wlr_scene_node_set_enabled(&c->scene->node, 1);
		} else if (has_pending_spawn_geo) {
			has_pending_spawn_geo = 0;
			c->isfloating = 1;
			if (!c->mon)
				c->mon = selmon;
			if (c->mon) {
				struct wlr_box min_hint = {0};
				int bw2 = 2 * (int)c->bw;
				if (pending_spawn_geo.width  < min_hint.width  + bw2 && min_hint.width  > 0)
					pending_spawn_geo.width  = min_hint.width  + bw2;
				if (pending_spawn_geo.height < min_hint.height + bw2 && min_hint.height > 0)
					pending_spawn_geo.height = min_hint.height + bw2;
				c->geom = pending_spawn_geo;
			}
		} else {
			wlr_scene_node_set_enabled(&c->scene->node, 0);
			waiting_geo_client = c;
			spawnregion(NULL);
		}
	}
unset_fullscreen:
	m = c->mon ? c->mon : xytomon(c->geom.x, c->geom.y);
	wl_list_for_each(w, &clients, link) {
		if (w != c && w != p && w->isfullscreen && m == w->mon && (w->tags & c->tags))
			setfullscreen(w, 0);
	}

	if (m) {
		int tagidx = getcurrenttag(m);
		c->home_cx = m->canvas[tagidx].cx;
		c->home_cy = m->canvas[tagidx].cy;
	}
	openanim_start(c);

}

void
maximizenotify(struct wl_listener *listener, void *data)
{
	Client *c = wl_container_of(listener, c, maximize);
	if (c->surface.xdg->initialized
			&& wl_resource_get_version(c->surface.xdg->toplevel->resource)
					< XDG_TOPLEVEL_WM_CAPABILITIES_SINCE_VERSION)
		wlr_xdg_surface_schedule_configure(c->surface.xdg);
}

void
motionabsolute(struct wl_listener *listener, void *data)
{
	struct wlr_pointer_motion_absolute_event *event = data;
	double lx, ly, dx, dy;

	if (!event->time_msec)
		wlr_cursor_warp_absolute(cursor, &event->pointer->base, event->x, event->y);

	wlr_cursor_absolute_to_layout_coords(cursor, &event->pointer->base, event->x, event->y, &lx, &ly);
	dx = lx - cursor->x;
	dy = ly - cursor->y;
	motionnotify(event->time_msec, &event->pointer->base, dx, dy, dx, dy);
}

void
motionnotify(uint32_t time, struct wlr_input_device *device, double dx, double dy,
		double dx_unaccel, double dy_unaccel)
{
	double sx = 0, sy = 0, sx_confined, sy_confined;
	Client *c = NULL, *w = NULL;
	LayerSurface *l = NULL;
	struct wlr_surface *surface = NULL;
	struct wlr_pointer_constraint_v1 *constraint;

	xytonode(cursor->x, cursor->y, &surface, &c, &l, &sx, &sy);

	if (cursor_mode == CurPressed && !seat->drag
			&& surface != seat->pointer_state.focused_surface
			&& toplevel_from_wlr_surface(seat->pointer_state.focused_surface, &w, &l) >= 0) {
		c = w;
		surface = seat->pointer_state.focused_surface;
		if (l) {
			sx = cursor->x - l->scene->node.x;
			sy = cursor->y - l->scene->node.y;
		} else if (w && w->mon && fabs(w->mon->zoom_scale - 1.0) >= 1e-9) {
			double s = w->mon->zoom_scale;
			int scene_x = (int)llround((double)w->geom.x * s + w->mon->zoom_pan_x);
			int scene_y = (int)llround((double)w->geom.y * s + w->mon->zoom_pan_y);
			int bw_s    = MAX(1, (int)llround((double)w->bw * s));
			int gw_s    = (int)llround((double)w->geom.width  * s);
			int gh_s    = (int)llround((double)w->geom.height * s);
			int inner_w = MAX(1, gw_s - 2 * bw_s);
			int inner_h = MAX(1, gh_s - 2 * bw_s);
			double cw   = (double)MAX(1, w->geom.width  - 2 * w->bw);
			double ch   = (double)MAX(1, w->geom.height - 2 * w->bw);
			sx = (cursor->x - (scene_x + bw_s)) * cw / (double)inner_w;
			sy = (cursor->y - (scene_y + bw_s)) * ch / (double)inner_h;
		} else {
			sx = cursor->x - w->geom.x;
			sy = cursor->y - w->geom.y;
		}
	}

	if (time) {
		wlr_relative_pointer_manager_v1_send_relative_motion(
				relative_pointer_mgr, seat, (uint64_t)time * 1000,
				dx, dy, dx_unaccel, dy_unaccel);

		wl_list_for_each(constraint, &pointer_constraints->constraints, link)
			cursorconstrain(constraint);

		if (active_constraint && cursor_mode != CurResize && cursor_mode != CurMove) {
			toplevel_from_wlr_surface(active_constraint->surface, &c, NULL);
			if (c && active_constraint->surface == seat->pointer_state.focused_surface) {
				sx = cursor->x - c->geom.x - c->bw;
				sy = cursor->y - c->geom.y - c->bw;
				if (wlr_region_confine(&active_constraint->region, sx, sy,
						sx + dx, sy + dy, &sx_confined, &sy_confined)) {
					dx = sx_confined - sx;
					dy = sy_confined - sy;
				}

				if (active_constraint->type == WLR_POINTER_CONSTRAINT_V1_LOCKED)
					return;
			}
		}

		wlr_cursor_move(cursor, device, dx, dy);
		wlr_idle_notifier_v1_notify_activity(idle_notifier, seat);

		if (sloppyfocus)
			selmon = xytomon(cursor->x, cursor->y);
	}

	wlr_scene_node_set_position(&drag_icon->node, (int)round(cursor->x), (int)round(cursor->y));

	if (cursor_mode == CurSpawnRegion) {
		spawnregion_x2 = cursor->x;
		spawnregion_y2 = cursor->y;
		spawnregion_update_rect();
		return;
	}
	if (cursor_mode == CurMove) {
		if (canvas_panning) {
			int tagidx;
			double raw_dx, raw_dy;
			double pdx_f, pdy_f;
			int pdx, pdy;
			Client *pc;
			if (!selmon) {
				canvas_panning = 0;
				cursor_mode = CurNormal;
				wlr_cursor_set_xcursor(cursor, cursor_mgr, "default");
				return;
			}
			tagidx = getcurrenttag(selmon);
			raw_dx = cursor->x - canvas_pan_last_x;
			raw_dy = cursor->y - canvas_pan_last_y;
			canvas_pan_last_x = cursor->x;
			canvas_pan_last_y = cursor->y;
			if (movecanvasmouse_multiplier_is_affected_by_zoom) {
				double s = selmon->zoom_scale;
				if (fabs(s) < 1e-9) s = 1.0;
				pdx_f = raw_dx * canvas_pan_multiplier / s;
				pdy_f = raw_dy * canvas_pan_multiplier / s;
			} else {
				pdx_f = raw_dx * canvas_pan_multiplier;
				pdy_f = raw_dy * canvas_pan_multiplier;
			}
			canvas_pan_accum_x += (float)pdx_f;
			canvas_pan_accum_y += (float)pdy_f;
			pdx = (int)canvas_pan_accum_x;
			pdy = (int)canvas_pan_accum_y;
			canvas_pan_accum_x -= pdx;
			canvas_pan_accum_y -= pdy;
			selmon->canvas[tagidx].cx += pdx;
			selmon->canvas[tagidx].cy += pdy;
			inertia_sample(selmon->canvas[tagidx].cx, selmon->canvas[tagidx].cy);
			wl_list_for_each(pc, &clients, link) {
				if (!VISIBLEON(pc, selmon))
					continue;
				pc->geom.x += pdx;
				pc->geom.y += pdy;
			}
			zoom_apply(selmon);
		} else if (grabc) {
			{
				double zmv_s = grabc->mon ? grabc->mon->zoom_scale : 1.0;
				int zmv_nx, zmv_ny;
				if (fabs(zmv_s) < 1e-9) zmv_s = 1.0;
				zmv_nx = (int)llround((cursor->x - grabcx - (grabc->mon ? grabc->mon->zoom_pan_x : 0.0)) / zmv_s);
				zmv_ny = (int)llround((cursor->y - grabcy - (grabc->mon ? grabc->mon->zoom_pan_y : 0.0)) / zmv_s);
				inertia_sample(zmv_nx, zmv_ny);
				if (grabc->mon)
					snap_edges(grabc, grabc->mon, &zmv_nx, &zmv_ny);
				canvas_move_window(grabc, zmv_nx, zmv_ny);
			}
		}
		return;
	} else if (cursor_mode == CurResize) {
		if (grabc && grabc->mon) {
			double rzs = grabc->mon->zoom_scale;
			int rzsx, rzsy, new_w, new_h;
			struct wlr_box geo;
			if (fabs(rzs) < 1e-9) rzs = 1.0;
			rzsx = (int)llround(resize_grab_world_x * rzs + grabc->mon->zoom_pan_x);
			rzsy = (int)llround(resize_grab_world_y * rzs + grabc->mon->zoom_pan_y);
			new_w = (int)llround((cursor->x - rzsx) / rzs);
			new_h = (int)llround((cursor->y - rzsy) / rzs);
			geo = (struct wlr_box){.x = grabc->geom.x, .y = grabc->geom.y,
				.width = new_w, .height = new_h};
			resize_magnetically(grabc, grabc->mon, &geo);
			resize(grabc, geo, 1);
		} else {
			struct wlr_box geo;
			geo = (struct wlr_box){.x = grabc->geom.x, .y = grabc->geom.y,
				.width = (int)round(cursor->x) - grabc->geom.x,
				.height = (int)round(cursor->y) - grabc->geom.y};
			resize_magnetically(grabc, grabc->mon, &geo);
			resize(grabc, geo, 1);
		}
		return;
	}

	if (!surface && !seat->drag && !spawnregion_active)
		wlr_cursor_set_xcursor(cursor, cursor_mgr, "default");

	if (c && !surface && c->mon && fabs(c->mon->zoom_scale - 1.0) >= 1e-9)
		surface = client_surface(c);

	pointerfocus(c, surface, sx, sy, time);
}

void
motionrelative(struct wl_listener *listener, void *data)
{
	struct wlr_pointer_motion_event *event = data;
	motionnotify(event->time_msec, &event->pointer->base, event->delta_x, event->delta_y,
			event->unaccel_dx, event->unaccel_dy);
}

void
moveresize(const Arg *arg)
{
	if (cursor_mode != CurNormal && cursor_mode != CurPressed)
		return;
	xytonode(cursor->x, cursor->y, NULL, &grabc, NULL, NULL, NULL);
	if (!grabc || client_is_unmanaged(grabc) || grabc->isfullscreen)
		return;

	setfloating(grabc, 1);
	resize_snap_x_target = 0;
	resize_snap_y_target = 0;
	switch (cursor_mode = arg->ui) {
	case CurMove: {
		Monitor *m = grabc->mon;
		double s;
		int mvsx, mvsy;
		s = m ? m->zoom_scale : 1.0;
		if (fabs(s) < 1e-9) s = 1.0;
		mvsx = m ? (int)llround((double)grabc->geom.x * s + m->zoom_pan_x) : grabc->geom.x;
		mvsy = m ? (int)llround((double)grabc->geom.y * s + m->zoom_pan_y) : grabc->geom.y;
		grabcx = (int)round(cursor->x) - mvsx;
		grabcy = (int)round(cursor->y) - mvsy;
		inertia_grab_begin(grabc);
		wlr_cursor_set_xcursor(cursor, cursor_mgr, "all-scroll");
		break;
	}
	case CurResize: {
		Monitor *m = grabc->mon;
		double s;
		int ex, ey;
		s = m ? m->zoom_scale : 1.0;
		if (fabs(s) < 1e-9) s = 1.0;
		resize_grab_world_x = grabc->geom.x;
		resize_grab_world_y = grabc->geom.y;
		ex = m ? (int)llround((double)(grabc->geom.x + grabc->geom.width) * s + m->zoom_pan_x)
		       : grabc->geom.x + grabc->geom.width;
		ey = m ? (int)llround((double)(grabc->geom.y + grabc->geom.height) * s + m->zoom_pan_y)
		       : grabc->geom.y + grabc->geom.height;
		wlr_cursor_warp_closest(cursor, NULL, ex, ey);
		wlr_cursor_set_xcursor(cursor, cursor_mgr, "se-resize");
		break;
	}
	}
}

void
outputmgrapply(struct wl_listener *listener, void *data)
{
	struct wlr_output_configuration_v1 *config = data;
	outputmgrapplyortest(config, 0);
}

void
outputmgrapplyortest(struct wlr_output_configuration_v1 *config, int test)
{
	struct wlr_output_configuration_head_v1 *config_head;
	int ok = 1;

	wl_list_for_each(config_head, &config->heads, link) {
		struct wlr_output *wlr_output = config_head->state.output;
		Monitor *m = wlr_output->data;
		struct wlr_output_state state;

		m->asleep = 0;

		wlr_output_state_init(&state);
		wlr_output_state_set_enabled(&state, config_head->state.enabled);
		if (!config_head->state.enabled)
			goto apply_or_test;

		if (config_head->state.mode)
			wlr_output_state_set_mode(&state, config_head->state.mode);
		else
			wlr_output_state_set_custom_mode(&state,
					config_head->state.custom_mode.width,
					config_head->state.custom_mode.height,
					config_head->state.custom_mode.refresh);

		wlr_output_state_set_transform(&state, config_head->state.transform);
		wlr_output_state_set_scale(&state, config_head->state.scale);
		wlr_output_state_set_adaptive_sync_enabled(&state,
				config_head->state.adaptive_sync_enabled);

apply_or_test:
		ok &= test ? wlr_output_test_state(wlr_output, &state)
				: wlr_output_commit_state(wlr_output, &state);

		if (!test && wlr_output->enabled && (m->m.x != config_head->state.x || m->m.y != config_head->state.y))
			wlr_output_layout_add(output_layout, wlr_output,
					config_head->state.x, config_head->state.y);

		wlr_output_state_finish(&state);
	}

	if (ok)
		wlr_output_configuration_v1_send_succeeded(config);
	else
		wlr_output_configuration_v1_send_failed(config);
	wlr_output_configuration_v1_destroy(config);

	updatemons(NULL, NULL);
}

void
outputmgrtest(struct wl_listener *listener, void *data)
{
	struct wlr_output_configuration_v1 *config = data;
	outputmgrapplyortest(config, 1);
}

void
pointerfocus(Client *c, struct wlr_surface *surface, double sx, double sy,
		uint32_t time)
{
	struct timespec now;

	if (surface != seat->pointer_state.focused_surface &&
			sloppyfocus && time && c && !client_is_unmanaged(c))
		focusclient(c, 1);

	if (!surface) {
		wlr_seat_pointer_notify_clear_focus(seat);
		return;
	}

	if (!time) {
		clock_gettime(CLOCK_MONOTONIC, &now);
		time = now.tv_sec * 1000 + now.tv_nsec / 1000000;
	}

	wlr_seat_pointer_notify_enter(seat, surface, sx, sy);
	wlr_seat_pointer_notify_motion(seat, time, sx, sy);
}

void
printstatus(void)
{
	Monitor *m = NULL;
	Client *c;
	uint32_t occ, urg, sel;

	wl_list_for_each(m, &mons, link) {
		occ = urg = 0;
		wl_list_for_each(c, &clients, link) {
			if (c->mon != m)
				continue;
			occ |= c->tags;
			if (c->isurgent)
				urg |= c->tags;
		}
		if ((c = focustop(m))) {
			printf("%s title %s\n", m->wlr_output->name, client_get_title(c));
			printf("%s appid %s\n", m->wlr_output->name, client_get_appid(c));
			printf("%s fullscreen %d\n", m->wlr_output->name, c->isfullscreen);
			printf("%s floating %d\n", m->wlr_output->name, c->isfloating);
			sel = c->tags;
		} else {
			printf("%s title \n", m->wlr_output->name);
			printf("%s appid \n", m->wlr_output->name);
			printf("%s fullscreen \n", m->wlr_output->name);
			printf("%s floating \n", m->wlr_output->name);
			sel = 0;
		}

		printf("%s selmon %u\n", m->wlr_output->name, m == selmon);
		printf("%s tags %"PRIu32" %"PRIu32" %"PRIu32" %"PRIu32"\n",
			m->wlr_output->name, occ, m->tagset[m->seltags], sel, urg);
		printf("%s layout ><>\n", m->wlr_output->name);
	}
	fflush(stdout);
	bar_draw_all();
}

void
powermgrsetmode(struct wl_listener *listener, void *data)
{
	struct wlr_output_power_v1_set_mode_event *event = data;
	struct wlr_output_state state = {0};
	Monitor *m = event->output->data;

	if (!m)
		return;

	m->gamma_lut_changed = 1;
	wlr_output_state_set_enabled(&state, event->mode);
	wlr_output_commit_state(m->wlr_output, &state);

	m->asleep = !event->mode;
	updatemons(NULL, NULL);
}

void
quit(const Arg *arg)
{
	wl_display_terminate(dpy);
}

void
rendermon(struct wl_listener *listener, void *data)
{
	Monitor *m = wl_container_of(listener, m, frame);
	Client *c;
	struct wlr_output_state pending = {0};
	struct timespec now;

	pan_anim_step(m);

	closeanim_step(m);

	tagslide_step(m);

	bar_anim_step(m);
	bar_hanim_step(m);

	inertia_step(m);
	canvas_inertia_step(m);

	zoom_frame(m);

	wl_list_for_each(c, &clients, link) {
		if (c->resize && !c->isfloating && client_is_rendered_on_mon(c, m) && !client_is_stopped(c))
			goto skip;
	}

	if (fabs(m->zoom_scale - 1.0) > 1e-5)
		zoom_apply(m);

	if (!wlr_scene_output_build_state(m->scene_output, &pending, NULL))
		goto skip;
	if (!wlr_output_commit_state(m->wlr_output, &pending))
		wlr_log(WLR_ERROR, "Failed to commit rendered frame on %s", m->wlr_output->name);

skip:
	clock_gettime(CLOCK_MONOTONIC, &now);
	wlr_scene_output_send_frame_done(m->scene_output, &now);
	wlr_output_state_finish(&pending);
}

void
requestdecorationmode(struct wl_listener *listener, void *data)
{
	Client *c = wl_container_of(listener, c, set_decoration_mode);
	if (c->surface.xdg->initialized)
		wlr_xdg_toplevel_decoration_v1_set_mode(c->decoration,
				WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
}

void
requeststartdrag(struct wl_listener *listener, void *data)
{
	struct wlr_seat_request_start_drag_event *event = data;

	if (wlr_seat_validate_pointer_grab_serial(seat, event->origin,
			event->serial))
		wlr_seat_start_pointer_drag(seat, event->drag, event->serial);
	else
		wlr_data_source_destroy(event->drag->source);
}

void
requestmonstate(struct wl_listener *listener, void *data)
{
	struct wlr_output_event_request_state *event = data;
	wlr_output_commit_state(event->output, event->state);
	updatemons(NULL, NULL);
}

void
resize(Client *c, struct wlr_box geo, int interact)
{
	struct wlr_box *bbox;
	struct wlr_box clip;

	if (!c->mon || !client_surface(c)->mapped)
		return;

	bbox = interact ? &sgeom : &c->mon->w;

	client_set_bounds(c, geo.width, geo.height);
	c->geom = geo;
	if (c->mon && fabs(c->mon->zoom_scale - 1.0) >= 1e-9) {
		c->geom.width  = MAX(1 + 2 * (int)c->bw, c->geom.width);
		c->geom.height = MAX(1 + 2 * (int)c->bw, c->geom.height);
	} else
	if (!c->isfloating || cursor_mode == CurResize)
		applybounds(c, bbox);

	zoom_update_scene_client(c);

	c->resize = client_set_size(c, c->geom.width - 2 * c->bw,
			c->geom.height - 2 * c->bw);
	client_get_clip(c, &clip);
	wlr_scene_subsurface_tree_set_clip(&c->scene_surface->node, &clip);
}

void
run(char *startup_cmd)
{
	const char *socket = wl_display_add_socket_auto(dpy);
	if (!socket)
		die("startup: display_add_socket_auto");
	setenv("WAYLAND_DISPLAY", socket, 1);

	if (!wlr_backend_start(backend))
		die("startup: backend_start");

	if (startup_cmd) {
		int piperw[2];
		if (pipe(piperw) < 0)
			die("startup: pipe:");
		if ((child_pid = fork()) < 0)
			die("startup: fork:");
		if (child_pid == 0) {
			setsid();
			dup2(piperw[0], STDIN_FILENO);
			close(piperw[0]);
			close(piperw[1]);
			execl("/bin/sh", "/bin/sh", "-c", startup_cmd, NULL);
			die("startup: execl:");
		}
		dup2(piperw[1], STDOUT_FILENO);
		close(piperw[1]);
		close(piperw[0]);
	}

	if (fd_set_nonblock(STDOUT_FILENO) < 0)
		close(STDOUT_FILENO);

	printstatus();

	selmon = xytomon(cursor->x, cursor->y);

	wlr_cursor_warp_closest(cursor, NULL, cursor->x, cursor->y);
	wlr_cursor_set_xcursor(cursor, cursor_mgr, "default");

	wl_display_run(dpy);
}

void
setcursor(struct wl_listener *listener, void *data)
{
	struct wlr_seat_pointer_request_set_cursor_event *event = data;
	if (spawnregion_active)
		return;
	if (cursor_mode != CurNormal && cursor_mode != CurPressed)
		return;
	if (event->seat_client == seat->pointer_state.focused_client)
		wlr_cursor_set_surface(cursor, event->surface,
				event->hotspot_x, event->hotspot_y);
}

void
setcursorshape(struct wl_listener *listener, void *data)
{
	struct wlr_cursor_shape_manager_v1_request_set_shape_event *event = data;

	if (spawnregion_active)
		return;
	if (cursor_mode != CurNormal && cursor_mode != CurPressed)
		return;
	if (event->seat_client == seat->pointer_state.focused_client)
		wlr_cursor_set_xcursor(cursor, cursor_mgr,
				wlr_cursor_shape_v1_name(event->shape));
}

void
setfloating(Client *c, int floating)
{
	c->isfloating = floating;
}

void
setfullscreen(Client *c, int fullscreen)
{
	if (c->isfakefullscreen)
		return;
	c->isfullscreen = fullscreen;
	if (!c->mon || !client_surface(c)->mapped)
		return;

		c->bw = fullscreen ? 0 : (innerborderpx + outerborderpx);
		c->ibw = fullscreen ? 0 : innerborderpx;
		c->obw = fullscreen ? 0 : outerborderpx;

	client_set_fullscreen(c, fullscreen);
	wlr_scene_node_reparent(&c->scene->node, layers[c->isfullscreen
			? LyrFS : c->isfloating ? LyrFloat : LyrTile]);

	if (fullscreen) {
		c->prev = c->geom;
		resize(c, c->mon->m, 0);
	} else {
		resize(c, c->prev, 0);
	}
	arrange(c->mon);
	printstatus();
}

void
setmon(Client *c, Monitor *m, uint32_t newtags)
{
	Monitor *oldmon = c->mon;

	if (oldmon == m)
		return;
	c->mon = m;
	c->prev = c->geom;

	if (oldmon)
		arrange(oldmon);
	if (m) {
		resize(c, c->geom, 0);
		c->tags = newtags ? newtags : m->tagset[m->seltags];

#if CENTER_NEW_WINDOWS
c->prev.x = (m->w.width - c->prev.width) / 2 + m->w.x;
		c->prev.y = (m->w.height - c->prev.height) / 2 + m->w.y;
#endif
		setfullscreen(c, c->isfullscreen);
		setfloating(c, c->isfloating);
		{
			int tagidx = getcurrenttag(m);
			c->home_cx = m->canvas[tagidx].cx;
			c->home_cy = m->canvas[tagidx].cy;
		}
	}
	focusclient(focustop(selmon), 1);
}

void
setpsel(struct wl_listener *listener, void *data)
{
	struct wlr_seat_request_set_primary_selection_event *event = data;
	wlr_seat_set_primary_selection(seat, event->source, event->serial);
}

void
setsel(struct wl_listener *listener, void *data)
{
	struct wlr_seat_request_set_selection_event *event = data;
	wlr_seat_set_selection(seat, event->source, event->serial);
}

void
setup(void)
{
	int drm_fd, i, sig[] = {SIGCHLD, SIGINT, SIGTERM, SIGPIPE};
	struct sigaction sa = {.sa_flags = SA_RESTART, .sa_handler = handlesig};
	sigemptyset(&sa.sa_mask);

	for (i = 0; i < (int)LENGTH(sig); i++)
		sigaction(sig[i], &sa, NULL);

	wlr_log_init(log_level, NULL);

	dpy = wl_display_create();
	event_loop = wl_display_get_event_loop(dpy);

	if (!(backend = wlr_backend_autocreate(event_loop, &session)))
		die("couldn't create backend");

	scene = wlr_scene_create();
	root_bg = wlr_scene_rect_create(&scene->tree, 0, 0, rootcolor);
	wallpaper_layer = wlr_scene_tree_create(&scene->tree);
	for (i = 0; i < NUM_LAYERS; i++)
		layers[i] = wlr_scene_tree_create(&scene->tree);
	drag_icon = wlr_scene_tree_create(&scene->tree);
	wlr_scene_node_place_below(&drag_icon->node, &layers[LyrBlock]->node);

	if (!(drw = wlr_renderer_autocreate(backend)))
		die("couldn't create renderer");
	wl_signal_add(&drw->events.lost, &gpu_reset);

	wlr_renderer_init_wl_shm(drw, dpy);

	if (wlr_renderer_get_texture_formats(drw, WLR_BUFFER_CAP_DMABUF)) {
		wlr_drm_create(dpy, drw);
		wlr_scene_set_linux_dmabuf_v1(scene,
				wlr_linux_dmabuf_v1_create_with_renderer(dpy, 5, drw));
	}

	if ((drm_fd = wlr_renderer_get_drm_fd(drw)) >= 0 && drw->features.timeline
			&& backend->features.timeline)
		wlr_linux_drm_syncobj_manager_v1_create(dpy, 1, drm_fd);

	if (!(alloc = wlr_allocator_autocreate(backend, drw)))
		die("couldn't create allocator");

	wallpaper_init();

	compositor = wlr_compositor_create(dpy, 6, drw);
	wlr_subcompositor_create(dpy);
	wlr_data_device_manager_create(dpy);
	wlr_export_dmabuf_manager_v1_create(dpy);
	wlr_screencopy_manager_v1_create(dpy);
	wlr_data_control_manager_v1_create(dpy);
	wlr_ext_data_control_manager_v1_create(dpy, 1);
	wlr_primary_selection_v1_device_manager_create(dpy);
	wlr_viewporter_create(dpy);
	wlr_single_pixel_buffer_manager_v1_create(dpy);
	wlr_fractional_scale_manager_v1_create(dpy, 1);
	wlr_presentation_create(dpy, backend, 2);
	wlr_alpha_modifier_v1_create(dpy);

	activation = wlr_xdg_activation_v1_create(dpy);
	wl_signal_add(&activation->events.request_activate, &request_activate);

	wlr_scene_set_gamma_control_manager_v1(scene, wlr_gamma_control_manager_v1_create(dpy));

	power_mgr = wlr_output_power_manager_v1_create(dpy);
	wl_signal_add(&power_mgr->events.set_mode, &output_power_mgr_set_mode);

	output_layout = wlr_output_layout_create(dpy);
	wl_signal_add(&output_layout->events.change, &layout_change);

	wlr_xdg_output_manager_v1_create(dpy, output_layout);

	wl_list_init(&mons);
	wl_list_init(&savedstates);
	wl_signal_add(&backend->events.new_output, &new_output);

	wl_list_init(&clients);
	wl_list_init(&fstack);
	wl_list_init(&popup_zoom_list);

	xdg_shell = wlr_xdg_shell_create(dpy, 6);
	wl_signal_add(&xdg_shell->events.new_toplevel, &new_xdg_toplevel);
	wl_signal_add(&xdg_shell->events.new_popup, &new_xdg_popup);

	layer_shell = wlr_layer_shell_v1_create(dpy, 4);
	wl_signal_add(&layer_shell->events.new_surface, &new_layer_surface);

	idle_notifier = wlr_idle_notifier_v1_create(dpy);

	idle_inhibit_mgr = wlr_idle_inhibit_v1_create(dpy);
	wl_signal_add(&idle_inhibit_mgr->events.new_inhibitor, &new_idle_inhibitor);

	session_lock_mgr = wlr_session_lock_manager_v1_create(dpy);
	wl_signal_add(&session_lock_mgr->events.new_lock, &new_session_lock);
	locked_bg = wlr_scene_rect_create(layers[LyrBlock], sgeom.width, sgeom.height,
			(float [4]){0.1f, 0.1f, 0.1f, 1.0f});
	wlr_scene_node_set_enabled(&locked_bg->node, 0);

	wlr_server_decoration_manager_set_default_mode(
			wlr_server_decoration_manager_create(dpy),
			WLR_SERVER_DECORATION_MANAGER_MODE_SERVER);
	xdg_decoration_mgr = wlr_xdg_decoration_manager_v1_create(dpy);
	wl_signal_add(&xdg_decoration_mgr->events.new_toplevel_decoration, &new_xdg_decoration);

	pointer_constraints = wlr_pointer_constraints_v1_create(dpy);
	pointer_gestures = wlr_pointer_gestures_v1_create(dpy);
	wl_signal_add(&pointer_constraints->events.new_constraint, &new_pointer_constraint);

	relative_pointer_mgr = wlr_relative_pointer_manager_v1_create(dpy);

	tablet_mgr = wlr_tablet_v2_create(dpy);

	cursor = wlr_cursor_create();
	wlr_cursor_attach_output_layout(cursor, output_layout);

	cursor_mgr = wlr_xcursor_manager_create(getenv("XCURSOR_THEME"),
			getenv("XCURSOR_SIZE") ? atoi(getenv("XCURSOR_SIZE")) : 24);
	setenv("XCURSOR_SIZE", getenv("XCURSOR_SIZE") ? getenv("XCURSOR_SIZE") : "24", 1);

	wl_signal_add(&cursor->events.motion, &cursor_motion);
	wl_signal_add(&cursor->events.motion_absolute, &cursor_motion_absolute);
	wl_signal_add(&cursor->events.button, &cursor_button);
	wl_signal_add(&cursor->events.axis, &cursor_axis);
	wl_signal_add(&cursor->events.frame, &cursor_frame);

	wl_signal_add(&cursor->events.tablet_tool_proximity, &tablet_tool_proximity);
	wl_signal_add(&cursor->events.tablet_tool_axis, &tablet_tool_axis);
	wl_signal_add(&cursor->events.tablet_tool_button, &tablet_tool_button);
	wl_signal_add(&cursor->events.tablet_tool_tip, &tablet_tool_tip);

	cursor_shape_mgr = wlr_cursor_shape_manager_v1_create(dpy, 1);
	wl_signal_add(&cursor_shape_mgr->events.request_set_shape, &request_set_cursor_shape);

	wl_signal_add(&backend->events.new_input, &new_input_device);
	virtual_keyboard_mgr = wlr_virtual_keyboard_manager_v1_create(dpy);
	wl_signal_add(&virtual_keyboard_mgr->events.new_virtual_keyboard,
			&new_virtual_keyboard);
	virtual_pointer_mgr = wlr_virtual_pointer_manager_v1_create(dpy);
	wl_signal_add(&virtual_pointer_mgr->events.new_virtual_pointer,
			&new_virtual_pointer);

	seat = wlr_seat_create(dpy, "seat0");
	wl_signal_add(&seat->events.request_set_cursor, &request_cursor);
	wl_signal_add(&seat->events.request_set_selection, &request_set_sel);
	wl_signal_add(&seat->events.request_set_primary_selection, &request_set_psel);
	wl_signal_add(&seat->events.request_start_drag, &request_start_drag);
	wl_signal_add(&seat->events.start_drag, &start_drag);

	kb_group = createkeyboardgroup();
	wl_list_init(&kb_group->destroy.link);

	output_mgr = wlr_output_manager_v1_create(dpy);
	wl_signal_add(&output_mgr->events.apply, &output_mgr_apply);
	wl_signal_add(&output_mgr->events.test, &output_mgr_test);

	unsetenv("DISPLAY");
#ifdef XWAYLAND
	if ((xwayland = wlr_xwayland_create(dpy, compositor, 1))) {
		wl_signal_add(&xwayland->events.ready, &xwayland_ready);
		wl_signal_add(&xwayland->events.new_surface, &new_xwayland_surface);

		setenv("DISPLAY", xwayland->display_name, 1);
	} else {
		fprintf(stderr, "failed to setup XWayland X server, continuing without it\n");
	}
#endif

	bar_init();
}

void
spawn(const Arg *arg)
{
	if (fork() == 0) {
		dup2(STDERR_FILENO, STDOUT_FILENO);
		setsid();
		execvp(((char **)arg->v)[0], (char **)arg->v);
		die("lilith: execvp %s failed:", ((char **)arg->v)[0]);
	}
}

void
startdrag(struct wl_listener *listener, void *data)
{
	struct wlr_drag *drag = data;
	if (!drag->icon)
		return;

	drag->icon->data = &wlr_scene_drag_icon_create(drag_icon, drag->icon)->node;
	LISTEN_STATIC(&drag->icon->events.destroy, destroydragicon);
}

void
tag(const Arg *arg)
{
	Client *sel = focustop(selmon);
	if (!sel || (arg->ui & TAGMASK) == 0)
		return;

	sel->tags = arg->ui & TAGMASK;
	if (selmon) {
		int _i;
		for (_i = 0; _i < TAGCOUNT; _i++) {
			if (arg->ui & (1u << _i)) {
#if TAG_KEEP_VIEW
				{
					int _t = getcurrenttag(selmon);
					sel->saved_cx = selmon->canvas[_t].cx + (selmon->w.width  - sel->geom.width)  / 2;
					sel->saved_cy = selmon->canvas[_t].cy + (selmon->w.y - selmon->m.y) + (selmon->w.height - sel->geom.height) / 2;
				}
#else
				sel->saved_cx = selmon->canvas[_i].cx + (selmon->w.width  - sel->geom.width)  / 2;
				sel->saved_cy = selmon->canvas[_i].cy + (selmon->w.y - selmon->m.y) + (selmon->w.height - sel->geom.height) / 2;
#endif
				sel->saved_cw = sel->geom.width;
				sel->saved_ch = sel->geom.height;
				sel->was_on_canvas = 1;
				break;
			}
		}
	}
	focusclient(focustop(selmon), 1);
	arrange(selmon);
	printstatus();
}

void
tagmon(const Arg *arg)
{
	Client *sel = focustop(selmon);
	if (sel)
		setmon(sel, dirtomon(arg->i), 0);
}

#ifndef MAGNET_SNAP_DIST
#define MAGNET_SNAP_DIST 12
#endif

#ifndef ALIGN_SNAP_DIST
#define ALIGN_SNAP_DIST 60
#endif

static void
snap_edges(Client *c, Monitor *m, int *nx, int *ny)
{
	Client *o, *xpartner = NULL;
	struct wlr_box cand;
	int best_dx = 0, best_dy = 0, have_dx = 0, have_dy = 0;
	int thresh, align_tol;

	if (!c || !m)
		return;

	thresh = MAGNET_SNAP_DIST;
	if (m->zoom_scale > 1e-9)
		thresh = (int)llround(MAGNET_SNAP_DIST / m->zoom_scale);
	align_tol = ALIGN_SNAP_DIST;
	if (m->zoom_scale > 1e-9)
		align_tol = (int)llround(ALIGN_SNAP_DIST / m->zoom_scale);

	cand.x = *nx;
	cand.y = *ny;
	cand.width = c->geom.width;
	cand.height = c->geom.height;

	{
		struct wlr_box *w = &m->w;
		int d;
		d = abs(cand.x - w->x);
		if (d <= thresh && (!have_dx || d < abs(best_dx))) { best_dx = w->x - cand.x; have_dx = 1; }
		d = abs((cand.x + cand.width) - (w->x + w->width));
		if (d <= thresh && (!have_dx || d < abs(best_dx))) { best_dx = (w->x + w->width) - (cand.x + cand.width); have_dx = 1; }
		d = abs(cand.y - w->y);
		if (d <= thresh && (!have_dy || d < abs(best_dy))) { best_dy = w->y - cand.y; have_dy = 1; }
		d = abs((cand.y + cand.height) - (w->y + w->height));
		if (d <= thresh && (!have_dy || d < abs(best_dy))) { best_dy = (w->y + w->height) - (cand.y + cand.height); have_dy = 1; }
	}

	wl_list_for_each(o, &clients, link) {
		int d;
		if (o == c || !VISIBLEON(o, m))
			continue;

		if (cand.y < o->geom.y + o->geom.height && o->geom.y < cand.y + cand.height) {
			d = abs(cand.x - o->geom.x);
			if (d <= thresh && (!have_dx || d < abs(best_dx))) { best_dx = o->geom.x - cand.x; have_dx = 1; xpartner = o; }
			d = abs(cand.x - (o->geom.x + o->geom.width + WINDOW_GAP));
			if (d <= thresh && (!have_dx || d < abs(best_dx))) { best_dx = (o->geom.x + o->geom.width + WINDOW_GAP) - cand.x; have_dx = 1; xpartner = o; }
			d = abs((cand.x + cand.width) - (o->geom.x + o->geom.width));
			if (d <= thresh && (!have_dx || d < abs(best_dx))) { best_dx = (o->geom.x + o->geom.width) - (cand.x + cand.width); have_dx = 1; xpartner = o; }
			d = abs((cand.x + cand.width + WINDOW_GAP) - o->geom.x);
			if (d <= thresh && (!have_dx || d < abs(best_dx))) { best_dx = o->geom.x - WINDOW_GAP - (cand.x + cand.width); have_dx = 1; xpartner = o; }
		}

		if (cand.x < o->geom.x + o->geom.width && o->geom.x < cand.x + cand.width) {
			d = abs(cand.y - o->geom.y);
			if (d <= thresh && (!have_dy || d < abs(best_dy))) { best_dy = o->geom.y - cand.y; have_dy = 1; }
			d = abs(cand.y - (o->geom.y + o->geom.height + WINDOW_GAP));
			if (d <= thresh && (!have_dy || d < abs(best_dy))) { best_dy = (o->geom.y + o->geom.height + WINDOW_GAP) - cand.y; have_dy = 1; }
			d = abs((cand.y + cand.height) - (o->geom.y + o->geom.height));
			if (d <= thresh && (!have_dy || d < abs(best_dy))) { best_dy = (o->geom.y + o->geom.height) - (cand.y + cand.height); have_dy = 1; }
			d = abs((cand.y + cand.height + WINDOW_GAP) - o->geom.y);
			if (d <= thresh && (!have_dy || d < abs(best_dy))) { best_dy = o->geom.y - WINDOW_GAP - (cand.y + cand.height); have_dy = 1; }
		}
	}

	if (have_dx && xpartner && !have_dy) {
		int top_d = abs(cand.y - xpartner->geom.y);
		int bot_d = abs((cand.y + cand.height) - (xpartner->geom.y + xpartner->geom.height));
		if (top_d <= align_tol || bot_d <= align_tol) {
			if (top_d <= bot_d)
				best_dy = xpartner->geom.y - cand.y;
			else
				best_dy = (xpartner->geom.y + xpartner->geom.height) - (cand.y + cand.height);
			have_dy = 1;
		}
	}

	*nx += best_dx;
	*ny += best_dy;
}

#ifndef RESIZE_MAGNET_SNAP_DIST
#define RESIZE_MAGNET_SNAP_DIST 48
#endif
#ifndef RESIZE_MAGNET_RELEASE_DIST
#define RESIZE_MAGNET_RELEASE_DIST 80
#endif

static void
resize_magnetically(Client *c, Monitor *m, struct wlr_box *geo)
{
	Client *o, *right = NULL, *below = NULL;
	int minw, minh, thresh, release;
	int right_edge, bottom_edge;
	int x_best = 0, y_best = 0;
	int xdist = 0x3fffffff, ydist = 0x3fffffff;
	int cluster_left, cluster_right, cluster_top, cluster_bottom;
	int balance_x, balance_y;
	int proposed_x, proposed_y;

	if (!c || !m || !geo)
		return;

	thresh = RESIZE_MAGNET_SNAP_DIST;
	release = RESIZE_MAGNET_RELEASE_DIST;
	if (m->zoom_scale > 1e-9) {
		thresh = (int)llround((double)RESIZE_MAGNET_SNAP_DIST / m->zoom_scale);
		release = (int)llround((double)RESIZE_MAGNET_RELEASE_DIST / m->zoom_scale);
	}

	minw = 1 + 2 * (int)c->bw;
	minh = 1 + 2 * (int)c->bw;

	wl_list_for_each(o, &clients, link) {
		int gap, overlap;
		if (o == c || o->mon != m || !VISIBLEON(o, m) || o->isfullscreen)
			continue;

		overlap = MIN(c->geom.y + c->geom.height, o->geom.y + o->geom.height)
			- MAX(c->geom.y, o->geom.y);
		if (overlap <= 0)
			continue;

		gap = abs((c->geom.x + c->geom.width + WINDOW_GAP) - o->geom.x);
		if (gap <= MAGNET_SNAP_DIST && o->geom.x >= c->geom.x + minw
				&& gap < xdist) {
			right = o;
			xdist = gap;
		}
	}

	wl_list_for_each(o, &clients, link) {
		int gap, overlap;
		if (o == c || o->mon != m || !VISIBLEON(o, m) || o->isfullscreen)
			continue;

		overlap = MIN(c->geom.x + c->geom.width, o->geom.x + o->geom.width)
			- MAX(c->geom.x, o->geom.x);
		if (overlap <= 0)
			continue;

		gap = abs((c->geom.y + c->geom.height + WINDOW_GAP) - o->geom.y);
		if (gap <= MAGNET_SNAP_DIST && o->geom.y >= c->geom.y + minh
				&& gap < ydist) {
			below = o;
			ydist = gap;
		}
	}

	if (right) {
		right_edge = geo->x + geo->width;
		cluster_left = c->geom.x;
		cluster_right = right->geom.x + right->geom.width;
		balance_x = cluster_left + (cluster_right - cluster_left - WINDOW_GAP) / 2;
		proposed_x = right_edge;

		if (resize_snap_x_target) {
			if (abs(proposed_x - resize_snap_x_target) <= release) {
				proposed_x = resize_snap_x_target;
			} else {
				resize_snap_x_target = 0;
			}
		}

		if (!resize_snap_x_target &&
				abs((proposed_x - cluster_left) - right->geom.width) <= thresh) {
			resize_snap_x_target = balance_x;
			proposed_x = balance_x;
		}

		proposed_x = MAX(cluster_left + minw, proposed_x);
		proposed_x = MIN(cluster_right - WINDOW_GAP - (1 + 2 * (int)right->bw), proposed_x);

		if (resize_snap_x_target)
			resize_snap_x_target = proposed_x;

		geo->width = MAX(minw, proposed_x - c->geom.x);

		{
			struct wlr_box other = right->geom;
			other.x = proposed_x + WINDOW_GAP;
			other.width = cluster_right - proposed_x - WINDOW_GAP;
			if (other.width >= 1 + 2 * (int)right->bw)
				resize(right, other, 1);
		}
	} else {
		resize_snap_x_target = 0;
	}

	if (below) {
		bottom_edge = geo->y + geo->height;
		cluster_top = c->geom.y;
		cluster_bottom = below->geom.y + below->geom.height;
		balance_y = cluster_top + (cluster_bottom - cluster_top - WINDOW_GAP) / 2;
		proposed_y = bottom_edge;

		if (resize_snap_y_target) {
			if (abs(proposed_y - resize_snap_y_target) <= release) {
				proposed_y = resize_snap_y_target;
			} else {
				resize_snap_y_target = 0;
			}
		}

		if (!resize_snap_y_target &&
				abs((proposed_y - cluster_top) - below->geom.height) <= thresh) {
			resize_snap_y_target = balance_y;
			proposed_y = balance_y;
		}

		proposed_y = MAX(cluster_top + minh, proposed_y);
		proposed_y = MIN(cluster_bottom - WINDOW_GAP - (1 + 2 * (int)below->bw), proposed_y);

		if (resize_snap_y_target)
			resize_snap_y_target = proposed_y;

		geo->height = MAX(minh, proposed_y - c->geom.y);

		{
			struct wlr_box other = below->geom;
			other.y = proposed_y + WINDOW_GAP;
			other.height = cluster_bottom - proposed_y - WINDOW_GAP;
			if (other.height >= 1 + 2 * (int)below->bw)
				resize(below, other, 1);
		}
	} else {
		resize_snap_y_target = 0;
	}
}

void
togglefullscreen(const Arg *arg)
{
	Client *sel = focustop(selmon);
	if (sel)
		setfullscreen(sel, !sel->isfullscreen);
}

void
togglefakefullscreen(const Arg *arg)
{
	Client *c = focustop(selmon);

	if (!c || !c->mon || c->isfullscreen)
		return;

	if (!c->isfakefullscreen) {
		c->prev = c->geom;
		c->isfakefullscreen = 1;
		if (!c->isfloating)
			setfloating(c, 1);

		c->bw = innerborderpx + outerborderpx;
		c->ibw = innerborderpx;
		c->obw = outerborderpx;
		resize(c, c->mon->m, 0);
	} else {
		c->isfakefullscreen = 0;
		c->bw = innerborderpx + outerborderpx;
		c->ibw = innerborderpx;
		c->obw = outerborderpx;
		resize(c, c->prev, 0);
	}
	arrange(c->mon);
	printstatus();
}

void
snaptoposition(const Arg *arg)
{
	Client *c = focustop(selmon);
	struct wlr_box *w;
	struct wlr_box left, right, top, bottom;
	struct wlr_box tl, tr, bl, br;
	struct wlr_box box, cur;
	int state = -1;
	int halfw, halfh, rightw, bottomh;

	if (!c || !c->mon || c->isfullscreen || c->isfakefullscreen)
		return;
	if (!c->isfloating)
		setfloating(c, 1);

	w = &c->mon->w;

	halfw = w->width / 2;
	halfh = w->height / 2;
	rightw = w->width - halfw;
	bottomh = w->height - halfh;

	left   = (struct wlr_box){w->x, w->y, halfw, w->height};
	right  = (struct wlr_box){w->x + halfw, w->y, rightw, w->height};
	top    = (struct wlr_box){w->x, w->y, w->width, halfh};
	bottom = (struct wlr_box){w->x, w->y + halfh, w->width, bottomh};
	tl     = (struct wlr_box){w->x, w->y, halfw, halfh};
	tr     = (struct wlr_box){w->x + halfw, w->y, rightw, halfh};
	bl     = (struct wlr_box){w->x, w->y + halfh, halfw, bottomh};
	br     = (struct wlr_box){w->x + halfw, w->y + halfh, rightw, bottomh};

	cur = c->geom;

	if (cur.x == tl.x && cur.y == tl.y &&
			cur.width == tl.width &&
			cur.height == tl.height)
		state = 1;
	else if (cur.x == top.x && cur.y == top.y &&
			cur.width == top.width &&
			cur.height == top.height)
		state = 2;
	else if (cur.x == tr.x && cur.y == tr.y &&
			cur.width == tr.width &&
			cur.height == tr.height)
		state = 3;
	else if (cur.x == right.x && cur.y == right.y &&
			cur.width == right.width &&
			cur.height == right.height)
		state = 4;
	else if (cur.x == br.x && cur.y == br.y &&
			cur.width == br.width &&
			cur.height == br.height)
		state = 5;
	else if (cur.x == bottom.x && cur.y == bottom.y &&
			cur.width == bottom.width &&
			cur.height == bottom.height)
		state = 6;
	else if (cur.x == bl.x && cur.y == bl.y &&
			cur.width == bl.width &&
			cur.height == bl.height)
		state = 7;
	else if (cur.x == left.x && cur.y == left.y &&
			cur.width == left.width &&
			cur.height == left.height)
		state = 0;
	else if (cur.x == w->x && cur.y == w->y &&
			cur.width == w->width &&
			cur.height == w->height)
		state = 8;

	if (state < 0) {
		switch (arg->i) {
		case SnapLeft:        box = left; break;
		case SnapRight:       box = right; break;
		case SnapTop:         box = top; break;
		case SnapBottom:      box = bottom; break;
		case SnapTopLeft:     box = tl; break;
		case SnapTopRight:    box = tr; break;
		case SnapBottomLeft:  box = bl; break;
		case SnapBottomRight: box = br; break;
		default: return;
		}
		resize(c, box, 0);
		return;
	}

	switch (arg->i) {
	case SnapLeft:
		switch (state) {
		case 2: box = tl; break;
		case 3: box = top; break;
		case 4: box = *w; break;
		case 5: box = bottom; break;
		case 6: box = bl; break;
		case 7: box = br; break;
		case 8: box = left; break;
		default: return;
		}
		break;

	case SnapRight:
		switch (state) {
		case 0: box = *w; break;
		case 1: box = top; break;
		case 2: box = tr; break;
		case 5: box = bl; break;
		case 6: box = br; break;
		case 7: box = bottom; break;
		case 8: box = right; break;
		default: return;
		}
		break;

	case SnapTop:
		switch (state) {
		case 0: box = tl; break;
		case 1: box = top; break;
		case 3: box = top; break;
		case 4: box = tr; break;
		case 5: box = right; break;
		case 6: box = *w; break;
		case 7: box = left; break;
		case 8: box = top; break;
		default: return;
		}
		break;

	case SnapBottom:
		switch (state) {
		case 0: box = bl; break;
		case 1: box = left; break;
		case 2: box = *w; break;
		case 3: box = right; break;
		case 4: box = br; break;
		case 5: box = right; break;
		case 7: box = bottom; break;
		case 8: box = bottom; break;
		default: return;
		}
		break;

	case SnapTopLeft:     box = tl; break;
	case SnapTopRight:    box = tr; break;
	case SnapBottomLeft:  box = bl; break;
	case SnapBottomRight: box = br; break;
	default: return;
	}

	resize(c, box, 0);
}

void
toggletag(const Arg *arg)
{
	uint32_t newtags;
	Client *sel = focustop(selmon);
	if (!sel || !(newtags = sel->tags ^ (arg->ui & TAGMASK)))
		return;

	sel->tags = newtags;
	focusclient(focustop(selmon), 1);
	arrange(selmon);
	printstatus();
}

void
toggleview(const Arg *arg)
{
	uint32_t newtagset;
	if (!(newtagset = selmon ? selmon->tagset[selmon->seltags] ^ (arg->ui & TAGMASK) : 0))
		return;

	selmon->tagset[selmon->seltags] = newtagset;
	focusclient(focustop(selmon), 1);
	arrange(selmon);
	printstatus();
}

void
unlocksession(struct wl_listener *listener, void *data)
{
	SessionLock *lock = wl_container_of(listener, lock, unlock);
	destroylock(lock, 1);
}

void
unmaplayersurfacenotify(struct wl_listener *listener, void *data)
{
	LayerSurface *l = wl_container_of(listener, l, unmap);

	l->mapped = 0;
	wlr_scene_node_set_enabled(&l->scene->node, 0);
	if (l == exclusive_focus)
		exclusive_focus = NULL;
	if (l->layer_surface->output && (l->mon = l->layer_surface->output->data))
		arrangelayers(l->mon);
	if (l->layer_surface->surface == seat->keyboard_state.focused_surface)
		focusclient(focustop(selmon), 1);
	motionnotify(0, NULL, 0, 0, 0, 0);
}

void
unmapnotify(struct wl_listener *listener, void *data)
{
	Client *c = wl_container_of(listener, c, unmap);
	c->inertia = 0;
	if (c == grabc) {
		cursor_mode = CurNormal;
		grabc = NULL;
	}

	openanim_abort(c);
	closeanim_start(c);

	if (client_is_unmanaged(c)) {
		if (c == exclusive_focus) {
			exclusive_focus = NULL;
			focusclient(focustop(selmon), 1);
		}
	} else {
		wl_list_remove(&c->link);
		setmon(c, NULL, 0);
		wl_list_remove(&c->flink);
	}

	if (c == waiting_geo_client)
		waiting_geo_client = NULL;

	wlr_scene_node_destroy(&c->scene->node);
	printstatus();
	motionnotify(0, NULL, 0, 0, 0, 0);
}

void
updatemons(struct wl_listener *listener, void *data)
{
	struct wlr_output_configuration_v1 *config
			= wlr_output_configuration_v1_create();
	Client *c;
	struct wlr_output_configuration_head_v1 *config_head;
	Monitor *m;

	wl_list_for_each(m, &mons, link) {
		if (m->wlr_output->enabled || m->asleep)
			continue;
		config_head = wlr_output_configuration_head_v1_create(config, m->wlr_output);
		config_head->state.enabled = 0;
		wlr_output_layout_remove(output_layout, m->wlr_output);
		closemon(m);
		m->m = m->w = (struct wlr_box){0};
		bar_mon_layout(m);
	}
	wl_list_for_each(m, &mons, link) {
		if (m->wlr_output->enabled
				&& !wlr_output_layout_get(output_layout, m->wlr_output))
			wlr_output_layout_add_auto(output_layout, m->wlr_output);
	}

	wlr_output_layout_get_box(output_layout, NULL, &sgeom);

	wlr_scene_node_set_position(&root_bg->node, sgeom.x, sgeom.y);
	wlr_scene_rect_set_size(root_bg, sgeom.width, sgeom.height);

	wlr_scene_node_set_position(&locked_bg->node, sgeom.x, sgeom.y);
	wlr_scene_rect_set_size(locked_bg, sgeom.width, sgeom.height);

	wl_list_for_each(m, &mons, link) {
		if (!m->wlr_output->enabled)
			continue;
		config_head = wlr_output_configuration_head_v1_create(config, m->wlr_output);

		wlr_output_layout_get_box(output_layout, m->wlr_output, &m->m);
		m->w = m->m;
		wlr_scene_output_set_position(m->scene_output, m->m.x, m->m.y);
		bar_mon_layout(m);

		wlr_scene_node_set_position(&m->fullscreen_bg->node, m->m.x, m->m.y);
		wlr_scene_rect_set_size(m->fullscreen_bg, m->m.width, m->m.height);

		if (m->lock_surface) {
			struct wlr_scene_tree *scene_tree = m->lock_surface->surface->data;
			wlr_scene_node_set_position(&scene_tree->node, m->m.x, m->m.y);
			wlr_session_lock_surface_v1_configure(m->lock_surface, m->m.width, m->m.height);
		}

		arrangelayers(m);
		arrange(m);
		if ((c = focustop(m)) && c->isfullscreen)
			resize(c, m->m, 0);

		m->gamma_lut_changed = 1;

		config_head->state.x = m->m.x;
		config_head->state.y = m->m.y;

		if (!selmon) {
			selmon = m;
		}
	}

	if (selmon && selmon->wlr_output->enabled) {
		wl_list_for_each(c, &clients, link) {
			if (!c->mon && client_surface(c)->mapped)
				setmon(c, selmon, c->tags);
		}
		focusclient(focustop(selmon), 1);
		if (selmon->lock_surface) {
			client_notify_enter(selmon->lock_surface->surface,
					wlr_seat_get_keyboard(seat));
			client_activate_surface(selmon->lock_surface->surface, 1);
		}
	}

	wlr_cursor_move(cursor, NULL, 0, 0);

	wlr_output_manager_v1_set_configuration(output_mgr, config);
}

void
updatetitle(struct wl_listener *listener, void *data)
{
	Client *c = wl_container_of(listener, c, set_title);
	if (c == focustop(c->mon))
		printstatus();
}

void
urgent(struct wl_listener *listener, void *data)
{
	struct wlr_xdg_activation_v1_request_activate_event *event = data;
	Client *c = NULL;
	toplevel_from_wlr_surface(event->surface, &c, NULL);
	if (!c || c == focustop(selmon))
		return;

	c->isurgent = 1;
	printstatus();

	if (client_surface(c)->mapped)
		client_set_border_color(c, urgent_inner_border_color, urgent_outer_border_color);
}

void
view(const Arg *arg)
{
	int tag_dir = 1;
	int keep_cx = 0, keep_cy = 0;

	if (!selmon || (arg->ui & TAGMASK) == selmon->tagset[selmon->seltags])
		return;

	{
		uint32_t ns = (arg->ui & TAGMASK) ? (arg->ui & TAGMASK)
			: selmon->tagset[selmon->seltags ^ 1];
		int oi = getcurrenttag(selmon), ni = 0;
		while (ni < TAGCOUNT - 1 && !(ns & (1u << ni)))
			ni++;
		tag_dir = ni < oi ? -1 : 1;
		tagslide_capture(selmon, tag_dir, ns);
	}

	save_canvas_positions(selmon);
	{
		int _oldtag = getcurrenttag(selmon);
		keep_cx = selmon->canvas[_oldtag].cx;
		keep_cy = selmon->canvas[_oldtag].cy;
	}

	pan_anim_cancel(selmon);

	selmon->seltags ^= 1;
	if (arg->ui & TAGMASK)
		selmon->tagset[selmon->seltags] = arg->ui & TAGMASK;

	{
		int _newtag = getcurrenttag(selmon);
		Client *_c;
#if TAG_KEEP_VIEW
		selmon->canvas[_newtag].saved_cx = keep_cx;
		selmon->canvas[_newtag].saved_cy = keep_cy;
#endif
		restore_canvas_positions(selmon);
		wl_list_for_each(_c, &clients, link)
			if (VISIBLEON(_c, selmon))
				_c->isfloating = 1;
	}

	tagslide_start(selmon, tag_dir);
	focusclient(focustop(selmon), 1);
	arrange(selmon);
	if (selmon->tag_anim)
		zoom_apply(selmon);
	tagopen_start(selmon);
	printstatus();
}

void
virtualkeyboard(struct wl_listener *listener, void *data)
{
	struct wlr_virtual_keyboard_v1 *kb = data;
	KeyboardGroup *group = createkeyboardgroup();
	wlr_keyboard_set_keymap(&kb->keyboard, group->wlr_group->keyboard.keymap);
	LISTEN(&kb->keyboard.base.events.destroy, &group->destroy, destroykeyboardgroup);

	wlr_keyboard_group_add_keyboard(group->wlr_group, &kb->keyboard);
}

void
virtualpointer(struct wl_listener *listener, void *data)
{
	struct wlr_virtual_pointer_v1_new_pointer_event *event = data;
	struct wlr_input_device *device = &event->new_pointer->pointer.base;

	wlr_cursor_attach_input_device(cursor, device);
	if (event->suggested_output)
		wlr_cursor_map_input_to_output(cursor, device, event->suggested_output);
}

Monitor *
xytomon(double x, double y)
{
	struct wlr_output *o = wlr_output_layout_output_at(output_layout, x, y);
	return o ? o->data : NULL;
}

void
xytonode(double x, double y, struct wlr_surface **psurface,
		Client **pc, LayerSurface **pl, double *nx, double *ny)
{
	struct wlr_scene_node *node, *pnode;
	struct wlr_surface *surface = NULL;
	Client *c = NULL;
	LayerSurface *l = NULL;
	int layer;

	bar_hover = NULL;
	for (layer = NUM_LAYERS - 1; !surface && layer >= 0; layer--) {
		if (!(node = wlr_scene_node_at(&layers[layer]->node, x, y, nx, ny)))
			continue;

		if (node->type == WLR_SCENE_NODE_BUFFER) {
			struct wlr_scene_surface *ss = wlr_scene_surface_try_from_buffer(
					wlr_scene_buffer_from_node(node));
			if (!ss) {
				Monitor *bm = locked ? NULL : bar_from_node(node);
				if (bm && bm->bar_hanim_on)
					continue;
				if (bm) {
					bar_hover = bm;
					if (psurface) *psurface = NULL;
					if (pc) *pc = NULL;
					if (pl) *pl = NULL;
					return;
				}
				continue;
			}
			surface = ss->surface;
		}
		for (pnode = node; pnode && !c; pnode = &pnode->parent->node)
			c = pnode->data;
		if (c && c->type == LayerShell) {
			l = (LayerSurface *)c;
			c = NULL;
		}
	}

	if (c && surface && !c->isfullscreen
			&& c->mon && fabs(c->mon->zoom_scale - 1.0) >= 1e-9 && nx && ny) {
		Monitor *m = c->mon;
		double s = m->zoom_scale;
		if (surface == client_surface(c)) {
			int scene_x = (int)llround((double)c->geom.x * s + m->zoom_pan_x);
			int scene_y = (int)llround((double)c->geom.y * s + m->zoom_pan_y);
			int bw_s    = MAX(1, (int)llround((double)c->bw * s));
			int gw_s    = (int)llround((double)c->geom.width  * s);
			int gh_s    = (int)llround((double)c->geom.height * s);
			int inner_w = MAX(1, gw_s - 2 * bw_s);
			int inner_h = MAX(1, gh_s - 2 * bw_s);
			double cw   = (double)MAX(1, c->geom.width  - 2 * c->bw);
			double ch   = (double)MAX(1, c->geom.height - 2 * c->bw);
			double ox   = (double)(scene_x + bw_s);
			double oy   = (double)(scene_y + bw_s);
			*nx = (x - ox) * cw / (double)inner_w;
			*ny = (y - oy) * ch / (double)inner_h;
		} else {
			int abs_nx = 0, abs_ny = 0;
			if (node && wlr_scene_node_coords(node, &abs_nx, &abs_ny)) {
				double cw = (double)MAX(1, c->geom.width  - 2 * c->bw);
				double ch = (double)MAX(1, c->geom.height - 2 * c->bw);
				int gw_s    = (int)llround((double)c->geom.width  * s);
				int gh_s    = (int)llround((double)c->geom.height * s);
				int bw_s    = MAX(1, (int)llround((double)c->bw * s));
				int inner_w = MAX(1, gw_s - 2 * bw_s);
				int inner_h = MAX(1, gh_s - 2 * bw_s);
				*nx = (x - abs_nx) * cw / (double)inner_w;
				*ny = (y - abs_ny) * ch / (double)inner_h;
			}
		}
	}

	if (psurface) *psurface = surface;
	if (pc) *pc = c;
	if (pl) *pl = l;
}

#ifdef XWAYLAND
void
activatex11(struct wl_listener *listener, void *data)
{
	Client *c = wl_container_of(listener, c, activate);

	if (!client_is_unmanaged(c))
		wlr_xwayland_surface_activate(c->surface.xwayland, 1);
}

void
associatex11(struct wl_listener *listener, void *data)
{
	Client *c = wl_container_of(listener, c, associate);

	LISTEN(&client_surface(c)->events.map, &c->map, mapnotify);
	LISTEN(&client_surface(c)->events.unmap, &c->unmap, unmapnotify);
}

void
configurex11(struct wl_listener *listener, void *data)
{
	Client *c = wl_container_of(listener, c, configure);
	struct wlr_xwayland_surface_configure_event *event = data;
	if (!client_surface(c) || !client_surface(c)->mapped) {
		wlr_xwayland_surface_configure(c->surface.xwayland,
				event->x, event->y, event->width, event->height);
		return;
	}
	if (client_is_unmanaged(c)) {
		wlr_scene_node_set_position(&c->scene->node, event->x, event->y);
		wlr_xwayland_surface_configure(c->surface.xwayland,
				event->x, event->y, event->width, event->height);
		return;
	}
	resize(c, (struct wlr_box){.x = event->x - c->bw,
			.y = event->y - c->bw, .width = event->width + c->bw * 2,
			.height = event->height + c->bw * 2}, 0);
}

void
createnotifyx11(struct wl_listener *listener, void *data)
{
	struct wlr_xwayland_surface *xsurface = data;
	Client *c;

	c = xsurface->data = ecalloc(1, sizeof(*c));
	c->surface.xwayland = xsurface;
	c->type = X11;
	c->bw = client_is_unmanaged(c) ? 0 : innerborderpx;

	LISTEN(&xsurface->events.associate, &c->associate, associatex11);
	LISTEN(&xsurface->events.destroy, &c->destroy, destroynotify);
	LISTEN(&xsurface->events.dissociate, &c->dissociate, dissociatex11);
	LISTEN(&xsurface->events.request_activate, &c->activate, activatex11);
	LISTEN(&xsurface->events.request_configure, &c->configure, configurex11);
	LISTEN(&xsurface->events.request_fullscreen, &c->fullscreen, fullscreennotify);
	LISTEN(&xsurface->events.set_hints, &c->set_hints, sethints);
	LISTEN(&xsurface->events.set_title, &c->set_title, updatetitle);
}

void
dissociatex11(struct wl_listener *listener, void *data)
{
	Client *c = wl_container_of(listener, c, dissociate);
	wl_list_remove(&c->map.link);
	wl_list_remove(&c->unmap.link);
}

void
sethints(struct wl_listener *listener, void *data)
{
	Client *c = wl_container_of(listener, c, set_hints);
	struct wlr_surface *surface = client_surface(c);
	if (c == focustop(selmon) || !c->surface.xwayland->hints)
		return;

	c->isurgent = xcb_icccm_wm_hints_get_urgency(c->surface.xwayland->hints);
	printstatus();

	if (c->isurgent && surface && surface->mapped)
		client_set_border_color(c, urgent_inner_border_color, urgent_outer_border_color);
}

void
xwaylandready(struct wl_listener *listener, void *data)
{
	struct wlr_xcursor *xcursor;

	wlr_xwayland_set_seat(xwayland, seat);

	if ((xcursor = wlr_xcursor_manager_get_xcursor(cursor_mgr, "default", 1)))
		wlr_xwayland_set_cursor(xwayland,
				xcursor->images[0]->buffer, xcursor->images[0]->width * 4,
				xcursor->images[0]->width, xcursor->images[0]->height,
				xcursor->images[0]->hotspot_x, xcursor->images[0]->hotspot_y);
}
#endif

int
getcurrenttag(Monitor *m)
{
	unsigned int i;
	for (i = 0; i < (unsigned int)TAGCOUNT && !(m->tagset[m->seltags] & (1u << i)); i++);
	return i < (unsigned int)TAGCOUNT ? (int)i : 0;
}

void
canvas_move_window(Client *c, int x, int y)
{
	if (!c->mon || !client_surface(c)->mapped)
		return;

	c->geom.x = x;
	c->geom.y = y;

	zoom_update_scene_client(c);
}

void
homecanvas(const Arg *arg)
{
	Client *c;
	int tagidx, cx, cy;
	if (!selmon)
		return;

	tagidx = getcurrenttag(selmon);
	cx = selmon->canvas[tagidx].cx;
	cy = selmon->canvas[tagidx].cy;

	wl_list_for_each(c, &clients, link) {
		if (!(c->tags & (1u << tagidx)) || c->mon != selmon)
			continue;
		c->geom.x -= cx;
		c->geom.y -= cy;
	}

	selmon->canvas[tagidx].cx = 0;
	selmon->canvas[tagidx].cy = 0;

	zoom_apply(selmon);
}

#ifndef MOVE_CANVAS_STEP
#define MOVE_CANVAS_STEP 120
#endif

void
movecanvas(const Arg *arg)
{
	Client *c;
	int tagidx, dx, dy;
	if (!selmon)
		return;

	tagidx = getcurrenttag(selmon);
	dx = 0;
	dy = 0;

	switch (arg->i) {
	case 0: dx = -MOVE_CANVAS_STEP; break;
	case 1: dx =  MOVE_CANVAS_STEP; break;
	case 2: dy = -MOVE_CANVAS_STEP; break;
	case 3: dy =  MOVE_CANVAS_STEP; break;
	}

	selmon->canvas[tagidx].cx -= dx;
	selmon->canvas[tagidx].cy -= dy;

	wl_list_for_each(c, &clients, link) {
		if (!VISIBLEON(c, selmon))
			continue;
		c->geom.x -= dx;
		c->geom.y -= dy;
	}
	zoom_apply(selmon);
}

#ifndef WINMOVE_STEP_PX
#define WINMOVE_STEP_PX 5
#endif
#ifndef WINRESIZE_STEP_PX
#define WINRESIZE_STEP_PX 5
#endif

void
movewinbykeys(const Arg *arg)
{
	Client *c = focustop(selmon);
	struct wlr_box geo;

	if (!c || !c->mon || c->isfullscreen || c->isfakefullscreen)
		return;
	if (!c->isfloating)
		setfloating(c, 1);

	geo = c->geom;
	switch (arg->i) {
	case 0: geo.x -= WINMOVE_STEP_PX; break;
	case 1: geo.x += WINMOVE_STEP_PX; break;
	case 2: geo.y -= WINMOVE_STEP_PX; break;
	case 3: geo.y += WINMOVE_STEP_PX; break;
	default: return;
	}
	resize(c, geo, 1);
}

void
resizewinbykeys(const Arg *arg)
{
	Client *c = focustop(selmon);
	struct wlr_box geo;
	int minw, minh;

	if (!c || !c->mon || c->isfullscreen || c->isfakefullscreen)
		return;
	if (!c->isfloating)
		setfloating(c, 1);

	minw = 1 + 2 * (int)c->bw;
	minh = 1 + 2 * (int)c->bw;
	geo = c->geom;
	switch (arg->i) {
	case 0: geo.width = MAX(minw, geo.width - WINRESIZE_STEP_PX); break;
	case 1: geo.width += WINRESIZE_STEP_PX; break;
	case 2: geo.height = MAX(minh, geo.height - WINRESIZE_STEP_PX); break;
	case 3: geo.height += WINRESIZE_STEP_PX; break;
	default: return;
	}
	resize(c, geo, 1);
}

void
movecanvasmouse(const Arg *arg)
{
	if (!selmon)
		return;
	if (cursor_mode != CurNormal && cursor_mode != CurPressed)
		return;

	canvas_pan_multiplier = arg ? arg->f : 1.0f;
	canvas_pan_accum_x = 0.0f;
	canvas_pan_accum_y = 0.0f;

	wlr_cursor_set_xcursor(cursor, cursor_mgr, "all-scroll");
	cursor_mode = CurMove;
	grabc = NULL;
	canvas_panning = 1;
	canvas_pan_last_x = cursor->x;
	canvas_pan_last_y = cursor->y;
	canvas_inertia_grab_begin();
}

void
save_canvas_positions(Monitor *m)
{
	Client *c;
	int tagidx = getcurrenttag(m);

	m->canvas[tagidx].saved_cx = m->canvas[tagidx].cx;
	m->canvas[tagidx].saved_cy = m->canvas[tagidx].cy;

	wl_list_for_each(c, &clients, link) {
		if (!VISIBLEON(c, m))
			continue;
		c->saved_cx = c->geom.x + m->canvas[tagidx].cx;
		c->saved_cy = c->geom.y + m->canvas[tagidx].cy;
		c->saved_cw = c->geom.width;
		c->saved_ch = c->geom.height;
		c->was_on_canvas = 1;
	}
}

void
restore_canvas_positions(Monitor *m)
{
	Client *c;
	int tagidx = getcurrenttag(m);

	m->canvas[tagidx].cx = m->canvas[tagidx].saved_cx;
	m->canvas[tagidx].cy = m->canvas[tagidx].saved_cy;

	wl_list_for_each(c, &clients, link) {
		int tx, ty;

		if (!VISIBLEON(c, m) || !c->was_on_canvas)
			continue;
		c->isfloating = 1;

		tx = c->saved_cx - m->canvas[tagidx].cx;
		ty = c->saved_cy - m->canvas[tagidx].cy;

		c->geom.width  = c->saved_cw;
		c->geom.height = c->saved_ch;

		c->geom.x = tx;
		c->geom.y = ty;

		zoom_update_scene_client(c);

		c->resize = client_set_size(c,
			c->geom.width  - 2 * c->bw,
			c->geom.height - 2 * c->bw);
	}
}

void
centerwindow(const Arg *arg)
{
	Monitor *m;
	Client *tmp;
	int tagidx, screen_cx, screen_cy, win_cx, win_cy, dx, dy;
	Client *c = (arg && arg->v) ? (Client *)arg->v : focustop(selmon);

	if (!c || !c->mon)
		return;

	m = c->mon;
	tagidx = getcurrenttag(m);

	screen_cx = m->w.x + m->w.width  / 2;
	screen_cy = m->w.y + m->w.height / 2;
	win_cx    = c->geom.x + c->geom.width  / 2;
	win_cy    = c->geom.y + c->geom.height / 2;

	dx = screen_cx - win_cx;
	dy = screen_cy - win_cy;

	if (dx == 0 && dy == 0)
		return;
	wl_list_for_each(tmp, &clients, link) {
		if (!VISIBLEON(tmp, m))
			continue;
		tmp->geom.x += dx;
		tmp->geom.y += dy;
	}

	m->canvas[tagidx].cx += dx;
	m->canvas[tagidx].cy += dy;

	zoom_apply(m);
}

double
bezier_ease(const char *spec, double t)
{
	double bx1 = 0, by1 = 0, bx2 = 1, by2 = 1;
	double cx, bx, ax, cy, by, ay, s;
	int i;

	sscanf(spec, "%lf,%lf,%lf,%lf", &bx1, &by1, &bx2, &by2);
	cx = 3.0 * bx1;
	bx = 3.0 * (bx2 - bx1) - cx;
	ax = 1.0 - cx - bx;
	cy = 3.0 * by1;
	by = 3.0 * (by2 - by1) - cy;
	ay = 1.0 - cy - by;

	s = t;
	for (i = 0; i < 8; i++) {
		double xs  = ((ax * s + bx) * s + cx) * s;
		double dxs = (3.0 * ax * s + 2.0 * bx) * s + cx;
		if (fabs(dxs) < 1e-9) break;
		s -= (xs - t) / dxs;
		if (s < 0.0) s = 0.0;
		if (s > 1.0) s = 1.0;
	}
	return ((ay * s + by) * s + cy) * s;
}

void
pan_anim_cancel(Monitor *m)
{
	if (!m->pan_anim && m->pan_off_x == 0.0 && m->pan_off_y == 0.0)
		return;
	m->pan_anim = 0;
	m->pan_off_x = m->pan_off_y = 0.0;
	zoom_apply(m);
}

void
canvas_shift(Monitor *m, int dx, int dy, int animate)
{
	Client *tmp;
	int tagidx = getcurrenttag(m);

	wl_list_for_each(tmp, &clients, link) {
		if (!VISIBLEON(tmp, m))
			continue;
		tmp->geom.x += dx;
		tmp->geom.y += dy;
	}
	m->canvas[tagidx].cx += dx;
	m->canvas[tagidx].cy += dy;

	if (animate && canvasduration > 0.0) {
		m->pan_from_x = m->pan_off_x - dx;
		m->pan_from_y = m->pan_off_y - dy;
		m->pan_off_x  = m->pan_from_x;
		m->pan_off_y  = m->pan_from_y;
		m->pan_start_ms = lilith_monotonic_ms();
		m->pan_anim = 1;
		wlr_output_schedule_frame(m->wlr_output);
	}

	zoom_apply(m);
}

void
pan_anim_step(Monitor *m)
{
	double t, e;

	if (!m->pan_anim)
		return;

	t = (double)(lilith_monotonic_ms() - m->pan_start_ms) / (canvasduration * 1000.0);
	if (t >= 1.0) {
		m->pan_anim = 0;
		m->pan_off_x = m->pan_off_y = 0.0;
	} else {
		e = bezier_ease(canvasbezier, t);
		m->pan_off_x = m->pan_from_x * (1.0 - e);
		m->pan_off_y = m->pan_from_y * (1.0 - e);
	}
	zoom_apply(m);
	if (m->pan_anim)
		wlr_output_schedule_frame(m->wlr_output);
}

typedef struct { int x, y; uint64_t ms; } InertiaSample;
static InertiaSample inertia_samples[32];
static int inertia_ns = 0;

void
inertia_grab_begin(Client *c)
{
	inertia_ns = 0;
	if (c)
		c->inertia = 0;
}

void
inertia_sample(int x, int y)
{
	int n = inertia_ns;

	if (n > 0 && inertia_samples[n - 1].x == x && inertia_samples[n - 1].y == y)
		return;
	if (n == (int)LENGTH(inertia_samples)) {
		memmove(&inertia_samples[0], &inertia_samples[1],
				sizeof(inertia_samples[0]) * (n - 1));
		n--;
	}
	inertia_samples[n].x = x;
	inertia_samples[n].y = y;
	inertia_samples[n].ms = lilith_monotonic_ms();
	inertia_ns = n + 1;
}

static int
inertia_velocity(double *ovx, double *ovy)
{
	uint64_t now;
	int i, first = -1, last = inertia_ns - 1;
	double dt, vx, vy, sp;

	if (inertia_ns < 2)
		return 0;

	now = lilith_monotonic_ms();
	if (now - inertia_samples[last].ms > inertiastalems)
		return 0;

	for (i = 0; i < last; i++) {
		if (now - inertia_samples[i].ms <= inertiawindowms) {
			first = i;
			break;
		}
	}
	if (first < 0)
		return 0;

	dt = (double)(inertia_samples[last].ms - inertia_samples[first].ms) / 1000.0;
	if (dt < 0.008)
		return 0;
	vx = (inertia_samples[last].x - inertia_samples[first].x) / dt * inertiagain;
	vy = (inertia_samples[last].y - inertia_samples[first].y) / dt * inertiagain;

	sp = hypot(vx, vy);
	if (sp < inertiaminstart)
		return 0;
	if (sp > inertiamaxspeed) {
		vx *= inertiamaxspeed / sp;
		vy *= inertiamaxspeed / sp;
	}
	*ovx = vx;
	*ovy = vy;
	return 1;
}

void
inertia_release(Client *c)
{
	double vx, vy;

	if (!animateinertia || !c || !c->mon || !c->isfloating || c->isfullscreen)
		return;
	if (!inertia_velocity(&vx, &vy))
		return;

	c->ivx = vx;
	c->ivy = vy;
	c->irx = c->iry = 0.0;
	c->inertia_ms = lilith_monotonic_ms();
	c->inertia = 1;
	wlr_output_schedule_frame(c->mon->wlr_output);
}

void
inertia_step(Monitor *m)
{
	Client *c;
	uint64_t now = lilith_monotonic_ms();
	int active = 0;

	wl_list_for_each(c, &clients, link) {
		double dt, decay, dx, dy;
		int mx, my;

		if (!c->inertia || c->mon != m)
			continue;
		if (c == grabc || !c->isfloating || c->isfullscreen || !VISIBLEON(c, m)) {
			c->inertia = 0;
			continue;
		}

		dt = (double)(now - c->inertia_ms) / 1000.0;
		c->inertia_ms = now;
		if (dt > 0.05)
			dt = 0.05;

		decay = exp(-dt / inertiatau);
		dx = c->ivx * inertiatau * (1.0 - decay) + c->irx;
		dy = c->ivy * inertiatau * (1.0 - decay) + c->iry;
		c->ivx *= decay;
		c->ivy *= decay;
		mx = (int)dx;
		my = (int)dy;
		c->irx = dx - mx;
		c->iry = dy - my;

		if (hypot(c->ivx, c->ivy) < inertiaminspeed)
			c->inertia = 0;
		if (mx || my)
			canvas_move_window(c, c->geom.x + mx, c->geom.y + my);
		if (c->inertia)
			active = 1;
	}
	if (active)
		wlr_output_schedule_frame(m->wlr_output);
}

static Monitor *cin_mon = NULL;
static int cin_tag, cin_cx, cin_cy;
static double cin_vx, cin_vy, cin_rx, cin_ry;
static uint64_t cin_ms;

void
canvas_inertia_grab_begin(void)
{
	inertia_ns = 0;
	cin_mon = NULL;
}

void
canvas_inertia_release(Monitor *m)
{
	double vx, vy;
	int tagidx;

	if (!animateinertia || !m)
		return;
	if (!inertia_velocity(&vx, &vy))
		return;

	tagidx = getcurrenttag(m);
	cin_mon = m;
	cin_tag = tagidx;
	cin_cx = m->canvas[tagidx].cx;
	cin_cy = m->canvas[tagidx].cy;
	cin_vx = vx;
	cin_vy = vy;
	cin_rx = cin_ry = 0.0;
	cin_ms = lilith_monotonic_ms();
	wlr_output_schedule_frame(m->wlr_output);
}

void
canvas_inertia_step(Monitor *m)
{
	Client *c;
	double dt, decay, dx, dy;
	int mx, my, tagidx;

	if (cin_mon != m)
		return;
	tagidx = getcurrenttag(m);
	if (tagidx != cin_tag
			|| m->canvas[tagidx].cx != cin_cx || m->canvas[tagidx].cy != cin_cy
			|| canvas_panning) {
		cin_mon = NULL;
		return;
	}

	{
		uint64_t now = lilith_monotonic_ms();
		dt = (double)(now - cin_ms) / 1000.0;
		cin_ms = now;
	}
	if (dt > 0.05)
		dt = 0.05;

	decay = exp(-dt / inertiatau);
	dx = cin_vx * inertiatau * (1.0 - decay) + cin_rx;
	dy = cin_vy * inertiatau * (1.0 - decay) + cin_ry;
	cin_vx *= decay;
	cin_vy *= decay;
	mx = (int)dx;
	my = (int)dy;
	cin_rx = dx - mx;
	cin_ry = dy - my;

	if (mx || my) {
		wl_list_for_each(c, &clients, link) {
			if (!VISIBLEON(c, m))
				continue;
			c->geom.x += mx;
			c->geom.y += my;
		}
		m->canvas[tagidx].cx += mx;
		m->canvas[tagidx].cy += my;
		cin_cx = m->canvas[tagidx].cx;
		cin_cy = m->canvas[tagidx].cy;
		zoom_apply(m);
	}

	if (hypot(cin_vx, cin_vy) < inertiaminspeed)
		cin_mon = NULL;
	else
		wlr_output_schedule_frame(m->wlr_output);
}

void
bringintoview(Client *c)
{
	Monitor *m;
	int tagidx, dx, dy;
	struct wlr_box *w, *g;

	if (!c || !c->mon)
		return;

	m = c->mon;
	w = &m->w;
	g = &c->geom;

	if (g->x >= w->x && g->y >= w->y &&
			g->x + g->width <= w->x + w->width &&
			g->y + g->height <= w->y + w->height)
		return;

	tagidx = getcurrenttag(m);

	dx = c->home_cx - m->canvas[tagidx].cx;
	dy = c->home_cy - m->canvas[tagidx].cy;

	if (g->x + dx < w->x || g->y + dy < w->y ||
			g->x + dx + g->width > w->x + w->width ||
			g->y + dy + g->height > w->y + w->height) {
		dx = 0;
		dy = 0;
		if (g->x < w->x)
			dx = w->x - g->x;
		else if (g->x + g->width > w->x + w->width)
			dx = (w->x + w->width) - (g->x + g->width);
		if (g->y < w->y)
			dy = w->y - g->y;
		else if (g->y + g->height > w->y + w->height)
			dy = (w->y + w->height) - (g->y + g->height);
	}

	if (dx == 0 && dy == 0)
		return;

	canvas_shift(m, dx, dy, animatecanvas);
}

void
popup_zoom_destroy(struct wl_listener *listener, void *data)
{
	PopupZoomData *pzd = wl_container_of(listener, pzd, destroy);
	wl_list_remove(&pzd->commit.link);
	wl_list_remove(&pzd->destroy.link);
	wl_list_remove(&pzd->link);
	free(pzd);
}

void
popup_zoom_commit(struct wl_listener *listener, void *data)
{
	double s, px, py;
	Client *c;
	Monitor *m;
	PopupZoomData *pzd = wl_container_of(listener, pzd, commit);

	if (!pzd->client || !pzd->client->mon || !pzd->node)
		return;
	if (!pzd->popup->base || !pzd->popup->base->surface->mapped)
		return;

	c = pzd->client;
	m = c->mon;
	s = m->zoom_scale;
	wlr_xdg_popup_get_position(pzd->popup, &px, &py);

	if (fabs(s - 1.0) < 1e-9) {
		wlr_scene_node_set_position(pzd->node, (int)llround(px), (int)llround(py));
		return;
	}

	wlr_scene_node_set_position(pzd->node,
		(int)llround(px * s),
		(int)llround(py * s));
}

void
zoom_scale_buffers(struct wlr_scene_buffer *buf, int sx, int sy, void *data)
{
	Client *c = data;
	Monitor *m;
	struct wlr_scene_surface *ss;
	struct wlr_surface *surf;
	double s;
	int lw, lh;

	(void)sx; (void)sy;

	if (!c || !(m = c->mon) || !buf->buffer)
		return;

	wlr_scene_buffer_set_filter_mode(buf,
			zoomnearest ? WLR_SCALE_FILTER_NEAREST : WLR_SCALE_FILTER_BILINEAR);

	if (c->isfullscreen) {
		wlr_scene_buffer_set_dest_size(buf, 0, 0);
		return;
	}

	s = m->zoom_scale;
	if (fabs(s - 1.0) < 1e-9) {
		wlr_scene_buffer_set_dest_size(buf, 0, 0);
		return;
	}

	ss = wlr_scene_surface_try_from_buffer(buf);
	if (ss && (surf = ss->surface)) {
		if (surf->current.viewport.has_dst) {
			lw = surf->current.viewport.dst_width;
			lh = surf->current.viewport.dst_height;
		} else {
			lw = surf->current.width;
			lh = surf->current.height;
		}
		if (lw <= 0 || lh <= 0) {
			lw = buf->buffer->width;
			lh = buf->buffer->height;
		}
	} else {
		lw = buf->buffer->width;
		lh = buf->buffer->height;
	}

	{
		int _bw     = (int)llround((double)c->bw * s);
		int gw      = (int)llround((double)c->geom.width  * s);
		int gh      = (int)llround((double)c->geom.height * s);
		int inner_w = MAX(1, gw - 2 * _bw);
		int inner_h = MAX(1, gh - 2 * _bw);

		if (ss && ss->surface == client_surface(c)) {
			wlr_scene_buffer_set_dest_size(buf, inner_w, inner_h);
			return;
		}

		{
			double cw = (double)MAX(1, c->geom.width  - 2 * c->bw);
			double ch = (double)MAX(1, c->geom.height - 2 * c->bw);
			wlr_scene_buffer_set_dest_size(buf,
				MAX(1, (int)llround((double)lw * (double)inner_w / cw)),
				MAX(1, (int)llround((double)lh * (double)inner_h / ch)));
		}
	}
}

void
zoom_update_scene_client(Client *c)
{
	Monitor *m = c->mon;
	double s;
	int gw, gh, w, h, ib, ob, ib_s, ob_s, bw_pos;

	if (!m || !client_surface(c)->mapped)
		return;

	s  = m->zoom_scale;
	w  = c->geom.width;
	h  = c->geom.height;
	ib = c->ibw;
	ob = c->obw;

	if (c->isfullscreen) {
		wlr_scene_node_set_position(&c->scene->node, c->geom.x, c->geom.y);
		wlr_scene_node_set_position(&c->scene_surface->node, c->bw, c->bw);
		wlr_scene_rect_set_size(c->inner_border[0], MAX(1, w - 2*ob), MAX(1, ib));
		wlr_scene_node_set_position(&c->inner_border[0]->node, ob, ob);
		wlr_scene_rect_set_size(c->inner_border[1], MAX(1, w - 2*ob), MAX(1, ib));
		wlr_scene_node_set_position(&c->inner_border[1]->node, ob, MAX(0, h - ob - ib));
		wlr_scene_rect_set_size(c->inner_border[2], MAX(1, ib), MAX(1, h - 2*ob));
		wlr_scene_node_set_position(&c->inner_border[2]->node, ob, ob);
		wlr_scene_rect_set_size(c->inner_border[3], MAX(1, ib), MAX(1, h - 2*ob));
		wlr_scene_node_set_position(&c->inner_border[3]->node, MAX(0, w - ob - ib), ob);
		wlr_scene_rect_set_size(c->outer_border[0], MAX(1, w), MAX(1, ob));
		wlr_scene_node_set_position(&c->outer_border[0]->node, 0, 0);
		wlr_scene_rect_set_size(c->outer_border[1], MAX(1, w), MAX(1, ob));
		wlr_scene_node_set_position(&c->outer_border[1]->node, 0, MAX(0, h - ob));
		wlr_scene_rect_set_size(c->outer_border[2], MAX(1, ob), MAX(1, h - 2*ob));
		wlr_scene_node_set_position(&c->outer_border[2]->node, 0, ob);
		wlr_scene_rect_set_size(c->outer_border[3], MAX(1, ob), MAX(1, h - 2*ob));
		wlr_scene_node_set_position(&c->outer_border[3]->node, MAX(0, w - ob), ob);
		return;
	}

	gw     = MAX(1, (int)llround((double)w  * s));
	gh     = MAX(1, (int)llround((double)h  * s));
	bw_pos = (int)llround((double)c->bw * s);
	ib_s   = (int)llround((double)ib * s);
	ob_s   = (int)llround((double)ob * s);

	wlr_scene_node_set_position(&c->scene->node,
			(int)llround(((double)c->geom.x + m->pan_off_x) * s + m->zoom_pan_x + m->tag_off_x),
			(int)llround(((double)c->geom.y + m->pan_off_y) * s + m->zoom_pan_y));
	wlr_scene_node_set_position(&c->scene_surface->node, bw_pos, bw_pos);

	wlr_scene_rect_set_size(c->inner_border[0], MAX(0, gw - 2*ob_s), ib_s);
	wlr_scene_node_set_position(&c->inner_border[0]->node, ob_s, ob_s);
	wlr_scene_rect_set_size(c->inner_border[1], MAX(1, gw - 2*ob_s), ib_s);
	wlr_scene_node_set_position(&c->inner_border[1]->node, ob_s, MAX(0, gh - ob_s - ib_s));
	wlr_scene_rect_set_size(c->inner_border[2], ib_s, MAX(0, gh - 2*ob_s));
	wlr_scene_node_set_position(&c->inner_border[2]->node, ob_s, ob_s);
	wlr_scene_rect_set_size(c->inner_border[3], ib_s, MAX(1, gh - 2*ob_s));
	wlr_scene_node_set_position(&c->inner_border[3]->node, MAX(0, gw - ob_s - ib_s), ob_s);
	wlr_scene_rect_set_size(c->outer_border[0], gw, ob_s);
	wlr_scene_node_set_position(&c->outer_border[0]->node, 0, 0);
	wlr_scene_rect_set_size(c->outer_border[1], gw, ob_s);
	wlr_scene_node_set_position(&c->outer_border[1]->node, 0, MAX(0, gh - ob_s));
	wlr_scene_rect_set_size(c->outer_border[2], ob_s, MAX(0, gh - 2*ob_s));
	wlr_scene_node_set_position(&c->outer_border[2]->node, 0, ob_s);
	wlr_scene_rect_set_size(c->outer_border[3], ob_s, MAX(1, gh - 2*ob_s));
	wlr_scene_node_set_position(&c->outer_border[3]->node, MAX(0, gw - ob_s), ob_s);

	if (fabs(s - 1.0) >= 1e-9)
		wlr_scene_node_for_each_buffer(&c->scene->node, zoom_scale_buffers, c);
}

void
zoom_apply(Monitor *m)
{
	Client *c;
	PopupZoomData *pzd;

	wallpaper_update(m);

	wl_list_for_each(c, &clients, link) {
		if (c->mon == m && client_surface(c)->mapped)
			zoom_update_scene_client(c);
	}

	wl_list_for_each(pzd, &popup_zoom_list, link) {
		if (pzd->client && pzd->client->mon == m)
			popup_zoom_commit(&pzd->commit, NULL);
	}

	if (m->zoom_popups_dirty && m->zoom_anim_t >= 1.0) {
		double inv_s = 1.0 / m->zoom_scale;
		double vis_x = -m->zoom_pan_x * inv_s;
		double vis_y = -m->zoom_pan_y * inv_s;
		double vis_w = (double)m->m.width  * inv_s;
		double vis_h = (double)m->m.height * inv_s;
		wl_list_for_each(pzd, &popup_zoom_list, link) {
			Client *pc;
			struct wlr_box pbox;
			if (!pzd->client || pzd->client->mon != m)
				continue;
			pc = pzd->client;
			pbox.x      = (int)llround(vis_x) - pc->geom.x;
			pbox.y      = (int)llround(vis_y) - pc->geom.y;
			pbox.width  = MAX(1, (int)llround(vis_w));
			pbox.height = MAX(1, (int)llround(vis_h));
			wlr_xdg_popup_unconstrain_from_box(pzd->popup, &pbox);
		}
		m->zoom_popups_dirty = 0;
	}
}

void
zoom_frame(Monitor *m)
{
	double t, u, target_s, end_pan_x, end_pan_y;

	if (m->zoom_anim_t >= 1.0)
		return;

	target_s  = m->zoom_target;
	end_pan_x = m->zoom_anchor_fx - m->zoom_anchor_lx * target_s;
	end_pan_y = m->zoom_anchor_fy - m->zoom_anchor_ly * target_s;

	m->zoom_anim_t += 1.0 / (zoomduration *
		((double)m->wlr_output->current_mode->refresh / 1000.0));
	if (m->zoom_anim_t > 1.0)
		m->zoom_anim_t = 1.0;

	t = m->zoom_anim_t;

	u = bezier_ease(zoombezier, t);

	if (m->zoom_anim_t >= 1.0) {
		m->zoom_scale = target_s;
		m->zoom_pan_x = end_pan_x;
		m->zoom_pan_y = end_pan_y;
	} else {
		m->zoom_scale = m->zoom_start_scale + (target_s  - m->zoom_start_scale) * u;
		m->zoom_pan_x = m->zoom_start_pan_x + (end_pan_x - m->zoom_start_pan_x) * u;
		m->zoom_pan_y = m->zoom_start_pan_y + (end_pan_y - m->zoom_start_pan_y) * u;
	}

	zoom_apply(m);

	if (m->zoom_anim_t < 1.0)
		wlr_output_schedule_frame(m->wlr_output);
}

void
zoom(const Arg *arg)
{
	double old_s, delta, new_s, fx, fy;
	if (!selmon)
		return;

	old_s = selmon->zoom_scale;
	delta = arg ? arg->f : 0.0;
	new_s = old_s + delta;

	if (new_s < ZOOMMIN) new_s = ZOOMMIN;
	if (new_s > ZOOMMAX) new_s = ZOOMMAX;

	fx = selmon->w.x + selmon->w.width  / 2.0;
	fy = selmon->w.y + selmon->w.height / 2.0;

	selmon->zoom_anchor_fx = fx;
	selmon->zoom_anchor_fy = fy;
	selmon->zoom_anchor_lx = (fx - selmon->zoom_pan_x) / old_s;
	selmon->zoom_anchor_ly = (fy - selmon->zoom_pan_y) / old_s;
	selmon->zoom_target    = new_s;

	if (!animatezoom) {
		selmon->zoom_scale = new_s;
		selmon->zoom_pan_x = fx - selmon->zoom_anchor_lx * new_s;
		selmon->zoom_pan_y = fy - selmon->zoom_anchor_ly * new_s;
		selmon->zoom_anim_t = 1.0;
		selmon->zoom_popups_dirty = 1;
		zoom_apply(selmon);
	} else {
		selmon->zoom_start_scale = old_s;
		selmon->zoom_start_pan_x = selmon->zoom_pan_x;
		selmon->zoom_start_pan_y = selmon->zoom_pan_y;
		selmon->zoom_anim_t = 0.0;
		selmon->zoom_popups_dirty = 1;
		wlr_output_schedule_frame(selmon->wlr_output);
	}
}

#ifndef ZOOMFIT_MARGIN
#define ZOOMFIT_MARGIN 0.92
#endif

void
zoomfit(const Arg *arg)
{
	Client *c;
	Monitor *m = selmon;
	int have = 0, x1 = 0, y1 = 0, x2 = 0, y2 = 0, dx, dy;
	double old_s, bw, bh, new_s, fx, fy, lx, ly;

	if (!m)
		return;

	wl_list_for_each(c, &clients, link) {
		if (!VISIBLEON(c, m) || c->isfullscreen)
			continue;
		if (!have) {
			x1 = c->geom.x;
			y1 = c->geom.y;
			x2 = c->geom.x + c->geom.width;
			y2 = c->geom.y + c->geom.height;
			have = 1;
		} else {
			x1 = MIN(x1, c->geom.x);
			y1 = MIN(y1, c->geom.y);
			x2 = MAX(x2, c->geom.x + c->geom.width);
			y2 = MAX(y2, c->geom.y + c->geom.height);
		}
	}
	if (!have)
		return;

	bw = MAX(1, x2 - x1);
	bh = MAX(1, y2 - y1);
	new_s = MIN((double)m->w.width  * ZOOMFIT_MARGIN / bw,
	            (double)m->w.height * ZOOMFIT_MARGIN / bh);
	if (new_s > 1.0)
		new_s = 1.0;
	if (new_s < ZOOMMIN)
		new_s = ZOOMMIN;
	if (new_s > ZOOMMAX)
		new_s = ZOOMMAX;

	old_s = m->zoom_scale;
	if (fabs(old_s) < 1e-9)
		old_s = 1.0;

	fx = m->w.x + m->w.width  / 2.0;
	fy = m->w.y + m->w.height / 2.0;

	lx = (fx - m->zoom_pan_x) / old_s;
	ly = (fy - m->zoom_pan_y) / old_s;

	dx = (int)llround(lx - (x1 + x2) / 2.0);
	dy = (int)llround(ly - (y1 + y2) / 2.0);
	if (dx != 0 || dy != 0)
		canvas_shift(m, dx, dy, animatecanvas);

	m->zoom_anchor_fx = fx;
	m->zoom_anchor_fy = fy;
	m->zoom_anchor_lx = lx;
	m->zoom_anchor_ly = ly;
	m->zoom_target    = new_s;

	if (!animatezoom) {
		m->zoom_scale = new_s;
		m->zoom_pan_x = fx - lx * new_s;
		m->zoom_pan_y = fy - ly * new_s;
		m->zoom_anim_t = 1.0;
		m->zoom_popups_dirty = 1;
		zoom_apply(m);
	} else {
		m->zoom_start_scale = old_s;
		m->zoom_start_pan_x = m->zoom_pan_x;
		m->zoom_start_pan_y = m->zoom_pan_y;
		m->zoom_anim_t = 0.0;
		m->zoom_popups_dirty = 1;
		wlr_output_schedule_frame(m->wlr_output);
	}
}

void
zoomreset(const Arg *arg)
{
	double old_s, fx, fy;

	if (!selmon)
		return;

	old_s = selmon->zoom_scale;
	fx = selmon->w.x + selmon->w.width  / 2.0;
	fy = selmon->w.y + selmon->w.height / 2.0;

	selmon->zoom_anchor_fx = fx;
	selmon->zoom_anchor_fy = fy;
	selmon->zoom_anchor_lx = fx;
	selmon->zoom_anchor_ly = fy;
	selmon->zoom_target    = 1.0;

	if (!animatezoom) {
		selmon->zoom_scale = 1.0;
		selmon->zoom_pan_x = fx - selmon->zoom_anchor_lx * 1.0;
		selmon->zoom_pan_y = fy - selmon->zoom_anchor_ly * 1.0;
		selmon->zoom_anim_t = 1.0;
		selmon->zoom_popups_dirty = 1;
		zoom_apply(selmon);
	} else {
		selmon->zoom_start_scale = old_s;
		selmon->zoom_start_pan_x = selmon->zoom_pan_x;
		selmon->zoom_start_pan_y = selmon->zoom_pan_y;
		selmon->zoom_anim_t = 0.0;
		selmon->zoom_popups_dirty = 1;
		wlr_output_schedule_frame(selmon->wlr_output);
	}
}

void
spawnregion_update_rect(void)
{
	int x, y, w, h;
	x = (int)MIN(spawnregion_x1, spawnregion_x2);
	y = (int)MIN(spawnregion_y1, spawnregion_y2);
	w = (int)fabs(spawnregion_x2 - spawnregion_x1);
	h = (int)fabs(spawnregion_y2 - spawnregion_y1);
	if (w < 1) w = 1;
	if (h < 1) h = 1;
#if SPAWNREGION_STYLE == 2
	{
		int ob = (int)spawnregion_outerborderpx;
		int ib = (int)spawnregion_innerborderpx;
		int side_h = MAX(0, h - 2 * ib);
		int i;
		if (!spawnregion_border[0]) return;
		for (i = 0; i < 8; i++)
			wlr_scene_node_set_enabled(&spawnregion_border[i]->node, 1);
		wlr_scene_node_set_position(&spawnregion_border[0]->node, x - ob, y - ob);
		wlr_scene_rect_set_size(spawnregion_border[0], w + 2 * ob, ob);
		wlr_scene_node_set_position(&spawnregion_border[1]->node, x - ob, y + h);
		wlr_scene_rect_set_size(spawnregion_border[1], w + 2 * ob, ob);
		wlr_scene_node_set_position(&spawnregion_border[2]->node, x - ob, y);
		wlr_scene_rect_set_size(spawnregion_border[2], ob, h);
		wlr_scene_node_set_position(&spawnregion_border[3]->node, x + w, y);
		wlr_scene_rect_set_size(spawnregion_border[3], ob, h);
		wlr_scene_node_set_position(&spawnregion_border[4]->node, x, y);
		wlr_scene_rect_set_size(spawnregion_border[4], w, ib);
		wlr_scene_node_set_position(&spawnregion_border[5]->node, x, y + h - ib);
		wlr_scene_rect_set_size(spawnregion_border[5], w, ib);
		wlr_scene_node_set_position(&spawnregion_border[6]->node, x, y + ib);
		wlr_scene_rect_set_size(spawnregion_border[6], ib, side_h);
		wlr_scene_node_set_position(&spawnregion_border[7]->node, x + w - ib, y + ib);
		wlr_scene_rect_set_size(spawnregion_border[7], ib, side_h);
	}
#else
	if (!spawnregion_rect)
		return;
	wlr_scene_node_set_position(&spawnregion_rect->node, x, y);
	wlr_scene_rect_set_size(spawnregion_rect, w, h);
#endif
}

void
spawnregion_finish(void)
{
	int x, y, w, h;

	if (spawnregion_rect)
		wlr_scene_node_set_enabled(&spawnregion_rect->node, 0);
#if SPAWNREGION_STYLE == 2
	{
		int i;
		for (i = 0; i < 8; i++)
			if (spawnregion_border[i])
				wlr_scene_node_set_enabled(&spawnregion_border[i]->node, 0);
	}
#endif

	spawnregion_active = 0;
	cursor_mode = CurNormal;
	wlr_cursor_set_xcursor(cursor, cursor_mgr, "default");

	if (selmon) {
		double s = selmon->zoom_scale;
		if (fabs(s) >= 1e-9 && fabs(s - 1.0) >= 1e-9) {
			double lx1 = (spawnregion_x1 - selmon->zoom_pan_x) / s;
			double ly1 = (spawnregion_y1 - selmon->zoom_pan_y) / s;
			double lx2 = (spawnregion_x2 - selmon->zoom_pan_x) / s;
			double ly2 = (spawnregion_y2 - selmon->zoom_pan_y) / s;
			x = (int)MIN(lx1, lx2);
			y = (int)MIN(ly1, ly2);
			w = (int)fabs(lx2 - lx1);
			h = (int)fabs(ly2 - ly1);
			goto coords_done;
		}
	}
	x = (int)MIN(spawnregion_x1, spawnregion_x2);
	y = (int)MIN(spawnregion_y1, spawnregion_y2);
	w = (int)fabs(spawnregion_x2 - spawnregion_x1);
	h = (int)fabs(spawnregion_y2 - spawnregion_y1);
coords_done:

	if (w < 10 || h < 10) {
		if (waiting_geo_client) {
			wlr_scene_node_set_enabled(&waiting_geo_client->scene->node, 1);
			focusclient(waiting_geo_client, 1);
			waiting_geo_client = NULL;
		}
		return;
	}

	pending_spawn_geo.x      = x;
	pending_spawn_geo.y      = y;
	pending_spawn_geo.width  = w;
	pending_spawn_geo.height = h;
	has_pending_spawn_geo    = 1;

	if (waiting_geo_client) {
		if (selmon)
			waiting_geo_client->tags = selmon->tagset[selmon->seltags];
		waiting_geo_client->isfloating = 1;
		resize(waiting_geo_client, pending_spawn_geo, 0);
		wlr_scene_node_set_enabled(&waiting_geo_client->scene->node, 1);
		focusclient(waiting_geo_client, 1);

		waiting_geo_client = NULL;
		has_pending_spawn_geo = 0;
	}
}

void
spawnregion(const Arg *arg)
{
	if (!selmon)
		return;
	if (cursor_mode != CurNormal && cursor_mode != CurPressed)
		return;
		if (!waiting_geo_client)
			return;

	spawnregion_active = 1;
	has_pending_spawn_geo = 0;
	wlr_cursor_set_xcursor(cursor, cursor_mgr, "crosshair");
}

void
createtablet(struct wlr_input_device *device)
{
	if (!tablet) {
		struct libinput_device *device_handle = NULL;
		if (!wlr_input_device_is_libinput(device) ||
				!(device_handle = wlr_libinput_get_device_handle(device)))
			return;
		tablet = wlr_tablet_create(tablet_mgr, seat, device);
		wl_signal_add(&tablet->wlr_device->events.destroy, &tablet_device_destroy);
		if (libinput_device_config_send_events_get_modes(device_handle)) {
			libinput_device_config_send_events_set_mode(device_handle, send_events_mode);
			wlr_cursor_attach_input_device(cursor, device);
		}
	} else if (device == tablet->wlr_device) {
		wlr_log(WLR_ERROR, "createtablet: duplicate device");
	} else {
		wlr_log(WLR_ERROR, "createtablet: already have one tablet");
	}
}

void
destroytablet(struct wl_listener *listener, void *data)
{
	wl_list_remove(&tablet_device_destroy.link);
	wlr_cursor_detach_input_device(cursor, tablet->wlr_device);
	tablet = NULL;
}

void
destroytabletpad(struct wl_listener *listener, void *data)
{
	wl_list_remove(&tablet_pad_button.link);
	wl_list_remove(&tablet_pad_destroy.link);
	tablet_pad = NULL;
}

void
destroytabletsurfacenotify(struct wl_listener *listener, void *data)
{
	if (tablet_curr_surface)
		wl_list_remove(&destroy_tablet_surface_listener.link);
	tablet_curr_surface = NULL;
}

void
destroytablettool(struct wl_listener *listener, void *data)
{
	destroytabletsurfacenotify(NULL, NULL);
	tablet_tool = NULL;
}

void
tabletapplymap(double x, double y, struct wlr_input_device *dev)
{
	Client *p;
	struct wlr_box geom = {0};
	if (tabletmaptosurface && tablet_curr_surface) {
		toplevel_from_wlr_surface(tablet_curr_surface, &p, NULL);
		if (p) {
			for (; client_get_parent(p); p = client_get_parent(p));
			geom.x = p->geom.x + p->bw;
			geom.y = p->geom.y + p->bw;
			geom.width = p->geom.width - 2 * p->bw;
			geom.height = p->geom.height - 2 * p->bw;
			wlr_cursor_map_input_to_region(cursor, dev, &geom);
		}
	}
	wlr_cursor_map_input_to_output(cursor, dev, selmon->wlr_output);
}

void
tabletpadbutton(struct wl_listener *listener, void *data)
{
	struct wlr_tablet_pad_button_event *event = data;
	const TabletPadButton *pb;

	for (pb = tabletpadbuttons; pb < END(tabletpadbuttons); pb++) {
		if (event->button == pb->button
				&& event->state == pb->state
				&& pb->func) {
			pb->func(&pb->arg);
		}
	}

	if (tablet_pad)
		wlr_tablet_v2_tablet_pad_notify_button(tablet_pad,
			event->button, event->time_msec,
			(enum zwp_tablet_pad_v2_button_state)event->state);
}

void
tablettoolmotion(struct wlr_tablet_v2_tablet_tool *tool, bool change_x, bool change_y,
		double x, double y, double dx, double dy)
{
	struct wlr_surface *surface = NULL;
	double sx, sy;

	if (!change_x && !change_y)
		return;

	tabletapplymap(x, y, tablet->wlr_device);

	x = (MAX(tablet_area[0], MIN(tablet_area[0] + tablet_area[2], x)) - tablet_area[0]) / tablet_area[2];
	y = (MAX(tablet_area[1], MIN(tablet_area[1] + tablet_area[3], y)) - tablet_area[1]) / tablet_area[3];

	switch (tablet_tool->wlr_tool->type) {
	case WLR_TABLET_TOOL_TYPE_LENS:
	case WLR_TABLET_TOOL_TYPE_MOUSE:
		wlr_cursor_move(cursor, tablet->wlr_device, dx, dy);
		break;
	default:
		wlr_cursor_warp_absolute(cursor, tablet->wlr_device, change_x ? x : NAN, change_y ? y : NAN);
		break;
	}

	motionnotify(0, NULL, 0, 0, 0, 0);

	xytonode(cursor->x, cursor->y, &surface, NULL, NULL, &sx, &sy);
	if (surface && !wlr_surface_accepts_tablet_v2(surface, tablet))
		surface = NULL;

	if (surface != tablet_curr_surface) {
		if (tablet_curr_surface) {
			if (tablet_tool)
				wlr_tablet_v2_tablet_tool_notify_proximity_out(tablet_tool);
			if (tablet_pad)
				wlr_tablet_v2_tablet_pad_notify_leave(tablet_pad, tablet_curr_surface);
			wl_list_remove(&destroy_tablet_surface_listener.link);
		}
		if (surface) {
			if (tablet_pad)
				wlr_tablet_v2_tablet_pad_notify_enter(tablet_pad, tablet, surface);
			if (tablet_tool)
				wlr_tablet_v2_tablet_tool_notify_proximity_in(tablet_tool, tablet, surface);
			wl_signal_add(&surface->events.destroy, &destroy_tablet_surface_listener);
		}
		tablet_curr_surface = surface;
	}

	if (surface)
		wlr_tablet_v2_tablet_tool_notify_motion(tablet_tool, sx, sy);
}

void
tablettoolproximity(struct wl_listener *listener, void *data)
{
	struct wlr_tablet_tool_proximity_event *event = data;
	struct wlr_tablet_tool *tool = event->tool;

	if (!tablet_tool) {
		tablet_tool = wlr_tablet_tool_create(tablet_mgr, seat, tool);
		wl_signal_add(&tablet_tool->wlr_tool->events.destroy, &tablet_tool_destroy);
		wl_signal_add(&tablet_tool->events.set_cursor, &request_cursor);
	}

	switch (event->state) {
	case WLR_TABLET_TOOL_PROXIMITY_OUT:
		wlr_tablet_v2_tablet_tool_notify_proximity_out(tablet_tool);
		destroytabletsurfacenotify(NULL, NULL);
		break;
	case WLR_TABLET_TOOL_PROXIMITY_IN:
		tablettoolmotion(tablet_tool, true, true, event->x, event->y, 0, 0);
		break;
	}
}

double tilt_x = 0;
double tilt_y = 0;

void
tablettoolaxis(struct wl_listener *listener, void *data)
{
	struct wlr_tablet_tool_axis_event *event = data;

	tablettoolmotion(tablet_tool,
		event->updated_axes & WLR_TABLET_TOOL_AXIS_X,
		event->updated_axes & WLR_TABLET_TOOL_AXIS_Y,
		event->x, event->y, event->dx, event->dy);

	if (event->updated_axes & WLR_TABLET_TOOL_AXIS_PRESSURE)
		wlr_tablet_v2_tablet_tool_notify_pressure(tablet_tool, event->pressure);
	if (event->updated_axes & WLR_TABLET_TOOL_AXIS_DISTANCE)
		wlr_tablet_v2_tablet_tool_notify_distance(tablet_tool, event->distance);
	if (event->updated_axes & WLR_TABLET_TOOL_AXIS_TILT_X)
		tilt_x = event->tilt_x;
	if (event->updated_axes & WLR_TABLET_TOOL_AXIS_TILT_Y)
		tilt_y = event->tilt_y;
	if (event->updated_axes & (WLR_TABLET_TOOL_AXIS_TILT_X | WLR_TABLET_TOOL_AXIS_TILT_Y))
		wlr_tablet_v2_tablet_tool_notify_tilt(tablet_tool, tilt_x, tilt_y);
	if (event->updated_axes & WLR_TABLET_TOOL_AXIS_ROTATION)
		wlr_tablet_v2_tablet_tool_notify_rotation(tablet_tool, event->rotation);
	if (event->updated_axes & WLR_TABLET_TOOL_AXIS_SLIDER)
		wlr_tablet_v2_tablet_tool_notify_slider(tablet_tool, event->slider);
	if (event->updated_axes & WLR_TABLET_TOOL_AXIS_WHEEL)
		wlr_tablet_v2_tablet_tool_notify_wheel(tablet_tool, event->wheel_delta, 0);
}

void
tablettoolbutton(struct wl_listener *listener, void *data)
{
	struct wlr_tablet_tool_button_event *event = data;
	const TabletButton *tb;

	for (tb = tabletbuttons; tb < END(tabletbuttons); tb++) {
		if (event->button == tb->button
				&& event->state == tb->state
				&& tb->func) {
			tb->func(&tb->arg);
		}
	}

	wlr_tablet_v2_tablet_tool_notify_button(tablet_tool, event->button,
		(enum zwp_tablet_pad_v2_button_state)event->state);
}

void
tablettooltip(struct wl_listener *listener, void *data)
{
	struct wlr_tablet_tool_tip_event *event = data;

	if (!tablet_curr_surface) {
		struct wlr_pointer_button_event fakeptrbtnevent = {
			.button = BTN_LEFT,
			.state = event->state == WLR_TABLET_TOOL_TIP_UP ?
				WL_POINTER_BUTTON_STATE_RELEASED : WL_POINTER_BUTTON_STATE_PRESSED,
			.time_msec = event->time_msec,
		};
		buttonpress(NULL, (void *)&fakeptrbtnevent);
	}

	if (event->state == WLR_TABLET_TOOL_TIP_UP) {
		wlr_tablet_v2_tablet_tool_notify_up(tablet_tool);
		return;
	}

	wlr_tablet_v2_tablet_tool_notify_down(tablet_tool);
	wlr_tablet_tool_v2_start_implicit_grab(tablet_tool);
}

int
main(int argc, char *argv[])
{
	char *startup_cmd = NULL;
	int c;

	while ((c = getopt(argc, argv, "s:hdv")) != -1) {
		if (c == 's')
			startup_cmd = optarg;
		else if (c == 'd')
			log_level = WLR_DEBUG;
		else if (c == 'v')
			die("lilith " VERSION);
		else
			goto usage;
	}
	if (optind < argc)
		goto usage;

	if (!getenv("XDG_RUNTIME_DIR"))
		die("XDG_RUNTIME_DIR must be set");
	setup();
	run(startup_cmd);
	cleanup();
	return EXIT_SUCCESS;

usage:
	die("Usage: %s [-v] [-d] [-s startup command]", argv[0]);
}
