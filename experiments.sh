#!/bin/bash

set -e

if [[ ! -d build ]]; then
    mkdir -p build;
fi

pushd build > /dev/null
gcc -std=gnu99 -g -lpthread -Wall -Wextra -o ring_buffer ../experiments/ring_buffer.c
popd > /dev/null
