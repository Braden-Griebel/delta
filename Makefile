all: rile

clean:
	rm rile rile.o river-layout-v3.h river-layout-v3.o river-layout-v3.c

rile: river-layout-v3.h river-layout-v3.o rile.o
	cc -o rile rile.o river-layout-v3.o -lwayland-client

rile.o: rile.c river-layout-v3.h
	cc -Wall -Wextra -Wpedantic -Wno-unused-parameter -c -o rile.o rile.c

river-layout-v3.o: river-layout-v3.c
	cc -Wall -Wextra -Wpedantic -Wno-unused-parameter -c -o river-layout-v3.o river-layout-v3.c

river-layout-v3.c: river-layout-v3.xml
	wayland-scanner private-code < river-layout-v3.xml > river-layout-v3.c

river-layout-v3.h: river-layout-v3.xml
	wayland-scanner client-header < river-layout-v3.xml > river-layout-v3.h
