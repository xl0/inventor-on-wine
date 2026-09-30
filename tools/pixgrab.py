# tools/pixgrab.py WINID OUT.png (DISPLAY set): read a composite-redirected X window's backing pixmap
# (XCompositeNameWindowPixmap + XGetImage), i.e. what the app last rendered into it (078).
import ctypes as C, sys
from PIL import Image
X = C.CDLL('libX11.so.6'); XC = C.CDLL('libXcomposite.so.1')
X.XOpenDisplay.restype = C.c_void_p
XC.XCompositeNameWindowPixmap.restype = C.c_ulong
XC.XCompositeNameWindowPixmap.argtypes = [C.c_void_p, C.c_ulong]
class Attr(C.Structure):
    _fields_ = [('x', C.c_int), ('y', C.c_int), ('width', C.c_int), ('height', C.c_int), ('bw', C.c_int),
                ('depth', C.c_int), ('pad', C.c_byte * 200)]
class Img(C.Structure):
    _fields_ = [('width', C.c_int), ('height', C.c_int), ('xoffset', C.c_int), ('format', C.c_int),
                ('data', C.c_void_p), ('byte_order', C.c_int), ('bitmap_unit', C.c_int),
                ('bitmap_bit_order', C.c_int), ('bitmap_pad', C.c_int), ('depth', C.c_int),
                ('bytes_per_line', C.c_int), ('bits_per_pixel', C.c_int)]
X.XGetImage.restype = C.POINTER(Img)
X.XGetImage.argtypes = [C.c_void_p, C.c_ulong, C.c_int, C.c_int, C.c_uint, C.c_uint, C.c_ulong, C.c_int]
X.XGetWindowAttributes.argtypes = [C.c_void_p, C.c_ulong, C.c_void_p]
X.XFreePixmap.argtypes = [C.c_void_p, C.c_ulong]
d = X.XOpenDisplay(None); w = int(sys.argv[1], 16); a = Attr()
if not X.XGetWindowAttributes(d, w, C.byref(a)): sys.exit('no window')
p = XC.XCompositeNameWindowPixmap(d, w)
im = X.XGetImage(d, p, 0, 0, a.width, a.height, 0xffffffff, 2)
if not im: sys.exit('XGetImage failed')
i = im.contents
buf = C.string_at(i.data, i.bytes_per_line * i.height)
Image.frombytes('RGB', (i.width, i.height), buf, 'raw', 'BGRX', i.bytes_per_line).save(sys.argv[2])
X.XFreePixmap(d, p)
