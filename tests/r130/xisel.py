#!/usr/bin/env python3
"""Cost of XISelectEvents on the root window (what winex11's 077 does per WM ConfigureNotify):
xisel.py DISPLAY [N] -> us per select (own connection, toggles XI_RawButtonRelease like select_raw_button_release)"""
import ctypes as C, sys, time
X = C.CDLL('libX11.so.6'); XI = C.CDLL('libXi.so.6')
X.XOpenDisplay.restype = C.c_void_p; X.XOpenDisplay.argtypes = [C.c_char_p]
X.XDefaultRootWindow.restype = C.c_ulong; X.XDefaultRootWindow.argtypes = [C.c_void_p]
X.XSync.argtypes = [C.c_void_p, C.c_int]
class Mask(C.Structure): _fields_ = [('deviceid', C.c_int), ('mask_len', C.c_int), ('mask', C.POINTER(C.c_ubyte))]
XI.XISelectEvents.argtypes = [C.c_void_p, C.c_ulong, C.POINTER(Mask), C.c_int]
d = X.XOpenDisplay(sys.argv[1].encode()); root = X.XDefaultRootWindow(d)
XI.XIQueryVersion.argtypes = [C.c_void_p, C.POINTER(C.c_int), C.POINTER(C.c_int)]
ma, mi = C.c_int(2), C.c_int(2); XI.XIQueryVersion(d, C.byref(ma), C.byref(mi))
n = int(sys.argv[2]) if len(sys.argv) > 2 else 2000
buf = (C.c_ubyte * 4)(); m = Mask(1, 4, buf)  # XIAllMasterDevices
t = time.perf_counter()
for i in range(n):
    buf[2] = 0x01 if i % 2 == 0 else 0  # XI_RawButtonRelease = 16 -> byte 2, bit 0
    XI.XISelectEvents(d, root, C.byref(m), 1); X.XSync(d, 0)
print(f'{sys.argv[1]}: {(time.perf_counter() - t) / n * 1e6:.1f} us per XISelectEvents+XSync on root')
