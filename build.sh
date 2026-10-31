#!/bin/sh
set -e

set --
for source in ./*.c; do
    set -- "$@" "$source"
done

gcc -Wall -Wextra -std=c11 -pedantic -ggdb -o adachi "$@" -lX11
