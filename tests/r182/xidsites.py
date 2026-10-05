#!/usr/bin/env python3
"""Fold the XIDSITE blocks printed at exit by the 182 debug build (xid-audit-debug.patch): which call chains allocate
X resource ids, on which display (gdi_display / own thread display / other thread's display), with the display locked by the allocating thread or not.
Usage: xidsites.py LOG...     one line per (display kind, mutex state, call chain) with the summed count."""
import re, subprocess, sys, collections

cache = {}
def sym(path, off):
    key = (path, off)
    if key not in cache:
        out = subprocess.run(['addr2line', '-f', '-i', '-e', path, hex(off - 1)], capture_output=True, text=True).stdout.split('\n')
        cache[key] = [out[i] for i in range(0, len(out) - 1, 2)] or ['??']
    return cache[key]

SKIP = ('dbg_alloc_xid',)
chains = collections.Counter()
for log in sys.argv[1:]:
    cur = None
    for line in open(log, errors='replace'):
        line = line.rstrip('\r\n')
        m = re.match(r'XIDSITE pid \d+ tid \d+: (\d+) allocations on (.*), display lock (.*)', line)
        if m:
            cur = [int(m.group(1)), m.group(2), m.group(3), []]
            continue
        if cur is None: continue
        if line.startswith('XIDSITE end'):
            bases = {}
            for path, name, off, addr in cur[3]:
                if not name: bases[path] = addr - off
            frames = []
            for path, name, off, addr in cur[3]:
                if path in bases and (path.endswith('.so') or '.so.' in path):
                    frames += [n for n in sym(path, addr - bases[path]) if n not in SKIP]
                else: frames.append(name or path.rsplit('/', 1)[-1] + '?')
            for stop in ('__wine_syscall_dispatcher', '__wine_unix_call_dispatcher'):
                if stop in frames: frames = frames[:frames.index(stop)]
            chains[(cur[1], cur[2], ' < '.join(frames))] += cur[0]
            cur = None
            continue
        m = re.match(r'(\S+?)\((\w*)\+?(0x[0-9a-f]+)?\) ?\[(0x[0-9a-f]+)\]', line)
        if m: cur[3].append((m.group(1), m.group(2), int(m.group(3) or '0', 16), int(m.group(4), 16)))

for (kind, state, chain), n in sorted(chains.items(), key=lambda x: (x[0][0], x[0][1], -x[1])):
    print(f'{n:9d}  {kind}, display lock {state}\n           {chain}')
