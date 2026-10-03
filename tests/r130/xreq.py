#!/usr/bin/env python3
"""X requests per client while a command runs: xreq.py DISPLAY CMD...  (RECORD, via tools/uilat)
Prints the busiest (client, request) pairs; XInputExtension:46 = XISelectEvents."""
import os, subprocess, sys, time
sys.dont_write_bytecode = True
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '../../tools/uilat'))
import uilat
rec = uilat.Record(sys.argv[1]); rec.on = True
p = subprocess.Popen(sys.argv[2:], stdin=subprocess.DEVNULL)
names, bases, t = {}, {}, 0
while p.poll() is None:
    rec.pump(); time.sleep(0.02)
    if time.time() - t > 0.5:  # clients are gone when the command exits: resolve them while it runs
        t = time.time(); bases.update(rec.pids())
        for pid in set(bases.values()):
            try: names[pid] = open(f'/proc/{pid}/comm').read().strip()
            except OSError: pass
rec.pump()
rec.pids = lambda: bases
print(rec.report(names, top=int(os.environ.get('XREQ_TOP', 24))))
