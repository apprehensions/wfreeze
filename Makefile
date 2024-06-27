.POSIX:

VERSION = 0

PREFIX = /usr/local

PKG_CONFIG = pkg-config

PKGS = wayland-client xkbcommon
INCS = `$(PKG_CONFIG) --cflags $(PKGS)`
LIBS = `$(PKG_CONFIG) --libs $(PKGS)`

WLCPPFLAGS = -DVERSION=\"$(VERSION)\"
WLCFLAGS   = -pedantic -Wall $(INCS) $(WLCPPFLAGS) $(CPPFLAGS) $(CFLAGS)
LDLIBS     = $(LIBS) -lcrypt

SRC = wfreeze.c xdg-shell-protocol.c wlr-layer-shell-unstable-v1-protocol.c \
      wlr-screencopy-unstable-v1-protocol.c
OBJ = $(SRC:.c=.o)

all: wfreeze

.c.o:
	$(CC) -o $@ $(WLCFLAGS) -c $<

wfreeze.o: wlr-layer-shell-unstable-v1-protocol.h wlr-screencopy-unstable-v1-protocol.h

wfreeze: $(OBJ)
	$(CC) $(LDFLAGS) -o $@ $(OBJ) $(LDLIBS)

WAYLAND_PROTOCOLS = `$(PKG_CONFIG) --variable=pkgdatadir wayland-protocols`
WAYLAND_SCANNER   = `$(PKG_CONFIG) --variable=wayland_scanner wayland-scanner`

xdg-shell-protocol.c:
	$(WAYLAND_SCANNER) private-code $(WAYLAND_PROTOCOLS)/stable/xdg-shell/xdg-shell.xml $@
xdg-shell-protocol.h:
	$(WAYLAND_SCANNER) client-header $(WAYLAND_PROTOCOLS)/stable/xdg-shell/xdg-shell.xml $@

wlr-layer-shell-unstable-v1-protocol.c:
	$(WAYLAND_SCANNER) private-code wlr-layer-shell-unstable-v1.xml $@
wlr-layer-shell-unstable-v1-protocol.h:
	$(WAYLAND_SCANNER) client-header wlr-layer-shell-unstable-v1.xml $@
wlr-layer-shell-unstable-v1-protocol.o: xdg-shell-protocol.o

wlr-screencopy-unstable-v1-protocol.c:
	$(WAYLAND_SCANNER) private-code wlr-screencopy-unstable-v1.xml $@
wlr-screencopy-unstable-v1-protocol.h:
	$(WAYLAND_SCANNER) client-header wlr-screencopy-unstable-v1.xml $@

clean:
	rm -f wfreeze *.o *-protocol.*

install: all
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	cp -f wfreeze $(DESTDIR)$(PREFIX)/bin
	chmod 4755 $(DESTDIR)$(PREFIX)/bin/wfreeze

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/wfreeze

.PHONY: all clean install uninstall
