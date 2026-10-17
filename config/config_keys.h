/*
 * Key and mouse bindings.
 * Binding format: { modifiers, key, function, argument }
 * Note: for keys, the first matching entry in keys[] wins and stops the search.
 */

/* Main modifier: Super (Logo) key. */
#define MODKEY WLR_MODIFIER_LOGO
#define ALT_MODKEY WLR_MODIFIER_ALT  /* secondary modifier: Alt */

/* Generates the four tag bindings for one number key:
 *   MODKEY + KEY                  view tag
 *   MODKEY + Ctrl + KEY           toggle tag visibility in the current view
 *   MODKEY + Shift + SKEY         move the focused window to the tag
 *   MODKEY + Ctrl + Shift + SKEY  toggle the tag on the focused window
 * KEY is the number key, SKEY its shifted symbol (US layout), TAG is 0-based. */
#define TAGKEYS(KEY,SKEY,TAG) \
	{ MODKEY,                    KEY,            view,            {.ui = 1 << TAG} }, \
	{ MODKEY|WLR_MODIFIER_CTRL,  KEY,            toggleview,      {.ui = 1 << TAG} }, \
	{ MODKEY|WLR_MODIFIER_SHIFT, SKEY,           tag,             {.ui = 1 << TAG} }, \
	{ MODKEY|WLR_MODIFIER_CTRL|WLR_MODIFIER_SHIFT,SKEY,toggletag, {.ui = 1 << TAG} }

/* Helper: run a shell command string via /bin/sh -c, e.g. SHCMD("cmd | cmd"). */
#define SHCMD(cmd) { .v = (const char*[]){ "/bin/sh", "-c", cmd, NULL } }

/* Commands started by the bindings below (NULL-terminated argv arrays). */
static const char *termcmd[] = { "foot", NULL };  /* terminal */
static const char *clockcmd[] ={ "foot", "-a", "clock", "tty-clock", "-sc", "-C", "7", NULL };  /* tty-clock (seconds, centered, color 7) in a terminal with app id "clock" */
static const char *wlaunch[] = { "wmenu-run", NULL };  /* application launcher */
static const char *btop[]    = { "foot", "btop", NULL };  /* system monitor in a terminal */
static const char *telegram[]= { "Telegram", NULL };  /* Telegram client */
static const char *browser[] = { "firefox", NULL };  /* web browser */
static const char *volupcmd[]   = { "wpctl", "set-volume", "-l", "1.0", "@DEFAULT_AUDIO_SINK@", "5%+", NULL };  /* volume +5% (capped at 100%) */
static const char *voldowncmd[] = { "wpctl", "set-volume", "@DEFAULT_AUDIO_SINK@", "5%-", NULL };  /* volume -5% */
static const char *volmutecmd[] = { "wpctl", "set-mute", "@DEFAULT_AUDIO_SINK@", "toggle", NULL };  /* toggle mute */
static const char *brupcmd[]    = { "brightnessctl", "set", "10%+", NULL };  /* brightness +10% */
static const char *brdowncmd[]  = { "brightnessctl", "set", "10%-", NULL };  /* brightness -10% */

