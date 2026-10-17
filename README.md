# lilith

a wlroots-based wayland compositor. no tiling layout, no rules engine — you
get an endless canvas and windows just float on top of it wherever you drop
them. the canvas itself can be panned and zoomed, so you can spread stuff out
across a huge desktop and zoom out to look at it all, like a whiteboard.

screenshots of it in daily use are over in the dotfiles of
[ceapeapea](https://github.com/ceapeapea/lilith-dotfiles), who also rices it
and contributes fixes back here.

## what it does

- floating windows on an infinite canvas: drag things wherever, pan the whole
  view with the mouse, throw a window and it keeps sliding for a bit (inertia)
- double borders: every window has a separate inner and outer border, each
  with its own width and color, plus dedicated variants for focused / urgent /
  unfocused — so you can do subtle stacked outlines or just one plain border
- the canvas zooms with the scroll wheel, and all the animations (tag switch,
  window open/close, zoom) follow the current zoom level so nothing looks off
- `zoomfit` (win+z): zooms so every open window fits on the screen at once.
  mostly used right before taking a screenshot
- magnet snapping while dragging — windows stick to each other's edges and to
  the screen edges, with some positioning logic that keeps a dragged window in
  line with its snapped neighbours
- a built-in status bar (tags, clock, volume, brightness, battery, cpu, ram,
  temperature, keyboard layout). it can sit on any screen edge, can be
  transparent or opaque, hides with an animation, and has its own tag
  switching animation. drawn with fcft/pixman, needs no extra daemon
- **spawn region**: drag a rectangle on the empty canvas and the new window
  opens inside it. you can pick how it looks in config — outline, filled, or
  invisible (SPAWNREGION_STYLE in config_appearance.h)
- by default every tag shares one canvas view; flip `TAG_KEEP_VIEW` in
  `config_animation.h` if you'd rather have each tag remember its own spot
- keyboard layouts (us/ru etc.) with a layout indicator in the bar, proper
  graphics tablet support (pen maps to the window under it), and XWayland for
  the occasional non-wayland thing

## building

arch/artix package names, adjust for your distro:

```
base-devel wlroots0.19 libinput libxkbcommon libdrm fcft pixman
xorg-server-xwayland xcb-util-wm
```

then:

```
git clone https://github.com/ohto121/lilith
cd lilith
make
sudo make install    # binary ends up in /usr/local/bin
lilith               # from a tty, not from inside another compositor
```

works from a tty; if you use a display manager that reads wayland-sessions it
should be able to start lilith directly.

## config

everything lives in `config/`. yes, all of it:

- `config_appearance.h` — borders, gaps, wallpaper, the spawn region
- `config_animation.h` — tag switch / open / close / zoom animations, inertia
- `config_bar.h` — the status bar: position, modules, colours, font
- `config_input.h` — keyboard layouts, touchpad, tablet
- `config_keys.h` — commands and all the key/mouse bindings

every single option is commented, and colors are just `0xRRGGBBAA` hex so you
don't need a converter. rebuild after editing (`make`), then restart.

as an example, one wallpaper hack worth knowing: by default the compositor
doesn't draw a wallpaper itself, it leaves it to whatever you run (swaybg or
anything else). set `wallpaper_path` in `config_appearance.h` if you'd rather
have an image that follows the canvas zoom.

## some keys (see config_keys.h for the full list)

- win+q terminal, win+c close, win+f fullscreen, win+shift+q quit
- win+r launcher, win+tab center a window and focus it
- win+1..9 tags, win+arrows pan the canvas, win+home reset it
- win+left-drag on a window moves it, win+right-drag resizes it, scroll zooms
- win+w,a,s,d focus by direction, win+shift+arrows pan the canvas
- win+b hides the bar, win+ctrl+b cycles which edge it's on

## credits

lilith is mine (ohto121), built on top of dwl, sway and wlroots — the
LICENSE* files list who did what in the parts that come from other projects.

the rice (pywal colors, session scripts, the bar setup, screenshots) is
maintained by [ceapeapea](https://github.com/ceapeapea), who is also the
first collaborator — go check
[his dotfiles](https://github.com/ceapeapea/lilith-dotfiles) if you want it
preconfigured.
