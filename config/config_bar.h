/*
 * Status bar configuration.
 * Colors are 0xRRGGBBAA, sizes are in pixels, durations in seconds.
 */

/* --- Enumerations used by the options below --- */

/* Screen edge the bar is attached to. */
enum { BarTop, BarBottom, BarLeft, BarRight };
/* Text orientation on vertical bars: rotated clockwise, rotated counter-clockwise, or upright. */
enum { BarRotCW, BarRotCCW, BarUpright };
/* Alignment: start, center or end of the available space. */
enum { BarAlignStart, BarAlignCenter, BarAlignEnd };
/* Bar modules; list them in barorder[] to show them.
 * BarModSpacer is flexible empty space that pushes later modules to the far end. */
enum { BarModTags, BarModStatus, BarModSpacer,
       BarModVolume, BarModBrightness, BarModBattery,
       BarModCpu, BarModRam, BarModTemp, BarModLayout, BarModCount };
/* Decoration of active tags: none, line on the inner edge, line on the outer edge, both edges, or a full rectangle border. */
enum { BarDecoNone, BarDecoInner, BarDecoOuter, BarDecoBoth, BarDecoBorder };
/* Indicator for tags that contain windows: none, dwm-style square, dot, or line. */
enum { BarOccNone, BarOccDwm, BarOccDot, BarOccLine };
/* Tag highlight animation: slide between tags, or cross-fade. */
enum { BarAnimSlide, BarAnimFade };
/* Bar show/hide animation: slide off the screen edge, fade, or both. */
enum { BarHideSlide, BarHideFade, BarHideBoth };
/* Color scheme indices into barcolors[]. They continue after the base
 * schemes SchemeNorm, SchemeSel and SchemeUrg defined in lilith.c. */
enum { SchemeOcc = SchemeUrg + 1, SchemeBar, SchemeStatus, SchemeSep, SchemeBorder,
       SchemeVolume, SchemeMuted, SchemeBrightness,
       SchemeBattery, SchemeBatChg, SchemeBatLow,
       SchemeCpu, SchemeCpuWarn, SchemeRam, SchemeRamWarn,
       SchemeTemp, SchemeTempWarn, SchemeLayout, SchemeCount };

/* --- Position and size --- */
static const int showbar         = 1;  /* 1 = bar visible at startup */
static const int barpos          = BarTop;  /* screen edge: BarTop, BarBottom, BarLeft or BarRight */
static const int barreserve      = 0;  /* 1 = reserve screen space for the bar, 0 = bar floats over the windows */
static const int barabovewindows = 1;  /* 1 = bar is drawn above windows, 0 = below them */
static const int barmargin[4]    = { 0, 0, 0, 0 };  /* outer margin in pixels: { top, right, bottom, left } */
static const int bargap          = 0;  /* extra space between the bar and the windows when space is reserved */
static const int barthickness    = 0;  /* bar height (width when vertical); 0 = derived from font size and padding */
static const int barlength       = 0;  /* fixed bar length; 0 = use barlengthpct or the full edge */
static const int barlengthpct    = 0;  /* bar length as a percentage of the edge (used when barlength is 0); 0 = full edge */
static const int baralign        = BarAlignCenter;  /* position of a shorter-than-edge bar along the edge: BarAlignStart/Center/End */
static const int barvertmode     = BarRotCW;  /* text orientation on vertical bars: BarRotCW, BarRotCCW or BarUpright */

/* --- Bar frame and fonts --- */
static const int barborderpx     = 0;  /* width of the bar border (colored by SchemeBorder) */
static const int barpad          = 0;  /* padding between the border and the bar content */

/* Fonts as fontconfig patterns; the first is primary, the rest are fallbacks (put an icon font here for the ICO_* glyphs). */
static const char *barfonts[]    = { "monospace:size=10" };
static const int barvpad         = 5;  /* vertical padding above and below text */
static const int barlrpad        = 2;  /* horizontal padding around text; 0 = use the font height */

/* --- Modules and separators --- */
static const int barorder[]      = { BarModTags, BarModSpacer, BarModStatus };  /* modules from start to end of the bar */
static const int barmodulegap    = 2;  /* gap between modules */
static const int barsepwidth     = 0;  /* thickness of the separator line between modules; 0 = none (needs barmodulegap > 0) */
static const int barseppct       = 60;  /* separator line length as a percentage of the bar thickness (max 100) */
static const char *barseptext    = "";  /* text drawn between modules instead of a line; "" = none */
static const int barcontentalign = BarAlignStart;  /* alignment of the modules when no BarModSpacer takes up the free space */

