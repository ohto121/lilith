/*
 * Main configuration entry point.
 * Settings are split into topic files under config/ and included below.
 */

/* Converts a 0xRRGGBBAA hex value into a float[4] RGBA initializer
 * (each channel 0.0 - 1.0), the format wlroots expects for colors. */
#define COLOR(hex)    { ((hex >> 24) & 0xFF) / 255.0f, \
                        ((hex >> 16) & 0xFF) / 255.0f, \
                        ((hex >> 8) & 0xFF) / 255.0f, \
                        (hex & 0xFF) / 255.0f }

/* Number of tags (workspaces) per monitor. Must match the number of labels
 * in bartags[] (config_bar.h) and the TAGKEYS() bindings (config_keys.h). */
#define TAGCOUNT (9)

/* wlroots log verbosity: WLR_SILENT, WLR_ERROR, WLR_INFO or WLR_DEBUG. */
static int log_level = WLR_SILENT;

/* Split configuration files: */
#include "config/config_appearance.h"  /* borders, colors, wallpaper, spawn region */
#include "config/config_animation.h"  /* tag/open/close/zoom/canvas animations, inertia */
#include "config/config_bar.h"  /* status bar layout, modules, colors */
#include "config/config_input.h"  /* keyboard, touchpad, tablet settings */

/* Per-window rules, applied when a window is mapped. Every matching rule is
 * applied, in order.
 * Fields: { app_id substring (NULL = any), title substring (NULL = any),
 *           tag mask (0 = keep current tags), floating (1/0),
 *           monitor index (-1 = focused monitor) }
 * The single entry below matches every window and keeps the defaults. */
static const Rule rules[] = {
	{ NULL,               NULL,       0,            0,           -1 },
};

/* 1 = place newly opened windows in the center of the monitor's working
 * area; 0 = leave their initial position untouched. */
#define CENTER_NEW_WINDOWS 0

/* Per-output rules, matched by output-name substring (e.g. "eDP-1").
 * Fields: { name (NULL = any output), scale, transform,
 *           x, y (-1 = automatic position),
 *           resx, resy (custom resolution, only used when mode is -1),
 *           refresh rate in Hz (0 = automatic, only used when mode is -1),
 *           mode (index into the output's mode list; -1 = custom mode),
 *           adaptive sync (1 = enabled, 0 = disabled) }
 * The entry below applies to all outputs: scale 1, no rotation, automatic
 * position, first mode of the output, adaptive sync off. */
static const MonitorRule monrules[] = {
	{ NULL, 1, WL_OUTPUT_TRANSFORM_NORMAL, -1, -1, 0, 0, 0.0f, 0, 0 },
};

/* Key and mouse bindings. Included last because the tables refer to handler
 * functions declared in lilith.c. */
#include "config/config_keys.h"
