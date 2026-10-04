#!/usr/bin/env python3
# pix.py SHOT.png: bounding boxes of the probe colours in a screenshot (xp.exe quadrants, wl_xswap magenta)
import sys
from PIL import Image
im = Image.open(sys.argv[1]).convert('RGB'); w, h = im.size; px = im.load()
names = {(255,0,255):'magenta', (0,255,255):'cyan', (0,255,0):'green', (0,0,255):'blue', (255,255,255):'white',
         (255,255,0):'yellow(panel)', (64,64,64):'grey(host)', (230,26,153):'xswap-magenta', (229,25,153):'xswap-magenta',
         (255,128,0):'orange', (0,0,0):'black'}
box = {}
for y in range(32, h, 2):
    for x in range(0, w, 2):
        n = names.get(px[x, y])
        if not n: continue
        b = box.setdefault(n, [x, y, x, y, 0])
        b[0] = min(b[0], x); b[1] = min(b[1], y); b[2] = max(b[2], x); b[3] = max(b[3], y); b[4] += 1
for n, b in sorted(box.items()): print('%-14s x %4d..%4d  y %4d..%4d  n=%d' % (n, b[0], b[2], b[1], b[3], b[4]))
