#!/usr/bin/env python3
"""X resources per client (X-Resource ext): xres.py DISPLAY [min] -> pid comm: TYPE=count ..."""
import ctypes as C, sys
X = C.CDLL('libX11.so.6'); R = C.CDLL('libXRes.so.1')
vp, ul = C.c_void_p, C.c_ulong
X.XOpenDisplay.restype = vp; X.XOpenDisplay.argtypes = [C.c_char_p]
X.XGetAtomName.restype = C.c_char_p; X.XGetAtomName.argtypes = [vp, ul]
d = X.XOpenDisplay(sys.argv[1].encode())
class Cl(C.Structure): _fields_ = [('base', ul), ('mask', ul)]
class Ty(C.Structure): _fields_ = [('type', ul), ('count', C.c_uint)]
class Spec(C.Structure): _fields_ = [('client', ul), ('mask', C.c_uint)]
class Val(C.Structure): _fields_ = [('spec', Spec), ('length', C.c_long), ('value', C.POINTER(C.c_uint32))]
n = C.c_int(); cl = C.POINTER(Cl)()
R.XResQueryClients.argtypes = [vp, C.POINTER(C.c_int), C.POINTER(C.POINTER(Cl))]
R.XResQueryClients(d, C.byref(n), C.byref(cl))
R.XResQueryClientIds.argtypes = [vp, C.c_long, C.POINTER(Spec), C.POINTER(C.c_long), C.POINTER(C.POINTER(Val))]
R.XResQueryClientResources.argtypes = [vp, ul, C.POINTER(C.c_int), C.POINTER(C.POINTER(Ty))]
R.XResQueryClientPixmapBytes.argtypes = [vp, ul, C.POINTER(ul)]
agg = {}
for i in range(n.value):
    base = cl[i].base
    spec = Spec(base, 2); m = C.c_long(); v = C.POINTER(Val)()
    R.XResQueryClientIds(d, 1, C.byref(spec), C.byref(m), C.byref(v))
    pid = v[0].value[0] if m.value and v[0].length == 4 else 0
    try: comm = open(f'/proc/{pid}/comm').read().strip()
    except OSError: comm = '?'
    k = C.c_int(); t = C.POINTER(Ty)()
    R.XResQueryClientResources(d, base, C.byref(k), C.byref(t))
    b = ul(); R.XResQueryClientPixmapBytes(d, base, C.byref(b))
    a = agg.setdefault((pid, comm), {'conns': 0, 'pixbytes': 0}); a['conns'] += 1; a['pixbytes'] += b.value
    for j in range(k.value):
        name = (X.XGetAtomName(d, t[j].type) or b'?').decode()
        a[name] = a.get(name, 0) + t[j].count
mn = int(sys.argv[2]) if len(sys.argv) > 2 else 0
for (pid, comm), a in sorted(agg.items(), key=lambda i: -sum(v for k, v in i[1].items() if k != 'pixbytes')):
    if sum(v for k, v in a.items() if k != 'pixbytes') >= mn:
        print(pid, comm, ' '.join(f'{k}={v}' for k, v in sorted(a.items())))