/* --- Tag buttons --- */
static const char *bartags[TAGCOUNT] = { " 1 ", " 2 ", " 3 ", " 4 ", " 5 ", " 6 ", " 7 ", " 8 ", " 9 " };  /* tag labels; one per tag, must have TAGCOUNT entries */
static const int bartaggap       = 0;  /* gap between tag buttons */
static const int bartagminw      = 0;  /* minimum width of a tag button */
static const int bartaginset     = 0;  /* inset of the tag button background inside the bar */
static const int bartaghideempty = 0;  /* 1 = hide tags that are empty, unselected and not urgent */
static const int bartagdeco      = BarDecoNone;  /* active tag decoration: BarDecoNone/Inner/Outer/Both/Border */
static const int bartagline      = 0;  /* thickness of the decoration line */
static const int bartagdecoall   = 0;  /* 1 = decorate all tags, 0 = only active ones */
static const int baroccstyle     = BarOccLine;  /* occupied tag indicator: BarOccNone/Dwm/Dot/Line */
static const int baroccsize      = 0;  /* size of the occupied indicator; 0 = derived from font height */
static const int baroccscheme    = 1;  /* 1 = occupied tags use the SchemeOcc colors */

/* --- Tag highlight animation --- */
static const int baranimtag      = 1;  /* 1 = animate the highlight when the selected tag changes */
static const int baranimstyle    = BarAnimSlide;  /* BarAnimSlide or BarAnimFade */
static const double baranimdur   = 0.25;  /* animation length */
static const char *baranimbezier = ".28,.67,.28,1.04";  /* easing curve (cubic-bezier "x1,y1,x2,y2") */

/* --- Bar show/hide animation --- */
static const int barhideanim        = 1;  /* 1 = animate hiding and showing the bar */
static const int barhidestyle       = BarHideBoth;  /* BarHideSlide, BarHideFade or BarHideBoth */
static const double barhidedur      = 0.35;  /* hide animation length */
static const double barshowdur      = 0.35;  /* show animation length */
static const char *barhidebezier    = ".28,.67,.28,1.04";  /* hide easing curve */
static const char *barshowbezier    = ".28,.67,.28,1.04";  /* show easing curve */

/* --- Status modules and data sources --- */
static const int barstatusallmons = 0;  /* 1 = show status modules on every monitor, 0 = only on the focused one */
#define barstatusstdin  0  /* 1 = read the status text from stdin when it is not a terminal (dwm-style status pipe) */
static const char *barclockfmt   = "%H:%M";  /* strftime() format of the built-in clock (updates every second if it contains seconds, otherwise every minute) */

/* Polling intervals and device selection. */
static const int barsysinterval  = 2000;  /* ms between CPU/RAM/temperature/battery/brightness updates */
static const int barvolinterval  = 5000;  /* ms between volume refreshes (checked on the system tick) */
static const int barkickms       = 150;  /* ms after a barspawn() command before modules are refreshed, so volume and brightness react to key presses */
static const char *barvolcmd[]   = { "wpctl", "get-volume", "@DEFAULT_AUDIO_SINK@", NULL };  /* command whose output is parsed for the volume */
static const char *barbatname    = "";  /* battery under /sys/class/power_supply (e.g. "BAT0"); "" = first found */
static const char *barblname     = "";  /* backlight under /sys/class/backlight; "" = first found */
static const char *bartemppath   = "";  /* temperature file (e.g. /sys/class/thermal/thermal_zone0/temp); "" = auto-detect */
static const int barbatwarn      = 20;  /* battery % at or below which the low-battery color is used */
static const int barcpuwarn      = 85;  /* CPU load % at or above which the warning color is used */
static const int barramwarn      = 85;  /* RAM usage % at or above which the warning color is used */
static const int bartempwarn     = 80;  /* temperature in degrees C at or above which the warning color is used */
static const char *barlayoutnames[] = { "us" };  /* display names for keyboard layouts, indexed by layout number (see xkb_rules) */

/* --- Icons: UTF-8 byte sequences of Font Awesome glyphs (an icon font must
 *     be available in barfonts[]) --- */
#define ICO_VOL_MUTE  "\xef\x80\xa6"  /* volume muted */
#define ICO_VOL_LOW   "\xef\x80\xa7"  /* volume below 50% */
#define ICO_VOL_HIGH  "\xef\x80\xa8"  /* volume 50% and above */
#define ICO_BRI       "\xef\x86\x85"  /* brightness */
#define ICO_BAT_CHG   "\xef\x83\xa7"  /* battery charging */
#define ICO_BAT_0     "\xef\x89\x84"  /* battery 0-20% */
#define ICO_BAT_1     "\xef\x89\x83"  /* battery 20-40% */
#define ICO_BAT_2     "\xef\x89\x82"  /* battery 40-60% */
#define ICO_BAT_3     "\xef\x89\x81"  /* battery 60-80% */
#define ICO_BAT_4     "\xef\x89\x80"  /* battery 80-100% */
#define ICO_CPU       "\xef\x8b\x9b"  /* CPU */
#define ICO_RAM       "\xef\x87\x80"  /* RAM */
#define ICO_TEMP      "\xef\x8b\x89"  /* temperature */
#define ICO_LAYOUT    "\xef\x84\x9c"  /* keyboard layout */

