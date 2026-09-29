#!/usr/bin/env python3
"""handle-attr.py WSLOG INVLOG PID(dec) MODS [N]: live unnamed Event/Section handles of PID at the end of WSLOG
(wineserver `LK A/C` lines from tools/soak/handle-trace.patch; SINCE/UPTO env = WSLOG line window), grouped
by creating request, creator pid and the first N frames of the kernelbase backtrace in INVLOG (`trace:leak`).
MODS: `resprobe.exe PID mods` output."""
import sys, collections, re, bisect
ws, inv, pid = sys.argv[1], sys.argv[2], '%04x' % int(sys.argv[3])
mods = []
if len(sys.argv) > 4:
    for l in open(sys.argv[4]):
        f = l.split()
        if len(f) >= 3:
            try: mods.append((int(f[0], 16), int(f[1], 16), f[2]))
            except ValueError: pass
    mods.sort()
def sym(a):
    a = int(a, 16)
    i = bisect.bisect_right([m[0] for m in mods], a) - 1
    if i >= 0 and a < mods[i][0] + mods[i][1]: return '%s+%x' % (mods[i][2], a - mods[i][0])
    return hex(a)
import os
since = int(os.environ.get('SINCE', 0)); upto = int(os.environ.get('UPTO', 1 << 60))
live = {}
for ln, l in enumerate(open(ws, errors='replace'), 1):
    if ln > upto: break
    if not l.startswith('LK '): continue
    f = l.split()
    if f[2] != pid: continue
    if f[1] == 'A': live[f[3]] = (f[4], f[6], f[7], ln)
    else: live.pop(f[3], None)
bt = {}
for l in open(inv, errors='replace'):
    m = re.search(r'trace:leak:leak_bt (\w) (\w+) (.*)', l)
    if m: bt[m.group(2)] = (m.group(1), m.group(3).split())
c = collections.Counter(); ex = {}
for h, (t, req, cp, ln) in live.items():
    if ln <= since: continue
    k = (t, req, 'self' if cp == pid else cp)
    if req in ('create_event', 'create_mapping') and h in bt and bt[h][0] == t:
        syms = [x for x in (sym(a) for a in bt[h][1]) if not x.startswith('0x') and not re.match(r'(kernel32.dll\+0|InvAIRLook.*|kernelbase.dll\+(7a850|139a0|13ab9|145ef))$', x)]
        k += tuple(syms[:int(sys.argv[5]) if len(sys.argv) > 5 else 8])
    c[k] += 1
for k, n in c.most_common(40): print(n, ' '.join(k))
print('total', sum(c.values()))
