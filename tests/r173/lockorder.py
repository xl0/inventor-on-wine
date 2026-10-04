#!/usr/bin/env python3
"""Symbolize and fold the LOCKORDER / XERROR blocks printed by the 173 debug build (lockorder-debug.patch).
Usage: lockorder.py [-x] LOG...   one line per distinct (held -> acquired, call chain); -x: the X errors instead.
Only pairs with a driver lock on one side are reported by the build."""
import re, subprocess, sys, collections

cache = {}

def sym(path, off):
    key = (path, off)
    if key not in cache:
        out = subprocess.run(['addr2line', '-f', '-i', '-e', path, hex(off - 1)], capture_output=True, text=True).stdout.split('\n')
        # -i prints inlined frames innermost first: function / file:line pairs
        cache[key] = [out[i] for i in range(0, len(out) - 1, 2)] or ['??']
    return cache[key]

SKIP = ('dbg_mutex_lock', 'dbg_mutex_trylock', 'dbg_lock_acquired', 'dbg_record', 'error_handler')
want = 'XERROR' if sys.argv[1] == '-x' else 'LOCKORDER'
logs = sys.argv[2:] if sys.argv[1] == '-x' else sys.argv[1:]
chains = collections.Counter()
for log in logs:
    cur = None
    for line in open(log, errors='replace'):
        line = line.rstrip('\r\n')
        m = re.match(want + r' pid \d+ tid \d+: (.*)', line)
        if m:
            cur = [re.sub(r' (resource|serial) \S+', '', m.group(1)), []]
            continue
        if cur is None: continue
        if line.startswith(want + ' end'):
            bases = {}
            for path, name, off, addr in cur[1]:
                if not name: bases[path] = addr - off
            frames = []
            for path, name, off, addr in cur[1]:
                if not path.endswith('.so') and '.so.' not in path or path not in bases: frames.append('?'); continue
                frames += [n for n in sym(path, addr - bases[path]) if n not in SKIP]
            # stop at the syscall / unix call boundary
            for stop in ('__wine_syscall_dispatcher', '__wine_unix_call_dispatcher'):
                if stop in frames: frames = frames[:frames.index(stop)]
            chains[(cur[0], ' < '.join(frames[:18]))] += 1
            cur = None
            continue
        m = re.match(r'(\S+?)\((\w*)\+(0x[0-9a-f]+)\) ?\[(0x[0-9a-f]+)\]', line)
        if m: cur[1].append((m.group(1), m.group(2), int(m.group(3), 16), int(m.group(4), 16)))

for (pair, chain), n in sorted(chains.items()):
    print(f'{pair}  [{n}x]\n    {chain}')
