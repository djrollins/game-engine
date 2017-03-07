#!/bin/bash

set -e

if [[ ! -d build ]]; then
	mkdir -p build;
fi

pushd build > /dev/null
gcc -g -std=gnu99 -O3 -lX11 -lm -ludev -lasound -lpthread -Wall -Wextra -o game ../src/linux_platform.c
popd > /dev/null
