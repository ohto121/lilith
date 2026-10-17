.POSIX:
.SUFFIXES:

include config.mk

VPATH = src

LILITHCPPFLAGS = -I. -DWLR_USE_UNSTABLE -D_POSIX_C_SOURCE=200809L \
	-DVERSION=\"$(VERSION)\" $(XWAYLAND)
LILITHDEVCFLAGS = -g -Wpedantic -Wall -Wextra -Wdeclaration-after-statement \
	-Wno-unused-parameter -Wshadow -Wunused-macros -Werror=strict-prototypes \
	-Werror=implicit -Werror=return-type -Werror=incompatible-pointer-types \
	-Wfloat-conversion

PKGS      = wayland-server xkbcommon libinput libdrm fcft pixman-1 $(XLIBS)
LILITHCFLAGS = `$(PKG_CONFIG) --cflags $(PKGS)` $(WLR_INCS) $(LILITHCPPFLAGS) $(LILITHDEVCFLAGS) $(CFLAGS)
LDLIBS    = `$(PKG_CONFIG) --libs $(PKGS)` $(WLR_LIBS) -lm $(LIBS)

all: lilith
lilith: lilith.o util.o
	$(CC) lilith.o util.o $(LILITHCFLAGS) $(LDFLAGS) $(LDLIBS) -o $@
lilith.o: lilith.c client.h wallpaper.h bar.h stb_image.h config.h config.mk \
	config/config_appearance.h config/config_animation.h config/config_bar.h \
	config/config_input.h config/config_keys.h cursor-shape-v1-protocol.h \
	pointer-constraints-unstable-v1-protocol.h wlr-layer-shell-unstable-v1-protocol.h \
	wlr-output-power-management-unstable-v1-protocol.h xdg-shell-protocol.h \
	tablet-v2-protocol.h
util.o: util.c util.h

WAYLAND_SCANNER   = `$(PKG_CONFIG) --variable=wayland_scanner wayland-scanner`
WAYLAND_PROTOCOLS = `$(PKG_CONFIG) --variable=pkgdatadir wayland-protocols`

cursor-shape-v1-protocol.h:
	$(WAYLAND_SCANNER) enum-header \
		$(WAYLAND_PROTOCOLS)/staging/cursor-shape/cursor-shape-v1.xml $@
pointer-constraints-unstable-v1-protocol.h:
	$(WAYLAND_SCANNER) enum-header \
		$(WAYLAND_PROTOCOLS)/unstable/pointer-constraints/pointer-constraints-unstable-v1.xml $@
wlr-layer-shell-unstable-v1-protocol.h:
	$(WAYLAND_SCANNER) enum-header \
		protocols/wlr-layer-shell-unstable-v1.xml $@
wlr-output-power-management-unstable-v1-protocol.h:
	$(WAYLAND_SCANNER) server-header \
		protocols/wlr-output-power-management-unstable-v1.xml $@
xdg-shell-protocol.h:
	$(WAYLAND_SCANNER) server-header \
		$(WAYLAND_PROTOCOLS)/stable/xdg-shell/xdg-shell.xml $@
tablet-v2-protocol.h:
	$(WAYLAND_SCANNER) server-header \
		$(WAYLAND_PROTOCOLS)/unstable/tablet/tablet-unstable-v2.xml $@
clean:
	rm -f lilith *.o *-protocol.h lilith-*.tar.gz

dist: clean
	mkdir -p lilith-$(VERSION)
	mkdir -p lilith-$(VERSION)/src
	cp -R LICENSE* Makefile config.h config \
		config.mk protocols lilith.desktop \
		lilith-$(VERSION)
	cp -R src/lilith.c src/client.h src/wallpaper.h src/bar.h src/stb_image.h src/util.c src/util.h lilith-$(VERSION)/src/
	tar -caf lilith-$(VERSION).tar.gz lilith-$(VERSION)

	rm -rf lilith-$(VERSION)

install: lilith

	mkdir -p $(DESTDIR)$(PREFIX)/bin
	rm -f $(DESTDIR)$(PREFIX)/bin/lilith
	cp -f lilith $(DESTDIR)$(PREFIX)/bin
	chmod 755 $(DESTDIR)$(PREFIX)/bin/lilith
	mkdir -p $(DESTDIR)$(MANDIR)/man1
	mkdir -p $(DESTDIR)$(DATADIR)/wayland-sessions
	cp -f lilith.desktop $(DESTDIR)$(DATADIR)/wayland-sessions/lilith.desktop
	chmod 644 $(DESTDIR)$(DATADIR)/wayland-sessions/lilith.desktop
uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/lilith \
		$(DESTDIR)$(DATADIR)/wayland-sessions/lilith.desktop

.SUFFIXES: .c .o
.c.o:
	$(CC) $(CPPFLAGS) $(LILITHCFLAGS) -o $@ -c $<
