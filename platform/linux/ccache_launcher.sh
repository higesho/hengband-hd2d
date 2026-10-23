#!/bin/sh
# Compiler launcher for platform/linux/CMakeLists.txt: runs ccache, except when the command
# generates a precompiled header (".gch"), which is compiled directly.
#
# Why: a .gch records the size and MD5 of every "#pragma once" header it read, and GCC uses
# that record to skip a later #include of the same header. ccache keys the .gch generation on
# the preprocessed text, which has no comments and no whitespace, so a header that only
# differs in its bytes (a comment edit, another overlay) still hits the cache and hands back
# a .gch with the old MD5. The sources then include that header a second time and fail with
# "redefinition of ... z-term.h". The object files are not affected (they do not embed the
# header bytes), so they keep using ccache. Generating the .gch takes about 10 seconds per
# core.
#
# usage: ccache_launcher.sh <ccache> <compiler> <compiler arguments...>
ccache=$1
shift
prev=
for arg in "$@"; do
    if [ "$prev" = "-x" ] && [ "$arg" = "c++-header" ]; then
        exec "$@"
    fi
    if [ "$arg" = "-xc++-header" ]; then
        exec "$@"
    fi
    prev=$arg
done
exec "$ccache" "$@"
