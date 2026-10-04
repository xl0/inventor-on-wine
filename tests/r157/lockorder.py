#!/usr/bin/env python3
"""Symbolize and fold the LOCKORDER blocks printed by the debug build (inst/157/lockorder-debug.patch).
Usage: lockorder.py LOG... ; prints one line per distinct (held -> acquired, call chain)."""
import re, subprocess, sys, collections

ROOT = '/home/xl0/projects/wine/'  # guest paths /host/... map here
cache = {}

def sym(path, off):
    key = (path, off)
    if key not in cache:
        host = path.replace('/host/', ROOT)
        out = subprocess.run(['addr2line', '-f', '-i', '-e', host, hex(off - 1)], capture_output=True, text=True).stdout.split('\n')
        # -i prints inlined frames innermost first: function / file:line pairs
        names = [out[i] for i in range(0, len(out) - 1, 2)]
        cache[key] = names or ['??']
    return cache[key]

chains = collections.Counter()
procs = collections.defaultdict(set)
for log in sys.argv[1:]:
    cur = None
    for line in open(log, errors='replace'):
        line = line.rstrip('\r\n')
        m = re.match(r'LOCKORDER (\S+) pid \d+ tid \d+: (.*)', line)
        if m:
            cur = [m.group(1), m.group(2), []]
            continue
        if cur is None: continue
        if line.startswith('LOCKORDER end'):
            bases = {}
            for path, name, off, addr in cur[2]:
                if not name: bases[path] = addr - off
            frames = []
            for path, name, off, addr in cur[2]:
                if not path.endswith('.so') or path not in bases: frames.append('?'); continue
                for n in sym(path, addr - bases[path]):
                    if n in ('dbg_mutex_lock', 'dbg_lock_acquired', 'dbg_win32u_lock', 'dbg_record'): continue
                    frames.append(n)
            # stop at the syscall / unix call boundary
            for stop in ('__wine_syscall_dispatcher', '__wine_unix_call_dispatcher'):
                if stop in frames: frames = frames[:frames.index(stop)]
            key = (cur[1], ' < '.join(frames[:16]))
            chains[key] += 1
            procs[key].add(cur[0])
            cur = None
            continue
        m = re.match(r'(\S+?)\((\w*)\+(0x[0-9a-f]+)\) ?\[(0x[0-9a-f]+)\]', line)
        if m: cur[2].append((m.group(1), m.group(2), int(m.group(3), 16), int(m.group(4), 16)))

for (pair, chain), n in sorted(chains.items()):
    print(f'{pair}  [{n}x {",".join(sorted(procs[(pair, chain)]))}]\n    {chain}')
