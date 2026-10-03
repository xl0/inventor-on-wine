#!/usr/bin/env python3
"""One line per idm.exe case: order of WM_GETDLGCODE(0,0) recipients (cycles folded), other messages, result.
brief.py OUT.txt [OP-SUBSTRING...]"""
import sys, re

def fold(seq):
    out, i = [], 0
    while i < len(seq):
        best = None
        for p in range(1, 31):
            n = 1
            while seq[i + n * p:i + (n + 1) * p] == seq[i:i + p] and len(seq[i:i+p]) == p: n += 1
            if n >= 3 and (not best or n * p > best[0] * best[1]): best = (n, p)
        if best:
            n, p = best
            out.append("[%dx: %s]" % (n, ' '.join(seq[i:i + p]))); i += n * p
        else:
            out.append(seq[i]); i += 1
    return out

cases, cur = [], None
for l in open(sys.argv[1], errors='replace'):
    l = l.rstrip('\r\n')
    if l.startswith('== '): cur = [l]; cases.append(cur)
    elif cur is not None and l: cur.append(l)
sel = sys.argv[2:]
last_tree = None
for c in cases:
    name = c[0].split()[1]
    tree, op = name.split('.')
    if sel and not any(s in op for s in sel): continue
    if tree != last_tree:
        print('#', c[0].split(' ', 2)[2]); last_tree = tree
    seq, res = [], ''
    for l in c[1:]:
        l = l.strip()
        m = re.match(r'(\S+) GETDLGCODE wp=(\w+) lp=(\S+) -> (\w+)', l)
        if l.startswith('pre:'):
            if 'focus=' + c[0].split('foc=')[1].split()[0] + ' ' not in l + ' ': seq.append('{' + l + '}')
        elif m:
            seq.append(m.group(1) + ('' if m.group(3) == '0' else '?'))
        elif l.startswith('->'): res = l
        elif l.startswith('!!'): res = l
        elif l.startswith('done:'): pass
        elif 'GETTEXT' in l: pass
        else: seq.append('<' + l + '>')
    print('%-9s %s  %s' % (name, ' '.join(fold(seq)), res))
