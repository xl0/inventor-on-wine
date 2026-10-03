#!/usr/bin/env python3
# Minimal x64 minidump reader (Autodesk CER's Temp\Inventor<ts>.dmp): exception, registers, stack scan of the
# faulting thread as module+RVA. Usage: tools/mdmp.py DUMP [-m (module list)] [-n FRAMES]
import struct, sys

d = open(sys.argv[1], 'rb').read()
NF = int(sys.argv[sys.argv.index('-n') + 1]) if '-n' in sys.argv else 60
sig, ver, nstreams, dir_rva = struct.unpack_from('<IIII', d, 0)
assert sig == 0x504d444d
streams = {}
for i in range(nstreams):
    t, size, rva = struct.unpack_from('<III', d, dir_rva + 12 * i)
    streams[t] = (rva, size)

def mstr(rva):
    n, = struct.unpack_from('<I', d, rva)
    return d[rva + 4:rva + 4 + n].decode('utf-16le')

mods = []
if 4 in streams:
    rva, _ = streams[4]
    n, = struct.unpack_from('<I', d, rva)
    for i in range(n):
        o = rva + 4 + 108 * i
        base, size, chk, ts, name_rva = struct.unpack_from('<QIIII', d, o)
        mods.append((base, size, mstr(name_rva).split('\\')[-1], ts))
mods.sort()

def sym(a):
    for base, size, name, _ in mods:
        if base <= a < base + size:
            return '%s+0x%x' % (name, a - base)
    return None

mem = []  # (start, size, file offset)
if 5 in streams:
    rva, _ = streams[5]
    n, = struct.unpack_from('<I', d, rva)
    for i in range(n):
        start, size, r = struct.unpack_from('<QII', d, rva + 4 + 16 * i)
        mem.append((start, size, r))
if 9 in streams:
    rva, _ = streams[9]
    n, base = struct.unpack_from('<QQ', d, rva)
    for i in range(n):
        start, size = struct.unpack_from('<QQ', d, rva + 16 + 16 * i)
        mem.append((start, size, base)); base += size

def read(a, n):
    for start, size, r in mem:
        if start <= a and a + n <= start + size:
            return d[r + (a - start):r + (a - start) + n]
    return None

threads = {}
if 3 in streams:
    rva, _ = streams[3]
    n, = struct.unpack_from('<I', d, rva)
    for i in range(n):
        o = rva + 4 + 48 * i
        tid, susp, pcls, prio, teb, stk_start, stk_size, stk_rva, ctx_size, ctx_rva = struct.unpack_from('<IIIIQQIIII', d, o)
        threads[tid] = (teb, stk_start, stk_size, stk_rva, ctx_rva, ctx_size)
        mem.append((stk_start, stk_size, stk_rva))

REGS = ['rax', 'rcx', 'rdx', 'rbx', 'rsp', 'rbp', 'rsi', 'rdi', 'r8', 'r9', 'r10', 'r11', 'r12', 'r13', 'r14', 'r15', 'rip']
def ctx(rva):
    return dict(zip(REGS, struct.unpack_from('<17Q', d, rva + 0x78)))

print('streams', sorted(streams))
if 6 in streams:
    rva, _ = streams[6]
    tid, _, code, flags, rec, addr, nparams, _ = struct.unpack_from('<IIIIQQII', d, rva)
    params = struct.unpack_from('<%dQ' % min(nparams, 15), d, rva + 40)
    ctx_size, ctx_rva = struct.unpack_from('<II', d, rva + 8 + 152)
    print('exception tid %x code %08x flags %x addr %x (%s) params %s' % (tid, code, flags, addr, sym(addr), ['%x' % p for p in params]))
    c = ctx(ctx_rva)
    for r in REGS:
        print('  %-3s %016x %s' % (r, c[r], sym(c[r]) or ''))
    ins = read(c['rip'], 16)
    if ins: print('  code at rip:', ins.hex())
    if tid in threads:
        teb, s, n, r, tctx, _ = threads[tid]
        print('thread stack %x..%x (dumped %d bytes), thread ctx rip %s' % (s, s + n, n, sym(ctx(tctx)['rip'])))
    sp = c['rsp']
    print('stack scan from exception rsp:')
    k = 0
    for a in range(sp & ~7, sp + 0x6000, 8):
        b = read(a, 8)
        if b is None: break
        v, = struct.unpack('<Q', b)
        s = sym(v)
        if s:
            print('  %x: %x %s' % (a, v, s)); k += 1
            if k >= NF: break
if '-m' in sys.argv:
    for base, size, name, ts in mods:
        print('%016x %8x %s' % (base, size, name))
print('threads', len(threads), 'modules', len(mods), 'mem ranges', len(mem))