/* --- Module text formats ---
 * Placeholders: %i = icon, %v = integer value, %s = text (volume: "muted" or
 * empty; layout: layout name), %u / %t = used / total (RAM), %% = literal %. */
static const char *barfmt_vol    = "%v%%";  /* volume */
static const char *barfmt_bri    = "%v%%";  /* brightness */
static const char *barfmt_bat    = "%v%%";  /* battery */
static const char *barfmt_cpu    = "%v%%";  /* CPU load */
static const char *barfmt_ram    = "%u/%tG";  /* RAM: used/total */
static const char *barfmt_temp   = "%v°C";  /* temperature */
static const char *barfmt_layout = "%s";  /* keyboard layout name */

/* --- Icon assignment (shown where a format contains %i) --- */
static const char *baricons_vol[3] = { ICO_VOL_MUTE, ICO_VOL_LOW, ICO_VOL_HIGH };  /* { muted, low, high } */
static const char *baricon_bri     = ICO_BRI;  /* brightness */
static const char *baricons_bat[6] = { ICO_BAT_CHG, ICO_BAT_0, ICO_BAT_1, ICO_BAT_2, ICO_BAT_3, ICO_BAT_4 };  /* { charging, 0-20%, 20-40%, 40-60%, 60-80%, 80-100% } */
static const char *baricon_cpu     = ICO_CPU;  /* CPU */
static const char *baricon_ram     = ICO_RAM;  /* RAM */
static const char *baricon_temp    = ICO_TEMP;  /* temperature */
static const char *baricon_layout  = ICO_LAYOUT;  /* keyboard layout */

/* --- Colors ---
 * Each scheme is { foreground, background, accent } as 0xRRGGBBAA.
 * The accent is used for tag decorations and occupied indicators;
 * 0 = use the foreground color. A background alpha of 00 is transparent. */
static const uint32_t barcolors[SchemeCount][3] = {
	[SchemeNorm]      = { 0xbbbbbbff, 0x222222ff, 0 },  /* tag: normal */
	[SchemeSel]       = { 0xeeeeeeff, 0x005577ff, 0 },  /* tag: selected */
	[SchemeUrg]       = { 0xeeeeeeff, 0xff0000ff, 0 },  /* tag: urgent */
	[SchemeOcc]       = { 0xeeeeeeff, 0x222222ff, 0 },  /* tag: occupied (see baroccscheme) */
	[SchemeBar]       = { 0xbbbbbbff, 0x222222ff, 0 },  /* bar background */
	[SchemeStatus]    = { 0xbbbbbbff, 0x00000000, 0 },  /* status text */
	[SchemeSep]       = { 0x777777ff, 0x444444ff, 0 },  /* module separators */
	[SchemeBorder]    = { 0,          0x005577ff, 0 },  /* bar border (background value is the border color) */
	[SchemeVolume]    = { 0xbbbbbbff, 0x00000000, 0 },  /* volume module */
	[SchemeMuted]     = { 0x777777ff, 0x00000000, 0 },  /* volume module while muted */
	[SchemeBrightness]= { 0xbbbbbbff, 0x00000000, 0 },  /* brightness module */
	[SchemeBattery]   = { 0xbbbbbbff, 0x00000000, 0 },  /* battery module */
	[SchemeBatChg]    = { 0xeeeeeeff, 0x00000000, 0 },  /* battery module while charging */
	[SchemeBatLow]    = { 0xeeeeeeff, 0xff0000ff, 0 },  /* battery module below barbatwarn */
	[SchemeCpu]       = { 0xbbbbbbff, 0x00000000, 0 },  /* CPU module */
	[SchemeCpuWarn]   = { 0xeeeeeeff, 0xff0000ff, 0 },  /* CPU module above barcpuwarn */
	[SchemeRam]       = { 0xbbbbbbff, 0x00000000, 0 },  /* RAM module */
	[SchemeRamWarn]   = { 0xeeeeeeff, 0xff0000ff, 0 },  /* RAM module above barramwarn */
	[SchemeTemp]      = { 0xbbbbbbff, 0x00000000, 0 },  /* temperature module */
	[SchemeTempWarn]  = { 0xeeeeeeff, 0xff0000ff, 0 },  /* temperature module above bartempwarn */
	[SchemeLayout]    = { 0xbbbbbbff, 0x00000000, 0 },  /* keyboard layout module */
};
