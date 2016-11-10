#!/bin/bash

set -e

if [[ ! -d build ]]; then
	mkdir -p build;
fi

pushd build > /dev/null
gcc -g -O3 -lX11 -Wall -Wextra -o game ../src/linux_platform.c
popd > /dev/null
