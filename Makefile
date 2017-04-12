CC = gcc
CFLAGS += -std=gnu99 -Wall -Wextra -Wpedantic -Werror -g -O3
LDFLAGS += -ldl -lxcb

SOURCES = src/linux_main.c src/platform/video/xcb.c

linux_main:
	$(CC) $(CFLAGS) $(LDFLAGS) $(SOURCES) -o build/$@
