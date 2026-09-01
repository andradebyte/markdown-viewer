CC ?= gcc
PKGS = gtk+-3.0 webkit2gtk-4.1
CFLAGS ?= -O2 -Wall
override CFLAGS += $(shell pkg-config --cflags $(PKGS))
LIBS = $(shell pkg-config --libs $(PKGS))

all: mdview

mdview: mdview.c
	$(CC) -o $@ $< $(CFLAGS) $(LIBS)

install: mdview
	mkdir -p ~/.local/bin ~/.local/share/applications ~/.local/share/icons/hicolor/scalable/apps
	cp mdview ~/.local/bin/
	cp mdview.desktop ~/.local/share/applications/
	cp mdview.svg ~/.local/share/icons/hicolor/scalable/apps/
	update-desktop-database ~/.local/share/applications
	gtk-update-icon-cache -f ~/.local/share/icons/hicolor || true
	xdg-mime default mdview.desktop text/markdown text/x-markdown text/plain

clean:
	rm -f mdview

.PHONY: all install clean
