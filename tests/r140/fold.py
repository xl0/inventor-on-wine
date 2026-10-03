#!/usr/bin/env python3
"""Fold idm.exe output: repeated message cycles inside a case become "[N x] ..." blocks.
fold.py OUT.txt [CASE-PREFIX...]   e.g. fold.py idm-vm.txt 6. 15.CH_Q"""
import sys

def fold(lines):
    out, i = [], 0
    while i < len(lines):
        best = None
        for p in range(1, 41):
            if i + 2 * p > len(lines): break
            n = 1
            while lines[i + n * p:i + (n + 1) * p] == lines[i:i + p]: n += 1
            if n >= 3 and (not best or n * p > best[0] * best[1]): best = (n, p)
        if best:
            n, p = best
            out.append(f"  [{n} x {p} lines]")
            out += ["  |" + l[1:] for l in lines[i:i + p]]
            i += n * p
        else:
            out.append(lines[i]); i += 1
    return out

cases, cur = [], None
for l in open(sys.argv[1], errors='replace'):
    l = l.rstrip('\r\n')
    if l.startswith('== '):
        cur = [l]; cases.append(cur)
    elif cur is not None and l: cur.append(l)
sel = sys.argv[2:]
for c in cases:
    name = c[0].split()[1]
    if sel and not any(name.startswith(s) or name == s for s in sel): continue
    print(c[0]); print('\n'.join(fold(c[1:])))
