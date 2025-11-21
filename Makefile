all: delta

clean:
	rm delta delta.o river-layout-v3.h river-layout-v3.o river-layout-v3.c

edit: river-layout-v3.h

delta: river-layout-v3.h river-layout-v3.o delta.o
	cc -o delta delta.o river-layout-v3.o -lwayland-client -lm

delta.o: delta.c river-layout-v3.h
	cc -Wall -Wextra -Wpedantic -Wno-unused-parameter -c -o delta.o delta.c

river-layout-v3.o: river-layout-v3.c
	cc -Wall -Wextra -Wpedantic -Wno-unused-parameter -c -o river-layout-v3.o river-layout-v3.c

river-layout-v3.c: river-layout-v3.xml
	wayland-scanner private-code < river-layout-v3.xml > river-layout-v3.c

river-layout-v3.h: river-layout-v3.xml
	wayland-scanner client-header < river-layout-v3.xml > river-layout-v3.h
