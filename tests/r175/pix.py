#!/usr/bin/env python3
# pix.py PNG: which of owned.exe's windows are on the screenshot (pixels of each window's colour) (175)
import sys
from PIL import Image
cols = {"owner": (0, 200, 0), "dlg": (255, 0, 0), "tool": (255, 255, 0), "noact": (255, 0, 255), "plain": (255, 128, 0),
        "early": (0, 255, 255), "sub": (128, 64, 0), "late": (128, 0, 255), "xproc": (0, 128, 255), "bar": (3, 3, 3)}
im = Image.open(sys.argv[1]).convert("RGB")
im = im.crop((0, 24, im.width, im.height))  # not awesome's bar (urgent tags are red)
count = {}
for _n, c in im.getcolors(1 << 24): count[c] = _n
print(" ".join("%s=%d" % (n, count.get(c, 0)) for n, c in cols.items() if count.get(c, 0)) or "nothing")
