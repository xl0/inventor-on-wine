# gdb: `bpbt FUNC N [SKIP]`: break on the unix-side FUNC (Nt* syscall implementation), print the PE
# backtrace of the hitting thread from its syscall frame for N hits taken every SKIP-th hit (098).
# Source tools/gdb/winesyms.py and sehbt.py first (uses their q/FR/IDX/unwind/name), and
# `handle SIGSEGV nostop noprint pass` for SIGSEGV/SIGUSR1/SIGUSR2/SIGILL/SIGBUS/SIGQUIT (not SIGTRAP).
import gdb
def sehcur():
    load_mods()
    teb = int(gdb.parse_and_eval('$gs_base'))
    fr = q(teb + 0x378)
    regs = [0] * 17
    for i, r in enumerate(FR):
        if r: regs[IDX[r]] = q(fr + 8 * i)
    for k in range(40):
        print('  #%-2d %s' % (k, name(regs[16])))
        try:
            if not unwind(regs) or regs[16] < 0x10000: break
        except gdb.MemoryError: print('  (memory error)'); break
class BpBt(gdb.Command):
    def __init__(self): super().__init__('bpbt', gdb.COMMAND_USER)
    def invoke(self, arg, tty):
        a = arg.split(); func = a[0]; n = int(a[1]); skip = int(a[2]) if len(a) > 2 else 1
        bp = gdb.Breakpoint(func)
        for l in gdb.execute('info breakpoints %d' % bp.number, to_string=True).splitlines():
            f = l.split()  # PE stubs (0x6fff...) see a stale syscall frame: keep the unix side
            if f and f[0].startswith('%d.' % bp.number) and int(f[2], 16) < 0x700000000000:
                gdb.execute('disable ' + f[0])
        for hits in range(n):
            bp.ignore_count = skip - 1
            gdb.execute('continue')
            print('#### hit %d LWP %d' % (hits, gdb.selected_thread().ptid[1]))
            sehcur()
        bp.delete()
BpBt()
