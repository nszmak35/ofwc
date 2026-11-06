.POSIX:
.SUFFIXES:

include config.mk

SRCDIR   = src
BUILDDIR = build
PROTODIR = protocols
GENDIR   = $(BUILDDIR)/gen

OFWCCPPFLAGS = -I$(SRCDIR) -I$(GENDIR) -D_GNU_SOURCE -DVERSION=\"$(VERSION)\"
OFWCDEVCFLAGS = -g -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers \
	-Werror=implicit -Werror=implicit-function-declaration -Werror=return-type \
	-Werror=incompatible-pointer-types -Werror=strict-prototypes

PKGCFLAGS = `$(PKG_CONFIG) --cflags $(PKGS)`
OFWCCFLAGS = -O2 $(OFWCDEVCFLAGS) $(OFWCCPPFLAGS) $(PKGCFLAGS) $(CFLAGS)
LDLIBS = `$(PKG_CONFIG) --libs $(PKGS)` -lm

PYTHON_CFLAGS = `$(PKG_CONFIG) --cflags python3-embed 2>/dev/null || python3-config --includes`
PYTHON_LIBS   = `$(PKG_CONFIG) --libs python3-embed 2>/dev/null || python3-config --embed --ldflags`

WAYLAND_SCANNER = `$(PKG_CONFIG) --variable=wayland_scanner wayland-scanner`

# IPC protocols (protocols/) become code through wayland-scanner.
GEN_HEADERS = \
	$(GENDIR)/dwl-ipc-unstable-v2-protocol.h \
	$(GENDIR)/ofwc-ipc-unstable-v1-protocol.h

HEADERS = $(wildcard $(SRCDIR)/*.h $(SRCDIR)/*/*.h)

OFWC_OBJS = \
	$(BUILDDIR)/ofwc.o \
	$(BUILDDIR)/util.o \
	$(BUILDDIR)/parser/parserconf.o \
	$(BUILDDIR)/dwl-ipc-unstable-v2-protocol.o \
	$(BUILDDIR)/ofwc-ipc-unstable-v1-protocol.o

all: ofwc ofwcctl

ofwc: $(OFWC_OBJS)
	$(CC) $(OFWC_OBJS) $(LDFLAGS) $(LDLIBS) $(PYTHON_LIBS) -o $@

# One translation unit: every header is a dependency.
OFWCCTL_OBJS = \
	$(BUILDDIR)/ofwcctl.o \
	$(BUILDDIR)/dwl-ipc-unstable-v2-protocol.o \
	$(BUILDDIR)/ofwc-ipc-unstable-v1-protocol.o

CLIENT_HEADERS = \
	$(GENDIR)/dwl-ipc-unstable-v2-client-protocol.h \
	$(GENDIR)/ofwc-ipc-unstable-v1-client-protocol.h

ofwcctl: $(OFWCCTL_OBJS)
	$(CC) $(OFWCCTL_OBJS) $(LDFLAGS) `$(PKG_CONFIG) --libs wayland-client` -o $@

$(BUILDDIR)/ofwcctl.o: $(SRCDIR)/ofwcctl/ofwcctl.c $(CLIENT_HEADERS)
	@mkdir -p $(@D)
	$(CC) -O2 $(OFWCDEVCFLAGS) -I$(GENDIR) -D_GNU_SOURCE `$(PKG_CONFIG) --cflags wayland-client` $(CFLAGS) -o $@ -c $(SRCDIR)/ofwcctl/ofwcctl.c

$(BUILDDIR)/ofwc.o: $(SRCDIR)/ofwc.c $(HEADERS) $(GENDIR)/default_config.h $(GEN_HEADERS) config.mk
	@mkdir -p $(@D)
	$(CC) $(OFWCCFLAGS) -o $@ -c $(SRCDIR)/ofwc.c

$(BUILDDIR)/util.o: $(SRCDIR)/util.c $(SRCDIR)/util.h
	@mkdir -p $(@D)
	$(CC) $(OFWCCFLAGS) -o $@ -c $(SRCDIR)/util.c

$(BUILDDIR)/parser/parserconf.o: $(SRCDIR)/parser/parserconf.c $(SRCDIR)/parser/parserconf.h
	@mkdir -p $(@D)
	$(CC) -O2 -g -Wall -D_GNU_SOURCE -I$(SRCDIR) $(PYTHON_CFLAGS) $(CFLAGS) -o $@ -c $(SRCDIR)/parser/parserconf.c

$(BUILDDIR)/dwl-ipc-unstable-v2-protocol.o: $(GENDIR)/dwl-ipc-unstable-v2-protocol.c $(GENDIR)/dwl-ipc-unstable-v2-protocol.h
	$(CC) -O2 -g -I$(GENDIR) $(PKGCFLAGS) $(CFLAGS) -o $@ -c $(GENDIR)/dwl-ipc-unstable-v2-protocol.c

$(BUILDDIR)/ofwc-ipc-unstable-v1-protocol.o: $(GENDIR)/ofwc-ipc-unstable-v1-protocol.c $(GENDIR)/ofwc-ipc-unstable-v1-protocol.h
	$(CC) -O2 -g -I$(GENDIR) $(PKGCFLAGS) $(CFLAGS) -o $@ -c $(GENDIR)/ofwc-ipc-unstable-v1-protocol.c

$(GENDIR)/%-client-protocol.h: $(PROTODIR)/%.xml
	@mkdir -p $(@D)
	$(WAYLAND_SCANNER) client-header $< $@

$(GENDIR)/%-protocol.h: $(PROTODIR)/%.xml
	@mkdir -p $(@D)
	$(WAYLAND_SCANNER) server-header $< $@

$(GENDIR)/%-protocol.c: $(PROTODIR)/%.xml
	@mkdir -p $(@D)
	$(WAYLAND_SCANNER) private-code $< $@

# default/config.py is compiled in as the built-in default.
$(GENDIR)/default_config.h: default/config.py
	@mkdir -p $(@D)
	( echo 'static const char ofwc_default_config[] ='; \
	  sed -e 's/\\/\\\\/g' -e 's/"/\\"/g' -e 's/^/"/' -e 's/$$/\\n"/' default/config.py; \
	  echo ';' ) > $@

clean:
	rm -rf $(BUILDDIR) ofwc ofwcctl

install: ofwc ofwcctl
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	rm -f $(DESTDIR)$(PREFIX)/bin/ofwc
	cp -f ofwc $(DESTDIR)$(PREFIX)/bin/ofwc
	chmod 755 $(DESTDIR)$(PREFIX)/bin/ofwc
	rm -f $(DESTDIR)$(PREFIX)/bin/ofwcctl
	cp -f ofwcctl $(DESTDIR)$(PREFIX)/bin/ofwcctl
	chmod 755 $(DESTDIR)$(PREFIX)/bin/ofwcctl
	mkdir -p $(DESTDIR)$(DATADIR)/wayland-sessions
	cp -f ofwc.desktop $(DESTDIR)$(DATADIR)/wayland-sessions/ofwc.desktop
	mkdir -p $(DESTDIR)/etc/ofwc
	cp -f default/config.py $(DESTDIR)/etc/ofwc/config.py

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/ofwc $(DESTDIR)$(PREFIX)/bin/ofwcctl \
		$(DESTDIR)$(DATADIR)/wayland-sessions/ofwc.desktop

.PHONY: all clean install uninstall
