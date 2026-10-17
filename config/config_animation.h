/*
 * Animation settings.
 * All durations are in seconds. All "bezier" strings are cubic-bezier easing
 * curves given as "x1,y1,x2,y2" (CSS cubic-bezier control points); values
 * above 1.0 on the y axis produce an overshoot.
 */

/* --- Tag (workspace) switching --- */
#define animatetag   1  /* 1 = animate tag switches, 0 = switch instantly */
#define tagstyle     2  /* 0 = whole view slides, 1 = old windows fade out in place, 2 = old windows play the close animation and new ones the open animation */
#define tagduration  0.4  /* length of the tag switch animation */
#define tagbezier    ".28,.67,.28,1.04"  /* easing curve of the tag switch animation */

/* 1 = the canvas view offset carries over when switching tags and windows
 * sent to another tag keep their on-screen position; 0 = every tag restores
 * its own saved view. */
#define TAG_KEEP_VIEW 1

/* --- Inertia: floating windows keep gliding after being flicked with
 *     the mouse and slowly come to rest --- */
#define animateinertia    1  /* 1 = enable inertia, 0 = windows stop as soon as released */
#define inertiatau        0.4  /* decay time constant: larger = longer glide */
#define inertiagain       0.6  /* multiplier applied to the measured release velocity */
#define inertiamaxspeed   2400  /* maximum glide speed (px/s) */
#define inertiaminstart   150  /* release speed (px/s) below which no glide starts */
#define inertiaminspeed   20  /* speed (px/s) below which the glide stops */
#define inertiawindowms   100  /* only pointer samples from the last N ms are used to estimate the release velocity */
#define inertiastalems    100  /* no glide if the pointer was idle for this many ms before release */

/* --- Canvas panning (homecanvas, movecanvas, centering windows, ...) --- */
#define animatecanvas 1  /* 1 = animate canvas movement, 0 = jump instantly */
#define canvasduration 0.7  /* length of the canvas pan animation */
#define canvasbezier ".28,.67,.28,1.04"  /* easing curve of the canvas pan animation */

/* --- Window close animation: the window shrinks and fades out --- */
#define animateclose   1  /* 1 = enabled, 0 = windows vanish instantly */
#define closeduration  0.4  /* animation length */
#define closebezier    ".28,.67,.28,1.04"  /* easing curve */
#define closescale     0.8  /* final size relative to the original (1.0 = no shrink) */
#define closealpha     0.0  /* final opacity (0.0 = fully transparent) */

/* --- Window open animation: the window grows and fades in --- */
#define animateopen   1  /* 1 = enabled, 0 = windows appear instantly */
#define openduration  0.4  /* animation length */
#define openbezier    ".28,.67,.28,1.04"  /* easing curve */
#define openscale     0.8  /* starting size relative to the final size */
#define openalpha     0.0  /* starting opacity (0.0 = fully transparent) */

/* --- Canvas zoom --- */
#define ZOOMMIN 0.3  /* smallest allowed zoom factor (0.3 = 30%); also used to warn when the wallpaper is too small */
#define ZOOMMAX 1  /* largest allowed zoom factor (1 = 100%) */
#define animatezoom 1.5  /* non-zero = animate zoom changes, 0 = instant (only used as an on/off switch) */
#define zoomduration 0.55  /* length of the zoom animation */
#define zoombezier ".22,1.3,.36,1.0"  /* easing curve (y > 1 gives a slight overshoot) */
#define movecanvasmouse_multiplier_is_affected_by_zoom 0  /* 1 = mouse canvas dragging is divided by the zoom factor, 0 = raw pointer distance */
#define ZOOM_IN_TOWARDS_CURSOR 0  /* reserved: not read by the current code (zoom is anchored at the center of the monitor) */
#define ZOOM_OUT_TOWARDS_CURSOR 0  /* reserved: not read by the current code (zoom is anchored at the center of the monitor) */
