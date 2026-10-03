#!/usr/bin/env python3
"""Compare two idm.exe outputs: cmp.py VM.txt WINE.txt [full]
Default compares per case: focus before, result line (return value, final focus / returned window), hang/crash,
WM_COMMAND / BM_CLICK / dispatched key messages.  `full` compares every logged line."""
import sys, collections

def load(path, full):
    cases, cur, name = collections.OrderedDict(), None, None
    for l in open(path, errors='replace'):
        l = l.strip()
        if l.startswith('== '):
            name = l.split()[1]; cur = cases.setdefault(name, []); continue
        if cur is None or not l: continue
        if l.startswith('!!'): cur[:] = ['HANG' if 'HANG' in l else 'CRASH']; continue
        if full or l.startswith(('pre:', '->')) or ' COMMAND ' in l or 'BM_CLICK' in l or 'dispatched' in l or 'SYSCOMMAND' in l:
            if len(cur) < 5000: cur.append(l)
    return cases

full = len(sys.argv) > 3
a, b = load(sys.argv[1], full), load(sys.argv[2], full)
diff = collections.Counter(); n = 0
for name in a:
    if a[name] != b.get(name):
        n += 1; op = name.split('.')[1]; diff[op] += 1
        print('%-9s VM:   %s' % (name, ' | '.join(a[name][:12])))
        print('%-9s Wine: %s' % ('', ' | '.join((b.get(name) or ['missing'])[:12])))
print('%d of %d cases differ' % (n, len(a)), dict(diff))
