_VERSION = 0.1
VERSION  = $(_VERSION)

PKG_CONFIG = pkg-config

PREFIX  = /usr/local
DATADIR = $(PREFIX)/share

# neuswc (swc.pc) pulls in its own dependencies, XWayland included.
PKGS = swc wayland-server xkbcommon libinput

CC = cc
CFLAGS = -O2
