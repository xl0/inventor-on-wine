# gdb, x86_64 Wine process (source tools/gdb/winesyms.py first for names):
#   sehbt [TID...]   PE backtraces of every thread (or the given Wine tids, hex), unwound with the
#                    modules' .pdata, so native code without symbols works; one per syscall frame
#                    of the thread's chain (TEB+0x378, prev_frame +0xa0: user callbacks nest).
#                    Stops at JIT code (no .pdata): continue with stackscan/sehfrom.
#   stackscan LO HI  qwords in [LO,HI) that look like return addresses (call before them)
#   sehfrom SLOT     unwind as if the return address stored at SLOT had just been returned to
import gdb, struct, re
inf = lambda: gdb.selected_inferior()
def q(a): return struct.unpack('<Q', bytes(inf().read_memory(a, 8)))[0]
def d(a): return struct.unpack('<I', bytes(inf().read_memory(a, 4)))[0]
def w(a): return struct.unpack('<H', bytes(inf().read_memory(a, 2)))[0]
mods = None
def load_mods():
    global mods
    mods = []
    pid = inf().pid; seen = set()
    for l in open('/proc/%d/maps' % pid):
        f = l.split(None, 5)
        if len(f) < 6 or int(f[2], 16) != 0: continue
        path = f[5].strip(); base = int(f[0].split('-')[0], 16)
        if not path.startswith('/') or path in seen: continue
        try:
            if bytes(inf().read_memory(base, 2)) != b'MZ': continue
            nt = base + d(base + 0x3c)
            if d(nt) != 0x4550 or w(nt + 0x18) != 0x20b: continue
        except gdb.MemoryError: continue
        seen.add(path)
        size = d(nt + 0x18 + 56)
        prva, psize = d(nt + 0x18 + 0x70 + 3 * 8), d(nt + 0x18 + 0x70 + 3 * 8 + 4)
        mods.append((base, base + size, path.rsplit('/', 1)[-1], prva, psize))
def find_mod(pc):
    for m in mods:
        if m[0] <= pc < m[1]: return m
def lookup(m, rva):
    base, _, _, prva, psize = m
    lo, hi = 0, psize // 12 - 1
    while lo <= hi:
        mid = (lo + hi) // 2; e = base + prva + mid * 12
        b, en = d(e), d(e + 4)
        if rva < b: hi = mid - 1
        elif rva >= en: lo = mid + 1
        else: return b, d(e + 8)
    return None
def unwind(regs):
    pc = regs[16]; m = find_mod(pc)
    if not m: return False
    rf = lookup(m, pc - m[0])
    if rf is None:
        regs[16] = q(regs[4]); regs[4] += 8; return True
    begin, ui = rf; base = m[0]; first = True
    while True:
        u = base + ui; flags = inf().read_memory(u, 4).tobytes()
        ver_flags, prolog, cnt, fr = flags
        off = pc - base - begin if first else 0x7fffffff
        freg, foff = fr & 15, fr >> 4
        if freg and (not first or off >= prolog or any(
                (w(u + 4 + 2 * i) & 0xff) <= off and (w(u + 4 + 2 * i) >> 8 & 15) == 3 for i in range(cnt))):
            regs[4] = regs[freg] - 16 * foff
        i = 0
        while i < cnt:
            c = w(u + 4 + 2 * i); co, op, info = c & 0xff, c >> 8 & 15, c >> 12
            n = {0: 1, 1: 2 if info == 0 else 3, 2: 1, 3: 1, 4: 2, 5: 3, 6: 2, 8: 2, 9: 3, 10: 1}.get(op, 1)
            if (ver_flags & 7) == 2 and op == 6: n = 1; i += n; continue
            if co > off: i += n; continue
            if op == 0: regs[info] = q(regs[4]); regs[4] += 8
            elif op == 1: regs[4] += w(u + 4 + 2 * (i + 1)) * 8 if info == 0 else d(u + 4 + 2 * (i + 1))
            elif op == 2: regs[4] += info * 8 + 8
            elif op == 4: regs[info] = q(regs[4] + w(u + 4 + 2 * (i + 1)) * 8)
            elif op == 5: regs[info] = q(regs[4] + d(u + 4 + 2 * (i + 1)))
            elif op == 10:
                if info: regs[4] += 8
                regs[16] = q(regs[4]); regs[4] = q(regs[4] + 24); return True
            i += n
        if not (ver_flags >> 3) & 4: break
        e = u + 4 + 2 * (cnt + (cnt & 1)); begin, ui = d(e), d(e + 8); first = False
    regs[16] = q(regs[4]); regs[4] += 8
    return True
