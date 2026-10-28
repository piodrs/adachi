#!/bin/sh
set -e

gcc -Wall -Wextra -std=c11 -pedantic -ggdb -o adachi main.c wm.c -lX11
