#!/usr/bin/env python3
"""classify.py HANGFILE...: one word per gdb hang dump: 177:<holder>, selfowner, malloc (190), surface (188), other"""
import re, sys, subprocess, collections
tot = collections.Counter()
for f in sys.argv[1:]:
    t = open(f, errors='replace').read()
    threads = t.split('\nThread ')[1:]
    names = []
    for th in threads:
        fr = re.findall(r'^#\d+\s+(?:0x[0-9a-f]+ in )?(\S+)', th, re.M)
        names.append(fr)
    cls = 'other'
    xerr = [n for n in names if '_XError' in n]
    holders = [n for n in names if '_XReply' in n and any(x in n for x in ('X11DRV_GetImage', 'create_shm_image', 'X11DRV_ExtFloodFill', 'lock_xid_alloc', 'XUnlockDisplay', 'XLockDisplay', 'create_glxcontext'))]
    selfown = [l for l in t.split('\n') if l.startswith('MUTEXWAIT') and re.search(r'LWP (\d+) waits for mutex \S+ owner LWP \1 ', l)]
    if selfown: cls = 'selfowner'
    elif xerr and holders:
        h = holders[0]
        cls = '177:' + next(x for x in ('X11DRV_GetImage', 'create_shm_image', 'X11DRV_ExtFloodFill', 'lock_xid_alloc', 'XUnlockDisplay', 'XLockDisplay', 'create_glxcontext') if x in h)
    elif any('__GI___lll_lock_wait_private' in n or '__lll_lock_wait_private' in n for n in names): cls = 'malloc(190)'
    elif any('XCheckIfEvent' in n and '___pthread_mutex_lock' in n for n in names): cls = 'display-mutex(no owner info)'
    elif any('flush_window_surfaces' in n for n in names) and not xerr: cls = 'surface(188?)'
    elif xerr: cls = '177:?'
    tot[cls] += 1
    if '-v' in sys.argv[0:1] or True: print(f.split('/')[-1], cls)
print(dict(tot))
