PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin
BUILDDIR ?= build
CC ?= cc

default: $(BUILDDIR)/delta

install: $(BUILDDIR)/delta
	install -D $(BUILDDIR)/delta $(BINDIR)/delta

clean:
	rm -f $(BUILDDIR)/delta
	rm -f $(BUILDDIR)/delta.o
	rm -f river-layout-v3.h
	rm -f river-layout-v3.c
	rm -f $(BUILDDIR)/river-layout-v3.o
	rmdir $(BUILDDIR)

edit: river-layout-v3.h

$(BUILDDIR)/delta: river-layout-v3.h $(BUILDDIR)/river-layout-v3.o $(BUILDDIR)/delta.o $(BUILDDIR)
	$(CC) -o $(BUILDDIR)/delta $(BUILDDIR)/delta.o $(BUILDDIR)/river-layout-v3.o -lwayland-client -lm

$(BUILDDIR)/delta.o: delta.c river-layout-v3.h $(BUILDDIR)
	$(CC) -Wall -Wextra -Wpedantic -Wno-unused-parameter -c -o $(BUILDDIR)/delta.o delta.c

$(BUILDDIR)/river-layout-v3.o: river-layout-v3.c $(BUILDDIR)
	$(CC) -Wall -Wextra -Wpedantic -Wno-unused-parameter -c -o $(BUILDDIR)/river-layout-v3.o river-layout-v3.c

river-layout-v3.c: river-layout-v3.xml
	wayland-scanner private-code < river-layout-v3.xml > river-layout-v3.c

river-layout-v3.h: river-layout-v3.xml
	wayland-scanner client-header < river-layout-v3.xml > river-layout-v3.h

$(BUILDDIR):
	mkdir -p $(BUILDDIR)