/* Keyboard bindings. */
static const Key keys[] = {
	{ MODKEY|WLR_MODIFIER_SHIFT, XKB_KEY_f,		  spawn,	    {.v = browser} },  /* Super+Shift+F: open browser (also bound to togglefullscreen below, which is never reached) */
	{ MODKEY|WLR_MODIFIER_SHIFT, XKB_KEY_t,		  spawn,	    {.v = telegram} },  /* Super+Shift+T: open Telegram */
	{ MODKEY|WLR_MODIFIER_SHIFT, XKB_KEY_b,		  spawn,	    {.v = btop} },  /* Super+Shift+B: open btop */
	{ MODKEY|WLR_MODIFIER_SHIFT, XKB_KEY_d,		  spawn,	    {.v = clockcmd} },  /* Super+Shift+D: open clock */
	{ MODKEY,                    XKB_KEY_r,           spawn,            {.v = wlaunch} },  /* Super+R: application launcher */
	{ MODKEY,                    XKB_KEY_z,		  zoomfit,	    {0} },  /* Super+Z: zoom out to fit all windows */
	{ MODKEY,                    XKB_KEY_q,           spawn,            {.v = termcmd} },  /* Super+Q: open terminal */
	{ MODKEY,                    XKB_KEY_Tab,         focusstackcenter,       {.i = +1} },  /* Super+Tab: focus next window and center the view on it */
	{ MODKEY,                    XKB_KEY_k,           focusstackcenter,       {.i = -1} },  /* Super+K: focus previous window and center the view on it */
	{ MODKEY,                    XKB_KEY_c,           killclient,       {0} },  /* Super+C: close focused window */
	{ MODKEY|WLR_MODIFIER_SHIFT, XKB_KEY_f,           togglefullscreen, {0} },  /* Super+Shift+F: toggle fullscreen (shadowed by the browser binding above) */
	{ MODKEY,                    XKB_KEY_0,           view,             {.ui = ~0} },  /* Super+0: show all tags */
	{ MODKEY|WLR_MODIFIER_SHIFT, XKB_KEY_parenright,  tag,              {.ui = ~0} },  /* Super+Shift+0: show the focused window on all tags */
	{ MODKEY,                    XKB_KEY_comma,       focusmon,         {.i = WLR_DIRECTION_LEFT} },  /* Super+,: focus the monitor to the left */
	{ MODKEY,                    XKB_KEY_period,      focusmon,         {.i = WLR_DIRECTION_RIGHT} },  /* Super+.: focus the monitor to the right */
	{ MODKEY|WLR_MODIFIER_SHIFT, XKB_KEY_less,        tagmon,           {.i = WLR_DIRECTION_LEFT} },  /* Super+Shift+,: send window to the monitor on the left */
	{ MODKEY|WLR_MODIFIER_SHIFT, XKB_KEY_greater,     tagmon,           {.i = WLR_DIRECTION_RIGHT} },  /* Super+Shift+.: send window to the monitor on the right */
	/* Super+1..9 and variants: tag bindings (see TAGKEYS above). */
	TAGKEYS(          XKB_KEY_1, XKB_KEY_exclam,                        0),
	TAGKEYS(          XKB_KEY_2, XKB_KEY_at,                            1),
	TAGKEYS(          XKB_KEY_3, XKB_KEY_numbersign,                    2),
	TAGKEYS(          XKB_KEY_4, XKB_KEY_dollar,                        3),
	TAGKEYS(          XKB_KEY_5, XKB_KEY_percent,                       4),
	TAGKEYS(          XKB_KEY_6, XKB_KEY_asciicircum,                   5),
	TAGKEYS(          XKB_KEY_7, XKB_KEY_ampersand,                     6),
	TAGKEYS(          XKB_KEY_8, XKB_KEY_asterisk,                      7),
	TAGKEYS(          XKB_KEY_9, XKB_KEY_parenleft,                     8),
	{ MODKEY|WLR_MODIFIER_SHIFT, XKB_KEY_q,           quit,             {0} },  /* Super+Shift+Q: quit the compositor */
	{ MODKEY,                    XKB_KEY_b,           togglebar,        {0} },  /* Super+B: show/hide the bar */
	{ MODKEY|WLR_MODIFIER_CTRL,  XKB_KEY_b,           barcyclepos,      {.i = +1} },  /* Super+Ctrl+B: move the bar to the next screen edge */
	{ MODKEY|WLR_MODIFIER_CTRL|WLR_MODIFIER_SHIFT, XKB_KEY_b, barcyclepos, {.i = -1} },  /* Super+Ctrl+Shift+B: move the bar to the previous screen edge */
	{ MODKEY|ALT_MODKEY,         XKB_KEY_b,           togglebarfloat,   {0} },  /* Super+Alt+B: toggle between reserved space and floating bar */
	{ MODKEY|ALT_MODKEY|WLR_MODIFIER_SHIFT, XKB_KEY_b, togglebarlayer,  {0} },  /* Super+Alt+Shift+B: toggle the bar above/below windows */

	/* Canvas navigation: focus by direction (0 = up, 1 = left, 2 = down, 3 = right). */
	{ MODKEY,                    XKB_KEY_w,           focusdir,         {.ui = 0} },  /* Super+W: focus the window above */
	{ MODKEY,                    XKB_KEY_a,           focusdir,         {.ui = 1} },  /* Super+A: focus the window to the left */
	{ MODKEY,                    XKB_KEY_s,           focusdir,         {.ui = 2} },  /* Super+S: focus the window below */
	{ MODKEY,                    XKB_KEY_d,           focusdir,         {.ui = 3} },  /* Super+D: focus the window to the right */
	{ MODKEY,                    XKB_KEY_Home,         homecanvas,       {0} },  /* Super+Home: return the canvas to its home position */
	{ MODKEY|WLR_MODIFIER_SHIFT, XKB_KEY_Left,         movecanvas,       {.i = 0} },  /* Super+Shift+Left: pan the canvas left (0 = left, 1 = right, 2 = up, 3 = down) */
	{ MODKEY|WLR_MODIFIER_SHIFT, XKB_KEY_Right,        movecanvas,       {.i = 1} },  /* Super+Shift+Right: pan the canvas right */
	{ MODKEY|WLR_MODIFIER_SHIFT, XKB_KEY_Up,           movecanvas,       {.i = 2} },  /* Super+Shift+Up: pan the canvas up */
	{ MODKEY|WLR_MODIFIER_SHIFT, XKB_KEY_Down,         movecanvas,       {.i = 3} },  /* Super+Shift+Down: pan the canvas down */
	{ MODKEY,		     XKB_KEY_Tab,            centerwindow,     {0} },  /* Super+Tab: center the focused window (shadowed by focusstackcenter above) */

	/* Zoom: step of 0.25 per press. */
	{ MODKEY, XKB_KEY_equal, zoom,      {.f =  +0.25f} },  /* Super+=: zoom in */
	{ MODKEY, XKB_KEY_minus, zoom,      {.f = -0.25f} },  /* Super+-: zoom out */

	/* Media keys; barspawn runs the command and refreshes the bar modules. */
	{ 0, XKB_KEY_XF86AudioRaiseVolume,  barspawn, {.v = volupcmd} },  /* volume up */
	{ 0, XKB_KEY_XF86AudioLowerVolume,  barspawn, {.v = voldowncmd} },  /* volume down */
	{ 0, XKB_KEY_XF86AudioMute,         barspawn, {.v = volmutecmd} },  /* mute toggle */
	{ 0, XKB_KEY_XF86MonBrightnessUp,   barspawn, {.v = brupcmd} },  /* brightness up */
	{ 0, XKB_KEY_XF86MonBrightnessDown, barspawn, {.v = brdowncmd} },  /* brightness down */

	{ WLR_MODIFIER_CTRL|WLR_MODIFIER_ALT,XKB_KEY_Terminate_Server, quit, {0} },  /* Ctrl+Alt+Backspace: quit the compositor */
/* Ctrl+Alt+F1..F12: switch to the virtual terminal n. */
#define CHVT(n) { WLR_MODIFIER_CTRL|WLR_MODIFIER_ALT,XKB_KEY_XF86Switch_VT_##n, chvt, {.ui = (n)} }
	CHVT(1), CHVT(2), CHVT(3), CHVT(4), CHVT(5), CHVT(6),
	CHVT(7), CHVT(8), CHVT(9), CHVT(10), CHVT(11), CHVT(12),
};

