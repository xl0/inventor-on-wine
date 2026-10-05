# check.py SHOT.png X Y W H [gpu]: compare the screen at the X window X,Y WxH (frame.exe's client area)
# with frame.exe's layout for that size (174). SCALE=1.5 in the environment: sizes at 144 DPI;
# BORDER=0: frame.exe plain. Prints "PIX ok" or, per child, the bounding box and
# count of wrong pixels (window coordinates). Exit 1 when anything is wrong.
import os, sys, numpy as np
from PIL import Image
S = float(os.environ.get('SCALE', 1))
RIBBON_H, BROWSER_W, STATUS_H, B = int(120 * S), int(240 * S), int(24 * S), int(float(os.environ.get('BORDER', 4)) * S)
shot, (x, y, w, h) = sys.argv[1], map(int, sys.argv[2:6])
gpu = len(sys.argv) > 6
im = np.asarray(Image.open(shot).convert('RGB'))[y:y + h, x:x + w]
if im.shape[:2] != (h, w): print('PIX BAD window not fully on screen', im.shape); sys.exit(1)
exp = np.zeros((h, w, 3), np.uint8)
def child(x0, y0, x1, y1, colour, border=True):
    exp[y0:y1, x0:x1] = 255 if border else colour
    if border and B: exp[y0 + B:y1 - B, x0 + B:x1 - B] = colour
    elif border: exp[y0:y1, x0:x1] = colour
    return (x0, y0, x1, y1)
rects = {'ribbon': child(0, 0, w, RIBBON_H, (255, 0, 0)),
         'browser': child(0, RIBBON_H, BROWSER_W, h - STATUS_H, (0, 255, 0)),
         'status': child(0, h - STATUS_H, w, h, (0, 0, 255)),
         'view': child(BROWSER_W, RIBBON_H, w, h - STATUS_H, (255, 255, 0) if gpu else (255, 0, 255), not gpu)}
bad = np.abs(im.astype(int) - exp).max(axis=2) > 40
# WPF draws its borders half a pixel off: ignore what is within 3 px of an expected colour change
# and the outermost 2 px; children with less than 50 wrong pixels pass
edge = np.zeros((h, w), bool)
for d in (1, 2, 3):
    for ax in (0, 1):
        for s in (d, -d):
            edge |= (np.roll(exp, s, axis=ax) != exp).any(axis=2)
for x0, y0, x1, y1 in rects.values():  # and the last row / column of a WPF child window
    edge[max(y0 - 2, 0):y0 + 2, x0:x1] = edge[max(y1 - 2, 0):y1 + 2, x0:x1] = True
    edge[y0:y1, max(x0 - 2, 0):x0 + 2] = edge[y0:y1, max(x1 - 2, 0):x1 + 2] = True
bad &= ~edge
bad[:2] = bad[-2:] = False; bad[:, :2] = bad[:, -2:] = False
res = []
for name, (x0, y0, x1, y1) in rects.items():
    b = bad[y0:y1, x0:x1]
    if b.sum() < 50: continue
    ys, xs = np.nonzero(b)
    res.append('%s %d px bbox %d,%d-%d,%d of %d,%d-%d,%d' % (name, b.sum(), xs.min() + x0, ys.min() + y0,
               xs.max() + 1 + x0, ys.max() + 1 + y0, x0, y0, x1, y1))
print('PIX ' + ('BAD ' + '; '.join(res) if res else 'ok'))
sys.exit(1 if res else 0)
