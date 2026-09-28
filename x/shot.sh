#!/usr/bin/env bash
# Screenshot an X display (default :98) to a PNG. Usage: x/shot.sh [out.png [DISPLAY_NUM]]
# Plain XGetImage of the root window (what the screen / x11vnc shows), via ctypes.
# Don't use `xwd -root`: Wine gives every process its own X colormap, which sends
# xwd down its multi-colormap path (packed 24 bpp output) and renders some Wine
# windows black (issue 036).
set -euo pipefail
out=$(realpath -m "${1:-/tmp/x${2:-98}.png}")
DISPLAY=:${2:-98} python3 -c "
import ctypes as C, numpy as np; from PIL import Image
class XImage(C.Structure):
    _fields_ = [(n, C.c_int) for n in ('width', 'height', 'xoffset', 'format')] + [('data', C.c_void_p)] + \
               [(n, C.c_int) for n in ('byte_order', 'bitmap_unit', 'bitmap_bit_order', 'bitmap_pad', 'depth',
                                       'bytes_per_line', 'bits_per_pixel')]
X = C.CDLL('libX11.so.6')
X.XOpenDisplay.restype = C.c_void_p; X.XDefaultRootWindow.restype = C.c_ulong
X.XDefaultRootWindow.argtypes = [C.c_void_p]
X.XGetImage.restype = C.POINTER(XImage)
X.XGetImage.argtypes = [C.c_void_p, C.c_ulong, C.c_int, C.c_int, C.c_uint, C.c_uint, C.c_ulong, C.c_int]
d = X.XOpenDisplay(None); assert d, 'cannot open display'
X.XDisplayWidth.argtypes = X.XDisplayHeight.argtypes = [C.c_void_p, C.c_int]
w, h = X.XDisplayWidth(d, 0), X.XDisplayHeight(d, 0)
im = X.XGetImage(d, X.XDefaultRootWindow(d), 0, 0, w, h, 0xffffffff, 2).contents  # ZPixmap
assert im.bits_per_pixel == 32 and im.byte_order == 0, (im.bits_per_pixel, im.byte_order)
px = np.frombuffer(C.string_at(im.data, im.bytes_per_line * h), np.uint8).reshape(h, im.bytes_per_line)
Image.fromarray(px[:, :w * 4].reshape(h, w, 4)[:, :, 2::-1]).save('$out')"
echo "$out"