/* Mouse bindings. Format: { click target, modifiers, button, function, argument }
 * Targets: ClkClient = window, ClkRoot = empty canvas, ClkTagBar = tag buttons,
 * ClkStatus = status area of the bar. SCROLL_UP/SCROLL_DOWN are the scroll wheel. */
static const Button buttons[] = {
	{ ClkClient, MODKEY, BTN_LEFT,   moveresize,     {.ui = CurMove} },  /* Super+left drag on a window: move it */
	{ ClkClient, MODKEY, BTN_RIGHT,  moveresize,     {.ui = CurResize} },  /* Super+right drag on a window: resize it */
	{ ClkClient, MODKEY|WLR_MODIFIER_SHIFT, BTN_LEFT, movecanvasmouse, {.f = 1} },  /* Super+Shift+left drag on a window: pan the canvas */
	{ ClkRoot, MODKEY|WLR_MODIFIER_SHIFT, BTN_LEFT, movecanvasmouse, {.f = 1} },  /* Super+Shift+left drag on empty canvas: pan the canvas */
	{ ClkRoot, 0, BTN_LEFT, movecanvasmouse, {.f = 1} },  /* left drag on empty canvas: pan the canvas */
	{ ClkRoot, 0, BTN_RIGHT, spawn, {.v = termcmd} },  /* right click on empty canvas: open terminal */
	{ ClkTagBar, 0,         BTN_LEFT,   view,       {0} },  /* left click on a tag: view it */
	{ ClkTagBar, 0,         BTN_RIGHT,  toggleview, {0} },  /* right click on a tag: toggle it in the current view */
	{ ClkTagBar, MODKEY,    BTN_LEFT,   tag,        {0} },  /* Super+left click on a tag: move the focused window there */
	{ ClkTagBar, MODKEY,    BTN_RIGHT,  toggletag,  {0} },  /* Super+right click on a tag: toggle the tag on the focused window */
	{ ClkStatus, 0,         BTN_MIDDLE, spawn,      {.v = termcmd} },  /* middle click on the status area: open terminal */
	{ ClkClient, MODKEY, SCROLL_UP,   zoom, {.f = +0.25f} },  /* Super+scroll up over a window: zoom in */
	{ ClkClient, MODKEY, SCROLL_DOWN, zoom, {.f = -0.25f} },  /* Super+scroll down over a window: zoom out */

	{ ClkRoot, MODKEY, SCROLL_UP,   zoom, {.f = +0.25f} },  /* Super+scroll up over empty canvas: zoom in */
	{ ClkRoot, MODKEY, SCROLL_DOWN, zoom, {.f = -0.25f} },  /* Super+scroll down over empty canvas: zoom out */

	{ ClkRoot, 0, SCROLL_UP,   zoom, {.f = +0.25f} },  /* scroll up over empty canvas: zoom in */
	{ ClkRoot, 0, SCROLL_DOWN, zoom, {.f = -0.25f} },  /* scroll down over empty canvas: zoom out */
};
/* Graphics tablet pen buttons. Format: { button, state, function, argument }.
 * The single placeholder entry below binds nothing. */
static const TabletButton tabletbuttons[] = {
	{ 0,            0,                     NULL,       {0} }
};

/* Graphics tablet pad buttons, same format. Placeholder entry only; binds nothing. */
static const TabletPadButton tabletpadbuttons[] = {
	{ 0,            0,                     NULL,       {0} }
};
