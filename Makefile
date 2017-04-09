CC = gcc
CFLAGS += -std=c11 -Wall -Wextra -Wpedantic -Werror -g
LDFLAGS += -ldl -lxcb

SOURCES = src/linux_main.c src/platform/video/xcb.c

linux_main:
	$(CC) $(CFLAGS) $(LDFLAGS) $(SOURCES) -o build/$@
