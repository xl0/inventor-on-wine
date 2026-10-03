#!/usr/bin/env python3
"""Layered comparison of two idm.exe outputs (VM, Wine): termination, return value, WM_COMMANDs, final focus.
cmp2.py VM.txt WINE.txt [-v LAYER]"""
import sys, re, collections

def load(path):
    cases, cur = collections.OrderedDict(), None
    for l in open(path, errors='replace'):
        l = l.strip()
        if l.startswith('== '):
            cur = cases.setdefault(l.split()[1], {'hang': '', 'ret': '', 'cmd': [], 'focus': '', 'pre': '', 'disp': []}); continue
        if cur is None or not l: continue
        if l.startswith('!!'): cur['hang'] = 'HANG' if 'HANG' in l else 'CRASH'
        elif l.startswith('pre:'): cur['pre'] = l
        elif l.startswith('->'):
            m = re.match(r'-> ret=(\d) focus=(\S+)', l)
            if m: cur['ret'], cur['focus'] = m.group(1), m.group(2)
            else: cur['ret'] = l[3:]
        elif ' COMMAND ' in l and 'SYSCOMMAND' not in l and len(cur['cmd']) < 20:
            if 'code=0 ' in l: cur['cmd'].append(l)   # BN_CLICKED only
        elif 'dispatched' in l: cur['disp'].append(l)
    return cases

a, b = load(sys.argv[1]), load(sys.argv[2])
verbose = sys.argv[4] if len(sys.argv) > 4 else None
layers = ['pre', 'hang', 'ret', 'cmd', 'focus', 'disp']
cnt = {k: collections.Counter() for k in layers}
for name in a:
    op = name.split('.')[1]
    kind = 'char' if op.startswith(('CH', 'SYS')) else 'api' if op in ('TABN', 'TABP', 'GRPN', 'GRPP') else 'key'
    x, y = a[name], b.get(name)
    if not y: print(name, 'missing'); continue
    for k in layers:
        if k != 'hang' and (x['hang'] or y['hang']): continue   # nothing else to compare
        if x[k] != y[k]:
            cnt[k][kind] += 1
            if verbose == k: print('%-9s VM %-28s Wine %s' % (name, x[k], y[k]))
print('cases:', len(a))
for k in layers: print('%-6s differs: %4d  %s' % (k, sum(cnt[k].values()), dict(cnt[k])))
