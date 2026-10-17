/*
 * Input device configuration.
 */

/* --- Graphics tablet --- */
static const int tabletmaptosurface                = 1;  /* 1 = map the tablet to the window under the pen, 0 = map it to the whole layout */
static const double tablet_area[4] 		   = { 0.4,  0.4,  0.4,   0.4 };  /* used part of the tablet as fractions (0.0 - 1.0) of its surface: { x, y, width, height } */

/* --- Keyboard ---
 * XKB keymap. Other fields (.rules, .model, .variant, .options) can be
 * added; several layouts are comma-separated, e.g. .layout = "us,ru". */
static const struct xkb_rule_names xkb_rules = {
	.layout = "us",
};

static const int repeat_rate = 25;  /* key repeats per second */
static const int repeat_delay = 600;  /* delay in ms before a held key starts repeating */

/* --- Touchpad (libinput) --- */
static const int tap_to_click = 1;  /* 1 = tapping clicks */
static const int tap_and_drag = 1;  /* 1 = tap, then touch and move to drag */
static const int drag_lock = 1;  /* 1 = a drag continues after lifting the finger briefly */
static const int natural_scrolling = 0;  /* 1 = content follows the finger direction */
static const int disable_while_typing = 1;  /* 1 = ignore the touchpad while typing */
static const int left_handed = 0;  /* 1 = swap left and right buttons */
static const int middle_button_emulation = 0;  /* 1 = pressing left and right together acts as middle click */
static const enum libinput_config_scroll_method scroll_method = LIBINPUT_CONFIG_SCROLL_2FG;  /* LIBINPUT_CONFIG_SCROLL_2FG (two-finger), _EDGE or _ON_BUTTON_DOWN */

/* Click method: BUTTON_AREAS (zones at the bottom) or CLICKFINGER (by finger count). */
static const enum libinput_config_click_method click_method = LIBINPUT_CONFIG_CLICK_METHOD_BUTTON_AREAS;

/* Device state: ENABLED, DISABLED, or DISABLED_ON_EXTERNAL_MOUSE. */
static const uint32_t send_events_mode = LIBINPUT_CONFIG_SEND_EVENTS_ENABLED;

/* Pointer acceleration profile: ADAPTIVE (speed-dependent) or FLAT (constant). */
static const enum libinput_config_accel_profile accel_profile = LIBINPUT_CONFIG_ACCEL_PROFILE_ADAPTIVE;
static const double accel_speed = 0;  /* pointer speed from -1.0 (slowest) to 1.0 (fastest) */

/* Tap button mapping: LRM = 1/2/3-finger tap gives left/right/middle, LMR = left/middle/right. */
static const enum libinput_config_tap_button_map button_map = LIBINPUT_CONFIG_TAP_MAP_LRM;
