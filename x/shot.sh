#!/usr/bin/env bash
# Screenshot the headless Xorg :98 to a PNG. Usage: x/shot.sh [out.png]
# NVIDIA quirk (headless, UseDisplayDevice none): GetImage of the *root* window
# returns packed 24-bit BGR (3 bytes/pixel) although the header says 32 bpp;
# rows still use the 32-bpp stride. Reading it as 4 bytes/pixel squashes the
# image to 0.75 width, so clicks at screenshot coordinates miss.
set -euo pipefail
out=$(realpath -m "${1:-/tmp/x98.png}")
xwd -display :98 -root -silent | python3 -c "
import sys, numpy as np; from PIL import Image
d = sys.stdin.buffer.read()
hs, _, _, depth, w, h = np.frombuffer(d[:24], '>u4')[:6]
ncol = int(np.frombuffer(d[76:80], '>u4')[0]); bpl = int(np.frombuffer(d[48:52], '>u4')[0])
px = np.frombuffer(d[hs + ncol * 12:], np.uint8)[:bpl * h].reshape(h, bpl)[:, :w * 3].reshape(h, w, 3)
Image.fromarray(px[:, :, ::-1]).save('$out')"
echo "$out"
