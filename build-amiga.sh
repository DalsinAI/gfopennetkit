#!/bin/sh
# gfopennetkit for AmigaOS 3.x: lib/libgfopennetkit.a (static link library),
# with the os32 GCC 16 stove (amiga-gcc, libnix), 68040, no FPU.
#   STOVE    the stove (default ~/AmiCradle/stoves/os32-gcc16)
#   TLS      a gfopentls checkout for its headers (default ../gfopentls)
#   OUT      the output directory (default build-amiga)
# MIT licence, (c) 2026 AmiCradle Team.
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
STOVE=${STOVE:-"$HOME/AmiCradle/stoves/os32-gcc16"}
P="$STOVE/prefix"
CC=${CC:-"$P/bin/m68k-amigaos-gcc"}
AR=${AR:-"$P/bin/m68k-amigaos-ar"}
TLS=${TLS:-"$HERE/../gfopentls"}
OUT=${OUT:-"$HERE/build-amiga"}
CFLAGS="-m68040 -O2 -fomit-frame-pointer -noixemul -Wall -Wextra -Wno-unused-parameter -std=gnu99 -I$HERE/include -I$TLS/include"
SRC="src/nk_crc32.c src/nk_util.c src/nk_line.c src/nk_conn.c src/nk_socks5.c
     src/nk_tls.c src/nk_journal.c src/amiga/nk_amiga.c"
mkdir -p "$OUT/obj" "$OUT/lib"
OBJS=""
for s in $SRC; do
    o="$OUT/obj/$(basename "$s" .c).o"
    "$CC" $CFLAGS -c "$HERE/$s" -o "$o"
    OBJS="$OBJS $o"
done
rm -f "$OUT/lib/libgfopennetkit.a"
"$AR" rcs "$OUT/lib/libgfopennetkit.a" $OBJS
echo "built $OUT/lib/libgfopennetkit.a"
