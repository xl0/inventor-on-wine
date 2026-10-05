#!/usr/bin/env python3
"""stacks.py HANGFILE: one line per thread of a gdb 'thread apply all bt' dump (frame names, innermost first)."""
import re, sys
t = open(sys.argv[1], errors='replace').read()
n = int(sys.argv[2]) if len(sys.argv) > 2 else 14
for th in t.split('\nThread ')[1:]:
    lines = th.split('\n')
    fr = []
    for l in lines[1:]:
        m = re.match(r'#\d+\s+(?:0x[0-9a-f]+ in )?(\S+)', l)
        if m and m.group(1) not in ('futex_wait', '__GI___lll_lock_wait', 'lll_mutex_lock_optimized', '__futex_abstimed_wait_common64', '__futex_abstimed_wait_common', '__internal_syscall_cancel', '__syscall_cancel', '__syscall_cancel_arch'): fr.append(m.group(1))
    print(lines[0].split()[0], ' < '.join(fr[:n]))
