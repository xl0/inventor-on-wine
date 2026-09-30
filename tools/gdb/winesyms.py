# gdb: `source tools/gdb/winesyms.py` after `gdb -p PID` on a Wine process: the preloader hides
# the link map, so this loads the symbols of every mapped ELF object and Wine PE module (DWARF)
# from /proc/PID/maps. Works when winedbg can't attach (e.g. loader lock held, 048).
import gdb, re, subprocess
pid = gdb.selected_inferior().pid
seen = {}
for l in open('/proc/%d/maps' % pid):
    f = l.split()
    if len(f) < 6 or not f[5].startswith('/') or int(f[2], 16) != 0 or f[5] in seen: continue
    seen[f[5]] = int(f[0].split('-')[0], 16)
n = 0
for path, base in seen.items():
    try: magic = open(path, 'rb').read(2)
    except OSError: continue
    if magic == b'\x7fE':
        if 'preloader' in path: continue
        off = base
    elif magic == b'MZ' and re.search(r'-windows/', path):
        m = re.search(r'^ImageBase\s+([0-9a-f]+)', subprocess.run(['objdump', '-p', path], capture_output=True, text=True).stdout, re.M)
        if not m: continue
        off = base - int(m.group(1), 16)
    else: continue
    try: gdb.execute('add-symbol-file %s -o %#x' % (path, off), to_string=True); n += 1
    except gdb.error as e: print(path, e)
print('loaded %d modules' % n)
