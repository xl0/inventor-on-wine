#!/bin/sh
# dshot.sh OUT.png L T R B: screenshot of the current display (x/shot.sh) cropped to the rectangle.
# Called by Harness.cs's dialog watcher; OUT may be a Wine path (Z:\...).
set -e
d=$(dirname "$0")/../..
out=$(printf %s "$1" | sed 's,^Z:,,; s,\\,/,g')
t=$(mktemp --suffix=.png)
"$d/x/shot.sh" "$t" "${DISPLAY#:}" >/dev/null
python3 -c "
from PIL import Image; import sys
i = Image.open('$t'); L, T, R, B = map(int, sys.argv[1:])
i.crop((max(L, 0), max(T, 0), min(R, i.width), min(B, i.height))).save('$out')" $2 $3 $4 $5
rm -f "$t"
