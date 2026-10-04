# gdb: entries of win32u's window surface list (dce.c) of a live process, with refcount and window.
#   gdb -p PID -batch -ex 'source tools/gdb/winesyms.py' -ex 'source tests/r171/surfaces.py'
import gdb
head = gdb.parse_and_eval("&'dce.c'::window_surfaces")
ws = gdb.lookup_type('struct window_surface')
off = [f.bitpos // 8 for f in ws.fields() if f.name == 'entry'][0]
n = 0
e = head['next']
while e != head and n < 10000:
    s = (e.cast(gdb.lookup_type('char').pointer()) - off).cast(ws.pointer())
    print("surface %s hwnd %s ref %d rect %s" % (s, s['hwnd'], int(s['ref']), s['rect']))
    n += 1
    e = e['next']
print("window_surfaces: %d entries" % n)
