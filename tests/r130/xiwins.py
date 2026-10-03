#!/usr/bin/env python3
"""Stand-in for the leaked windows of Chromium's GPU process: xiwins.py DISPLAY N [hold]
creates N unmapped toplevels (each with one child) with XI2 pointer/key masks selected on
both, like the GPU process' leftover Wine whole/client windows; 'hold' keeps them until killed."""
import ctypes as C, sys, time
X = C.CDLL('libX11.so.6'); XI = C.CDLL('libXi.so.6')
vp, ul = C.c_void_p, C.c_ulong
X.XOpenDisplay.restype = vp; X.XOpenDisplay.argtypes = [C.c_char_p]
X.XDefaultRootWindow.restype = ul; X.XDefaultRootWindow.argtypes = [vp]
X.XCreateSimpleWindow.restype = ul; X.XCreateSimpleWindow.argtypes = [vp, ul, C.c_int, C.c_int, C.c_uint, C.c_uint, C.c_uint, ul, ul]
X.XSync.argtypes = [vp, C.c_int]
class Mask(C.Structure): _fields_ = [('deviceid', C.c_int), ('mask_len', C.c_int), ('mask', C.POINTER(C.c_ubyte))]
XI.XISelectEvents.argtypes = [vp, ul, C.POINTER(Mask), C.c_int]
XI.XIQueryVersion.argtypes = [vp, C.POINTER(C.c_int), C.POINTER(C.c_int)]
d = X.XOpenDisplay(sys.argv[1].encode()); root = X.XDefaultRootWindow(d)
ma, mi = C.c_int(2), C.c_int(2); XI.XIQueryVersion(d, C.byref(ma), C.byref(mi))
buf = (C.c_ubyte * 4)(0xfc, 0x03, 0, 0)  # XI_KeyPress..XI_FocusOut (2..9)
m = Mask(1, 4, buf)
for i in range(int(sys.argv[2])):
    w = X.XCreateSimpleWindow(d, root, 0, 0, 1920, 884, 0, 0, 0)
    c = X.XCreateSimpleWindow(d, w, 0, 0, 1920, 884, 0, 0, 0)
    XI.XISelectEvents(d, w, C.byref(m), 1); XI.XISelectEvents(d, c, C.byref(m), 1)
X.XSync(d, 0)
print('created', flush=True)
if len(sys.argv) > 3:
    while True: time.sleep(3600)
