CC = gcc
CFLAGS += -std=gnu99 -Wall -Wextra -Wpedantic -Werror -g -O3
LDFLAGS = -lxcb -lxcb-shm

SOURCES = src/linux_main.c
HEADERS =
DEPS = build/platform.o

.DEFAULT: build/linux_main

build/linux_main: $(DEPS) $(SOURCES) $(HEADERS)
	$(CC) $(CFLAGS) $(SOURCES) $(LDFLAGS) $(DEPS) -o $@

PLATFORM_SOURCES = $(wildcard src/platform/**/*.c)
PLATFORM_HEADERS = $(wildcard src/platform/*.h)

build/platform.o: $(PLATFORM_HEADERS) $(PLATFORM_SOURCES)
	$(CC) $(CFLAGS) -c $(PLATFORM_SOURCES) -o $@
