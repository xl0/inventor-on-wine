#!/usr/bin/env python3
"""cycles.py FOLDED...: lock graph from lockorder.py output (blocking pairs only, "(try)" ignored); prints the cycles."""
import sys, re, collections
g = collections.defaultdict(set)
for f in sys.argv[1:]:
    for line in open(f):
        m = re.match(r'(\S+) -> (\S+?)( \(try\))?  \[', line)
        if m and not m.group(3) and m.group(1) != m.group(2): g[m.group(1)].add(m.group(2))
cycles = set()
def walk(n, path):
    for m in sorted(g.get(n, ())):
        if m in path:
            c = path[path.index(m):]; i = c.index(min(c)); cycles.add(tuple(c[i:] + c[:i]))
        else: walk(m, path + [m])
for n in sorted(g): walk(n, [n])
print(f'{len(g)} locks with outgoing edges, {sum(len(v) for v in g.values())} edges, {len(cycles)} cycles')
for c in sorted(cycles): print('  ' + ' -> '.join(c + (c[0],)))
