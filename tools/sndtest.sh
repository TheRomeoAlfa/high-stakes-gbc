#!/bin/sh
# usage: tools/sndtest.sh OUT.gbc -DPAT=1 [-DLVL=3 | -DSFX=62]
set -e
GBDK=${GBDK:-$HOME/tools/gbdk}
cd "$(dirname "$0")/.."
OUT=$1; shift
T=$(mktemp -d)
$GBDK/bin/lcc -Isrc -Wf--opt-code-speed "$@" -c -o $T/t.o tools/sndtest.c
$GBDK/bin/lcc -Isrc -Wf--opt-code-speed "$@" -c -o $T/s.o src/sound.c
$GBDK/bin/lcc -Wl-yt0x1B -Wm-yC -autobank -Wm-yoA -o "$OUT" $T/t.o $T/s.o build/obj/gen/assets0.o build/obj/gen/assetsb.o
rm -rf $T
