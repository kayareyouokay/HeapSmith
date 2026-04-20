#!/bin/sh
set -eu

lib=${1:?usage: preload_hostile.sh path/to/libnnalloc.so}

LD_PRELOAD="$lib" /bin/true
LD_PRELOAD="$lib" /bin/echo "nn_alloc preload smoke"
LD_PRELOAD="$lib" /bin/ls . >/dev/null
LD_PRELOAD="$lib" /usr/bin/env >/dev/null
LD_PRELOAD="$lib" /usr/bin/printf "%s\n" "printf under preload" >/dev/null

if command -v sort >/dev/null 2>&1; then
    LD_PRELOAD="$lib" sort README.md >/dev/null
fi