def name(pc):
    m = find_mod(pc)
    s = gdb.execute('info symbol %#x' % pc, to_string=True).strip()
    s = '' if s.startswith('No symbol') else ' ' + re.sub(r' in section .*', '', s)
    return '%#x %s+%#x%s' % (pc, m[2] if m else '?', pc - m[0] if m else 0, s)
FR = ['rax','rbx','rcx','rdx','rsi','rdi','r8','r9','r10','r11','r12','r13','r14','r15','rip',None,None,'rsp',None,'rbp']
IDX = {'rax':0,'rcx':1,'rdx':2,'rbx':3,'rsp':4,'rbp':5,'rsi':6,'rdi':7,'rip':16, **{'r%d' % i: i for i in range(8, 16)}}
class SehBt(gdb.Command):
    def __init__(self): super().__init__('sehbt', gdb.COMMAND_USER)
    def invoke(self, arg, tty):
        load_mods()
        want = {int(x, 16) for x in arg.split()}
        for t in sorted(inf().threads(), key=lambda t: t.num):
            t.switch()
            teb = int(gdb.parse_and_eval('$gs_base'))
            if not teb: continue
            tid = q(teb + 0x48)
            if want and tid not in want: continue
            print('==== thread %d LWP %d tid %#x' % (t.num, t.ptid[1], tid))
            fr = q(teb + 0x378); lvl = 0
            while fr:
                regs = [0] * 17
                for i, r in enumerate(FR):
                    if r: regs[IDX[r]] = q(fr + 8 * i)
                print('-- syscall frame %d (%#x)' % (lvl, fr))
                for k in range(80):
                    print('  #%-2d %s' % (k, name(regs[16])))
                    try:
                        if not unwind(regs) or regs[16] < 0x10000: break
                    except gdb.MemoryError: print('  (memory error)'); break
                fr = q(fr + 0xa0); lvl += 1
SehBt()
class StackScan(gdb.Command):
    """stackscan LO HI: qwords in [LO,HI) that look like return addresses into PE modules"""
    def __init__(self): super().__init__('stackscan', gdb.COMMAND_USER)
    def invoke(self, arg, tty):
        load_mods()
        lo, hi = [int(gdb.parse_and_eval(x)) for x in arg.split()]
        mem = bytes(inf().read_memory(lo, hi - lo))
        for o in range(0, len(mem) - 7, 8):
            v = struct.unpack_from('<Q', mem, o)[0]
            m = find_mod(v)
            if not m: continue
            try: pre = bytes(inf().read_memory(v - 6, 6))
            except gdb.MemoryError: continue
            if pre[1] == 0xe8 or pre[0] == 0xff and pre[1] in (0x15, 0x95) or pre[3] == 0xff and (pre[4] & 0x38) == 0x10 or pre[4] == 0xff and (pre[5] & 0x38) == 0x10:
                print('%#x: %s' % (lo + o, name(v)))
StackScan()
class SehFrom(gdb.Command):
    """sehfrom SLOT: unwind as if the return address stored at SLOT was just returned to"""
    def __init__(self): super().__init__('sehfrom', gdb.COMMAND_USER)
    def invoke(self, arg, tty):
        load_mods()
        slot = int(gdb.parse_and_eval(arg.split()[0]))
        regs = [0] * 17; regs[16] = q(slot); regs[4] = slot + 8
        for k in range(100):
            print('  #%-2d [%#x] %s' % (k, regs[4] - 8, name(regs[16])))
            try:
                if not unwind(regs) or regs[16] < 0x10000: break
            except gdb.MemoryError: print('  (memory error)'); break
SehFrom()
