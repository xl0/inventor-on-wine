#!/usr/bin/env python3
"""uilat: input -> visible-update latency on an X display.

Injects XTest input and watches the screen through XDamage on the root window
(non-compositing server: root damage covers every window) plus XGetImage of the
watched rect after each damage event. Timestamps: time.perf_counter(); a move's
t0 is taken right before XTestFakeMotionEvent + XSync.

Two measurements per scenario:
  step  isolated moves: wait for quiet, move, time until the watched rect's pixels
        differ from before the move (latency); 'settle' = last damage in the rect.
  drag  continuous moves at --hz: frames = distinct watched-rect images, fps,
        and for tracked scenarios (rubber, wmdrag, xmove) the lag of each frame:
        displayed position -> time since the move that put the cursor there.
Also samples per-process CPU (/proc) of Inventor (--prefix, default prefixes/inv), its wineserver,
explorer and the display's Xorg, and with --record counts X requests per client
PID and opcode (RECORD + X-Resource).

Scenarios assume tools/invscen/run.sh uilat (part in sketch edit, front view) on
1920x1080 with Inventor maximized; wmdrag/xmove need it restored (not maximized).
  self    own X window that draws at the pointer (tool validation, ~0 ms expected)
  rubber  Line tool: click start, drag right; tracks the rubber-band end on its row
  hover   alternate on/off the vertical sketch line (preselect highlight)
  orbit   Shift+middle drag in the viewport (3D control case)
  pan     middle drag in the viewport
  wmdrag  drag Inventor's caption strip (Wine SC_MOVE -> _NET_WM_MOVERESIZE, or Wine's own
          move loop when the WM lacks it, e.g. awesome 4.3)
  superdrag  same drag with Super held (WM's own move binding, e.g. awesome Mod4+drag)
  xmove   XMoveWindow the WM frame of Inventor's main window (openbox then reasserts
          its own geometry and sends synthetic ConfigureNotify per step: WM-dependent)
--setup reruns tools/invscen/run.sh uilat before each scenario (fresh part/sketch state).
Output: OUT/SCEN-{step,drag}.csv (per move / per frame) + summary on stdout and
appended to OUT/summary.txt.
"""
import argparse, ctypes as C, os, select, subprocess, sys, time
import numpy as np

X = C.CDLL('libX11.so.6'); XT = C.CDLL('libXtst.so.6'); XD = C.CDLL('libXdamage.so.1')
XR = C.CDLL('libXRes.so.1')
vp, ul, ci = C.c_void_p, C.c_ulong, C.c_int
X.XOpenDisplay.restype = vp; X.XOpenDisplay.argtypes = [C.c_char_p]
X.XDefaultRootWindow.restype = ul; X.XDefaultRootWindow.argtypes = [vp]
for f in ('XSync', 'XFlush', 'XPending', 'XConnectionNumber', 'XNextEvent', 'XCloseDisplay'):
    getattr(X, f).argtypes = [vp] + ([ci] if f == 'XSync' else [vp] if f == 'XNextEvent' else [])
X.XKeysymToKeycode.argtypes = [vp, ul]; X.XKeysymToKeycode.restype = C.c_ubyte
X.XStringToKeysym.argtypes = [C.c_char_p]; X.XStringToKeysym.restype = ul
XT.XTestFakeMotionEvent.argtypes = [vp, ci, ci, ci, ul]
XT.XTestFakeButtonEvent.argtypes = [vp, C.c_uint, ci, ul]
XT.XTestFakeKeyEvent.argtypes = [vp, C.c_uint, ci, ul]
XD.XDamageQueryExtension.argtypes = [vp, C.POINTER(ci), C.POINTER(ci)]
XD.XDamageCreate.argtypes = [vp, ul, ci]; XD.XDamageCreate.restype = ul
XD.XDamageSubtract.argtypes = [vp, ul, ul, ul]
X.XMoveWindow.argtypes = [vp, ul, ci, ci]


class XImage(C.Structure):
    _fields_ = [(n, ci) for n in ('width', 'height', 'xoffset', 'format')] + [('data', vp)] + \
               [(n, ci) for n in ('byte_order', 'bitmap_unit', 'bitmap_bit_order', 'bitmap_pad', 'depth',
                                  'bytes_per_line', 'bits_per_pixel')]
X.XGetImage.restype = C.POINTER(XImage)
X.XGetImage.argtypes = [vp, ul, ci, ci, C.c_uint, C.c_uint, ul, ci]
X.XFree.argtypes = [vp]


class XRect(C.Structure):
    _fields_ = [('x', C.c_short), ('y', C.c_short), ('w', C.c_ushort), ('h', C.c_ushort)]


class DamageNotify(C.Structure):
    _fields_ = [('type', ci), ('serial', ul), ('send_event', ci), ('display', vp), ('drawable', ul),
                ('damage', ul), ('level', ci), ('more', ci), ('timestamp', ul), ('area', XRect), ('geometry', XRect)]


now = time.perf_counter


class Disp:
    def __init__(self, name):
        self.d = X.XOpenDisplay(name.encode())
        assert self.d, 'cannot open display ' + name
        self.root = X.XDefaultRootWindow(self.d)
        self.fd = X.XConnectionNumber(self.d)


class Screen:
    """inj: input connection; obs: damage + images."""
    def __init__(self, name):
        self.inj, self.obs = Disp(name), Disp(name)
        ev, er = ci(), ci()
        assert XD.XDamageQueryExtension(self.obs.d, C.byref(ev), C.byref(er))
        self.dmg_type = ev.value  # XDamageNotify = 0
        self.dmg = XD.XDamageCreate(self.obs.d, self.obs.root, 3)  # XDamageReportBoundingBox
        X.XSync(self.obs.d, 0)
        self.evbuf = (C.c_char * 192)()
        self.rec = None

    # --- input
    def move(self, x, y):
        t = now(); XT.XTestFakeMotionEvent(self.inj.d, -1, int(x), int(y), 0); X.XSync(self.inj.d, 0); return t

    def button(self, b, down):
        XT.XTestFakeButtonEvent(self.inj.d, b, down, 0); X.XSync(self.inj.d, 0)

    def key(self, name, down):
        kc = X.XKeysymToKeycode(self.inj.d, X.XStringToKeysym(name.encode()))
        XT.XTestFakeKeyEvent(self.inj.d, kc, down, 0); X.XSync(self.inj.d, 0)

    def click(self, x, y, b=1):
        self.move(x, y); time.sleep(0.05); self.button(b, 1); time.sleep(0.05); self.button(b, 0)

    def tap(self, name):
        self.key(name, 1); time.sleep(0.03); self.key(name, 0)

    # --- output
    def grab(self, r):
        x, y, w, h = r
        im = X.XGetImage(self.obs.d, self.obs.root, x, y, w, h, 0xffffffff, 2).contents
        a = np.frombuffer(C.string_at(im.data, im.bytes_per_line * h), np.uint8).reshape(h, im.bytes_per_line)
        a = a[:, :w * 4].reshape(h, w, 4)[:, :, :3].copy()
        X.XFree(im.data); X.XFree(C.addressof(im))
        return a

    def damage(self, deadline):
        """Damage bounding boxes that arrive before deadline: [(t, x, y, w, h)] (first batch only)."""
        out = []
        while True:
            if self.rec: self.rec.pump()
            if not X.XPending(self.obs.d):
                if out: break
                to = deadline - now()
                if to <= 0: break
                fds = [self.obs.fd] + ([self.rec.fd] if self.rec else [])
                select.select(fds, [], [], min(to, 0.05))
                continue
            X.XNextEvent(self.obs.d, self.evbuf)
            t = now()
            e = DamageNotify.from_buffer(self.evbuf)
            if e.type == self.dmg_type:
                out.append((t, e.area.x, e.area.y, e.area.w, e.area.h))
        if out: XD.XDamageSubtract(self.obs.d, self.dmg, 0, 0)
        return out

    def drain(self):
        while self.damage(now()): pass


def hit(d, r):
    _, x, y, w, h = d
    return x < r[0] + r[2] and r[0] < x + w and y < r[1] + r[3] and r[1] < y + h


def differs(a, b, thr=24, n=3):
    return int((np.abs(a.astype(np.int16) - b).max(axis=2) > thr).sum()) >= n


# ---------------------------------------------------------------- process CPU / X requests

def proc_pids(disp, prefix):
    """Inventor (prefix on this display), its wineserver + explorer, Xorg of the display."""
    wp = os.path.realpath(prefix)
    res = {}
    for p in os.listdir('/proc'):
        if not p.isdigit(): continue
        try:
            argv = open(f'/proc/{p}/cmdline', 'rb').read().split(b'\0')
        except OSError: continue
        a0 = os.path.basename(argv[0].replace(b'\\', b'/')).decode(errors='replace')
        if a0 == 'Xorg' and disp.encode() in argv: res['Xorg'] = int(p); continue
        if a0 not in ('Inventor.exe', 'wineserver', 'explorer.exe'): continue
        try:
            env = open(f'/proc/{p}/environ', 'rb').read().split(b'\0')
        except OSError: continue
        if f'WINEPREFIX={wp}'.encode() in env and f'DISPLAY={disp}'.encode() in env:
            res[a0.split('.')[0]] = int(p)
    return res


def cpu(pid, threads=False):
    """utime+stime in s (whole process, or {tid-comm: s})."""
    hz = os.sysconf('SC_CLK_TCK')
    def st(path):
        s = open(path).read(); f = s[s.rindex(')') + 2:].split()
        return (int(f[11]) + int(f[12])) / hz, s[s.index('(') + 1:s.rindex(')')]
    if not threads: return st(f'/proc/{pid}/stat')[0]
    out = {}
    for t in os.listdir(f'/proc/{pid}/task'):
        try: v, comm = st(f'/proc/{pid}/task/{t}/stat'); out[f'{t}:{comm}'] = v
        except OSError: pass
    return out


class CPU:
    def __init__(self, pids): self.pids = pids
    def __enter__(self):
        self.t = now(); self.a = {k: cpu(p) for k, p in self.pids.items()}
        self.th = cpu(self.pids['Inventor'], True) if 'Inventor' in self.pids else {}
        return self
    def __exit__(self, *e):
        dt = now() - self.t
        self.pct = {k: 100 * (cpu(p) - self.a[k]) / dt for k, p in self.pids.items()}
        th = cpu(self.pids['Inventor'], True) if 'Inventor' in self.pids else {}
        top = sorted(((v - self.th.get(k, 0), k) for k, v in th.items()), reverse=True)[:5]
        self.top = [(k, 100 * v / dt) for v, k in top if v > 0]
    def __str__(self):
        return 'cpu% ' + ' '.join(f'{k}={v:.0f}' for k, v in self.pct.items()) + \
            ' | Inventor threads ' + ' '.join(f'{k}={v:.0f}' for k, v in self.top)


class Record:
    """X request counts per (client pid, opcode) via RECORD on a third connection."""
    class Range(C.Structure):
        _fields_ = [('core_req', C.c_ubyte * 2), ('core_rep', C.c_ubyte * 2),
                    ('ext_req_major', C.c_ubyte * 2), ('ext_req_minor', C.c_ushort * 2),
                    ('ext_rep_major', C.c_ubyte * 2), ('ext_rep_minor', C.c_ushort * 2),
                    ('delivered', C.c_ubyte * 2), ('device', C.c_ubyte * 2), ('errors', C.c_ubyte * 2),
                    ('started', ci), ('died', ci)]
    class Intercept(C.Structure):
        _fields_ = [('id_base', ul), ('server_time', ul), ('client_seq', ul), ('category', ci),
                    ('swapped', ci), ('data', C.POINTER(C.c_ubyte)), ('data_len', ul)]
    CB = C.CFUNCTYPE(None, vp, C.POINTER(Intercept))
    CORE = {1: 'CreateWindow', 2: 'ChangeWindowAttributes', 3: 'GetWindowAttributes', 8: 'MapWindow',
            10: 'UnmapWindow', 12: 'ConfigureWindow', 14: 'GetGeometry', 15: 'QueryTree', 18: 'ChangeProperty',
            20: 'GetProperty', 25: 'SendEvent', 36: 'GrabServer', 37: 'UngrabServer', 38: 'QueryPointer',
            40: 'TranslateCoords', 43: 'GetInputFocus', 53: 'CreatePixmap', 54: 'FreePixmap', 55: 'CreateGC',
            56: 'ChangeGC', 58: 'SetDashes', 59: 'SetClipRectangles', 60: 'FreeGC', 62: 'CopyArea',
            66: 'PolySegment', 70: 'PolyFillRectangle', 72: 'PutImage', 73: 'GetImage', 98: 'QueryExtension'}

    def __init__(self, name, raw=None):
        self.raw, self.log = raw, []  # raw(major, minor) -> keep the first 64 bytes of matching requests
        self.c = Disp(name); self.ctl = Disp(name)
        d = self.ctl.d
        X.XListExtensions.restype = vp; X.XListExtensions.argtypes = [vp, C.POINTER(ci)]
        n = ci(); names = C.cast(X.XListExtensions(d, C.byref(n)), C.POINTER(C.c_char_p))
        X.XQueryExtension.argtypes = [vp, C.c_char_p, C.POINTER(ci), C.POINTER(ci), C.POINTER(ci)]
        self.ext = {}
        for i in range(n.value):
            op, e1, e2 = ci(), ci(), ci()
            X.XQueryExtension(d, names[i], C.byref(op), C.byref(e1), C.byref(e2)); self.ext[op.value] = names[i].decode()
        XT.XRecordAllocRange.restype = C.POINTER(self.Range)
        r = XT.XRecordAllocRange()
        r.contents.core_req[:] = [1, 127]; r.contents.ext_req_major[:] = [128, 255]; r.contents.ext_req_minor[:] = [0, 255]
        clients = (ul * 1)(3)  # XRecordAllClients
        XT.XRecordCreateContext.restype = ul
        XT.XRecordCreateContext.argtypes = [vp, ci, vp, ci, vp, ci]
        self.ctx = XT.XRecordCreateContext(d, 0, clients, 1, C.byref(C.pointer(r.contents)), 1)
        X.XSync(d, 0)
        self.counts = {}; self.win = {}; self.on = False
        self.cb = self.CB(self._cb)
        XT.XRecordEnableContextAsync.argtypes = [vp, ul, self.CB, vp]
        XT.XRecordProcessReplies.argtypes = [vp]
        XT.XRecordFreeData.argtypes = [vp]
        assert XT.XRecordEnableContextAsync(self.c.d, self.ctx, self.cb, None)
        self.fd = self.c.fd

    def _cb(self, _, p):
        e = p.contents
        if e.category == 1 and self.on and e.data_len:  # XRecordFromClient
            op = e.data[0]
            if self.raw and self.raw(op, e.data[1]):
                self.log.append((now(), e.id_base, bytes(e.data[:min(e.data_len * 4, 64)])))
            key = (e.id_base, f'{self.ext.get(op, op)}:{e.data[1]}' if op >= 128 else self.CORE.get(op, str(op)))
            c = self.counts.setdefault(key, [0, 0]); c[0] += 1; c[1] += e.data_len * 4
            # window-targeting requests: count per (client, op, window) to see who churns what
            off = 4 if op in (2, 8, 10, 12, 18, 61) else 8 if self.ext.get(op) == 'SHAPE' else 0
            if off and e.data_len * 4 >= off + 4:
                w = int.from_bytes(bytes(e.data[off:off + 4]), 'little')
                self.win[(e.id_base, key[1], w)] = self.win.get((e.id_base, key[1], w), 0) + 1
        XT.XRecordFreeData(p)

    def pump(self): XT.XRecordProcessReplies(self.c.d)

    def pids(self):
        """id_base -> pid via X-Resource (0 if unknown)."""
        class Spec(C.Structure): _fields_ = [('client', ul), ('mask', C.c_uint)]
        class Val(C.Structure): _fields_ = [('spec', Spec), ('length', C.c_long), ('value', C.POINTER(C.c_uint32))]
        XR.XResQueryClientIds.argtypes = [vp, C.c_long, C.POINTER(Spec), C.POINTER(C.c_long), C.POINTER(C.POINTER(Val))]
        spec = Spec(0, 2); n = C.c_long(); v = C.POINTER(Val)()
        XR.XResQueryClientIds(self.ctl.d, 1, C.byref(spec), C.byref(n), C.byref(v))
        return {v[i].spec.client & ~0x1fffff: v[i].value[0] for i in range(n.value) if v[i].length == 4}

    def report(self, pidname, top=30):
        m = self.pids(); agg = {}
        for (base, op), (cnt, byt) in self.counts.items():
            who = pidname.get(m.get(base & ~0x1fffff, 0), f'pid{m.get(base & ~0x1fffff, "?")}')
            a = agg.setdefault((who, op), [0, 0]); a[0] += cnt; a[1] += byt
        tot = {}
        for (who, op), (cnt, byt) in agg.items(): t = tot.setdefault(who, [0, 0]); t[0] += cnt; t[1] += byt
        lines = ['X requests: ' + ' '.join(f'{w}={c}/{b >> 10}KiB' for w, (c, b) in sorted(tot.items(), key=lambda i: -i[1][0]))]
        for (who, op), (cnt, byt) in sorted(agg.items(), key=lambda i: -i[1][0])[:top]:
            lines.append(f'  {who:10} {op:24} {cnt:7} {byt >> 10:8} KiB')
        lines.append('top window ops (client op window count):')
        for (base, op, w), cnt in sorted(self.win.items(), key=lambda i: -i[1])[:top // 2]:
            who = pidname.get(m.get(base & ~0x1fffff, 0), f'pid{m.get(base & ~0x1fffff, "?")}')
            lines.append(f'  {who:10} {op:24} {w:#10x} {cnt:7}')
        return '\n'.join(lines)


# ---------------------------------------------------------------- measurements

def pct(v, q): return float(np.percentile(v, q)) * 1000 if len(v) else float('nan')


def step(S, positions, rect_of, timeout=1.0, quiet=0.15):
    """Isolated moves. rect_of(x, y) -> watched rect. Returns rows (i, x, y, latency_ms|'', settle_ms, ndamage)."""
    rows = []
    for i, (x, y) in enumerate(positions):
        # wait for quiet
        t = now()
        while now() - t < 3:
            if not S.damage(now() + quiet): break
        r = rect_of(x, y); before = S.grab(r)
        t0 = S.move(x, y); lat = None; last = None; nd = 0
        end = t0 + timeout
        while now() < end:
            ds = S.damage(end if lat is None else min(end, now() + quiet))
            if not ds:
                if lat is not None: break
                continue
            ds = [d for d in ds if hit(d, r)]
            if not ds: continue
            nd += len(ds); last = ds[-1][0]
            if lat is None and differs(S.grab(r), before): lat = ds[-1][0] - t0
        rows.append((i, x, y, '' if lat is None else round(lat * 1000, 2),
                     '' if last is None else round((last - t0) * 1000, 2), nd))
    return rows


def drag(S, path, hz, rect, track=None, tail=1.0, poll=None):
    """Moves along path at hz. Per damage hitting rect (or every poll s: window moves make
    no damage), grab rect, record frame if changed.
    track(img) -> displayed position (path coordinate) or None. Returns (moves, frames)."""
    moves, frames = [], []
    prev = S.grab(rect)
    period = 1 / hz; t_next = now()
    k = 0
    while True:
        tn = now()
        if k < len(path) and tn >= t_next:
            moves.append((S.move(*path[k]), *path[k])); k += 1; t_next += period
            if t_next < now(): t_next = now()  # injector fell behind: don't burst
            continue
        dl = t_next if k < len(path) else moves[-1][0] + tail
        if k >= len(path) and tn >= dl: break
        if poll:
            time.sleep(max(0, min(dl, now() + poll) - now())); t = now()
            if S.rec: S.rec.pump()
        else:
            ds = [d for d in S.damage(dl) if hit(d, rect)]
            if not ds: continue
            t = ds[-1][0]
        img = S.grab(rect)
        if not differs(img, prev, n=1): continue
        prev = img
        frames.append((t, track(img) if track else None))
        if k >= len(path): dl = max(dl, t + 0.3)
    return moves, frames


def drag_stats(moves, frames, axis, sign=1):
    """Lag per frame: tracked position p -> last move with sign*coord <= sign*p (monotonic path)."""
    t0 = moves[0][0]; t_end = moves[-1][0]
    during = [f for f in frames if f[0] <= t_end + 0.05]
    dur = t_end - t0
    lags, seen = [], set()
    coords = np.array([m[1 + axis] for m in moves]) * sign
    for t, p in frames:
        if p is None: continue
        j = int(np.searchsorted(coords, p * sign, side='right')) - 1
        if j < 0: continue
        while j > 0 and moves[j][0] > t: j -= 1  # displayed ahead of injected: tracker slop
        seen.add(j); lags.append(t - moves[j][0])
    s = {'moves': len(moves), 'frames': len(frames), 'fps': round(len(during) / dur, 1) if dur else 0,
         'settle_ms': round((frames[-1][0] - t_end) * 1000, 1) if frames else None}
    if lags:
        s.update(lag_p50=round(pct(lags, 50), 1), lag_p95=round(pct(lags, 95), 1), lag_max=round(pct(lags, 100), 1),
                 shown_moves=len(seen))
    return s, lags


# ---------------------------------------------------------------- scenarios

def track_row_end(ref, x0, thr=40):
    """Rubber band: rightmost column (screen x) of the row strip differing from ref."""
    def f(img):
        d = np.abs(img.astype(np.int16) - ref).max(axis=2).max(axis=0) > thr
        nz = np.nonzero(d)[0]
        return x0 + int(nz[-1]) if len(nz) else None
    return f


def track_shift(ref, x0, seg=(600, 300)):
    """Window drag: horizontal shift of ref[seg] in the current row strip (SAD over shifts)."""
    s0, sl = seg; tpl = ref[:, s0:s0 + sl].astype(np.int16)
    def f(img):
        a = img.astype(np.int16); n = a.shape[1] - sl
        win = np.lib.stride_tricks.sliding_window_view(a, (a.shape[0], sl, 3))[0, :, 0]
        sad = np.abs(win - tpl).sum(axis=(1, 2, 3))
        return x0 + int(np.argmin(sad)) - s0 if sad.min() < sad.mean() * 0.2 else None
    return f


def run(S, a, pids):
    sc, out = a.scenario, a.out
    res = {}
    def box(x, y, h=32): return (max(0, x - h), max(0, y - h), 2 * h, 2 * h)

    if sc == 'self':
        child = subprocess.Popen([sys.executable, __file__, '_selfwin', a.display])
        time.sleep(1.0)
        S.move(100, 100); time.sleep(0.3)
        res['step'] = step(S, [(100 + 17 * i, 100 + (i % 2) * 20) for i in range(40)], box)
        y = 300
        ref = S.grab((0, y, 1000, 1))
        res['drag'] = drag(S, [(50 + 3 * i, y) for i in range(300)], a.hz, (0, y, 1000, 1),
                           track_row_end(ref, 0)) + (0,)
        child.kill()

    elif sc == 'rubber':
        # Line tool (ribbon Sketch > Line), start point, then moves along the row.
        S.click(102, 85); time.sleep(0.8)
        y, x0 = 820, 600
        S.click(x0, y); time.sleep(0.5)
        S.move(x0 + 40, y); time.sleep(0.5)
        res['step'] = step(S, [(x0 + 60 + 12 * i, y + (i % 2)) for i in range(30)], box)
        S.move(x0 + 20, y); time.sleep(0.5)
        strip = (x0 + 30, y - 3, 1200, 9)  # rows around the line and its end marker
        ref = S.grab(strip)
        path = [(x0 + 40 + round(i * 1100 / (a.hz * a.secs)), y) for i in range(int(a.hz * a.secs))]
        res['drag'] = drag(S, path, a.hz, strip, track_row_end(ref, strip[0])) + (0,)
        S.tap('Escape'); time.sleep(0.2); S.tap('Escape')

    elif sc == 'hover':
        xl, y = 572, 600  # vertical sketch line
        pos = [(xl + (0 if i % 2 else 25), y + i) for i in range(40)]
        res['step'] = step(S, pos, lambda x, y: (xl - 12, y - 12, 24, 24))

    elif sc in ('orbit', 'pan'):
        cx, cy = 1150, 590
        watch = (1000, 450, 320, 280)
        mod = sc == 'orbit'
        S.move(cx, cy); time.sleep(0.3)
        if mod: S.key('Shift_L', 1)
        S.button(2, 1); time.sleep(0.2)
        res['step'] = step(S, [(cx + 6 * (i + 1), cy) for i in range(20)], lambda x, y: watch)
        path = [(cx + 120 - round(i * 240 / (a.hz * a.secs)), cy) for i in range(int(a.hz * a.secs))]
        res['drag'] = drag(S, path, a.hz, watch) + (0,)
        S.button(2, 0)
        if mod: S.key('Shift_L', 0)

    elif sc == 'split':
        # Drag the splitter between browser pane and viewport right by 300 px; tracks the border column on a 1-row strip.
        y, bx = 600, 150
        S.move(bx, y); time.sleep(0.3)
        ref = S.grab((bx, y, 400, 1))[0].astype(np.int16)
        bg = ref[10]
        b = int(np.argmax(np.abs(ref - bg).max(axis=1) > 6)); x0 = bx + b  # first column that is not browser background
        strip = (bx, y, 800, 1)
        def tr(img):
            d = np.abs(img[0].astype(np.int16) - bg).max(axis=1) > 6
            return bx + int(np.argmax(d)) if d.any() else None
        n = int(a.hz * a.secs)
        S.move(x0 + 2, y); time.sleep(0.3); S.button(1, 1); time.sleep(0.2)
        path = [(x0 + 2 + round(i * 300 / n), y) for i in range(1, n + 1)]
        res['drag'] = drag(S, path, a.hz, strip, tr) + (0,)
        S.button(1, 0); time.sleep(0.5)
        S.move(x0 + 302, y); S.button(1, 1); time.sleep(0.2)  # back
        for i in range(20): S.move(x0 + 302 - (i + 1) * 15, y); time.sleep(0.03)
        S.button(1, 0)

    elif sc in ('wmdrag', 'superdrag', 'xmove'):
        # Inventor restored (not maximized); its caption strip at y = caption_y of the frame.
        fr, geo = frame_of(a.display)
        fx, fy, fw = geo
        y = fy + 12
        strip = (0, y, 1920, 1)
        ref = S.grab(strip)
        seg = (fx + fw // 2 - 150, 300)
        tr = track_shift(ref, 0, seg)
        n = int(a.hz * a.secs); dist = 400
        if sc in ('wmdrag', 'superdrag'):
            gx = fx + int(fw * 0.62)  # empty caption area right of the title text
            mod = sc == 'superdrag'
            S.move(gx, y); time.sleep(0.2)
            if mod: S.key('Super_L', 1)
            S.button(1, 1); time.sleep(0.3)
            S.move(gx + 3, y); time.sleep(0.3)
            path = [(gx + 3 + round(i * dist / n), y) for i in range(n)]
            moves, frames = drag(S, path, a.hz, strip, tr, poll=0.001)
            S.button(1, 0); time.sleep(0.5)
            # back
            S.move(gx + dist, y); S.button(1, 1); time.sleep(0.2)
            for i in range(20): S.move(gx + dist - (i + 1) * dist // 20, y); time.sleep(0.02)
            S.button(1, 0)
            if mod: S.key('Super_L', 0)
            # track() returns the screen x of the tracked segment start; path coord = cursor x
            frames = [(t, None if p is None else p + gx) for t, p in frames]  # p = window shift
            res['drag'] = (moves, frames, 0)
        else:
            X.XMoveWindow.argtypes = [vp, ul, ci, ci]
            orig = fx
            def mv(x, _y):
                t = now(); X.XMoveWindow(S.inj.d, fr, int(x), fy); X.XSync(S.inj.d, 0); return t
            real = S.move
            S.move = mv
            path = [(fx + round(i * dist / n), fy) for i in range(1, n + 1)]
            moves, frames = drag(S, path, a.hz, strip, tr, poll=0.001)
            S.move = real
            X.XMoveWindow(S.inj.d, fr, orig, fy); X.XSync(S.inj.d, 0)
            frames = [(t, None if p is None else p + fx) for t, p in frames]
            res['drag'] = (moves, frames, 0)
    else:
        sys.exit('unknown scenario ' + sc)
    return res


def frame_of(disp):
    """WM frame (child of root) holding Inventor's main window, and its x, y, width."""
    o = subprocess.run(['xwininfo', '-root', '-children', '-display', disp], capture_output=True, text=True).stdout
    for line in o.splitlines():
        if 'has no name' not in line or not line.strip().startswith('0x'): continue
        wid = line.split()[0]
        sub = subprocess.run(['xwininfo', '-children', '-id', wid, '-display', disp], capture_output=True, text=True).stdout
        if 'Autodesk Inventor Professional' in sub:
            g = subprocess.run(['xwininfo', '-id', wid, '-display', disp], capture_output=True, text=True).stdout
            v = {l.split(':')[0].strip(): l.split(':')[1].strip() for l in g.splitlines() if ':' in l}
            return int(wid, 16), (int(v['Absolute upper-left X']), int(v['Absolute upper-left Y']), int(v['Width']))
    sys.exit('Inventor frame not found')


def selfwin(disp):
    """Test window: fills a black 8x8 square at each pointer motion (validates the tool)."""
    D = Disp(disp); d = D.d
    X.XCreateSimpleWindow.restype = ul; X.XCreateSimpleWindow.argtypes = [vp, ul, ci, ci, C.c_uint, C.c_uint, C.c_uint, ul, ul]
    w = X.XCreateSimpleWindow(d, D.root, 0, 0, 1100, 700, 0, 0, 0xffffff)
    X.XSelectInput.argtypes = [vp, ul, C.c_long]; X.XSelectInput(d, w, 1 << 6 | 1 << 15)  # PointerMotion|Exposure
    X.XMapRaised.argtypes = [vp, ul]; X.XMapRaised(d, w)
    X.XDefaultGC.restype = vp; X.XDefaultGC.argtypes = [vp, ci]; gc = X.XDefaultGC(d, 0)
    X.XFillRectangle.argtypes = [vp, ul, vp, ci, ci, C.c_uint, C.c_uint]
    ev = (C.c_char * 192)()
    while True:
        X.XNextEvent(d, ev)
        if C.cast(ev, C.POINTER(ci))[0] == 6:  # MotionNotify: x, y at offsets 64, 68
            x, y = C.cast(C.addressof(ev) + 64, C.POINTER(ci))[0], C.cast(C.addressof(ev) + 68, C.POINTER(ci))[0]
            X.XFillRectangle(d, w, gc, x - 4, y - 4, 8, 8); X.XFlush(d)


def main():
    if len(sys.argv) > 2 and sys.argv[1] == '_selfwin': return selfwin(sys.argv[2])
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('scenarios', nargs='+')
    p.add_argument('--setup', action='store_true', help='run tools/invscen/run.sh uilat before each scenario')
    p.add_argument('--display', default=':98')
    p.add_argument('--prefix', default=os.path.join(os.path.dirname(__file__), '../../prefixes/inv'),
                   help='Wine prefix of the Inventor to sample (CPU)')
    p.add_argument('--hz', type=float, default=120)
    p.add_argument('--secs', type=float, default=4)
    p.add_argument('--out', default=os.path.join(os.path.dirname(__file__), '../../inst/uilat'))
    p.add_argument('--tag', default='', help='label for this run (renderer, build, ...)')
    p.add_argument('--record', action='store_true', help='count X requests per client (RECORD)')
    p.add_argument('--perf', help='run "perf record -g" on these comma-separated roles (Inventor,wineserver,Xorg,explorer)'
                   ' during the scenario; data in OUT/NAME-ROLE.perf')
    a = p.parse_args()
    os.makedirs(a.out, exist_ok=True)
    S = Screen(a.display)
    if a.record: S.rec = Record(a.display)
    root = os.path.join(os.path.dirname(os.path.abspath(__file__)), '../..')
    for sc in a.scenarios:
        if a.setup:
            with open(f'{a.out}/setup.log', 'a') as f:
                subprocess.run([f'{root}/tools/invscen/run.sh', 'uilat'], cwd=root, stdout=f, stderr=f,
                               stdin=subprocess.DEVNULL, check=True)
            time.sleep(2)
        a.scenario = sc
        one(S, a)


def one(S, a):
    name = a.scenario + (('-' + a.tag) if a.tag else '')
    pids = proc_pids(a.display, a.prefix)
    if S.rec: S.rec.counts.clear(); S.rec.win.clear()
    perfs = []
    for role in (a.perf.split(',') if a.perf else []):
        perfs.append(subprocess.Popen(['perf', 'record', '-g', '-q', '-o', f'{a.out}/{name}-{role}.perf',
                                       '-p', str(pids[role])], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL))
    if perfs: time.sleep(1)
    if S.rec: S.rec.on = True
    with CPU(pids) as c:
        res = run(S, a, pids)
    if S.rec: S.rec.on = False
    for pp in perfs: pp.send_signal(2); pp.wait()
    lines = [f'== {name} {time.strftime("%F %T")} hz={a.hz}', str(c)]
    if 'step' in res:
        rows = res['step']
        with open(f'{a.out}/{name}-step.csv', 'w') as f:
            f.write('i,x,y,latency_ms,settle_ms,ndamage\n'); f.writelines(','.join(map(str, r)) + '\n' for r in rows)
        lat = [r[3] / 1000 for r in rows if r[3] != '']
        st = [r[4] / 1000 for r in rows if r[4] != '']
        lines.append(f'step: n={len(rows)} changed={len(lat)} latency p50={pct(lat, 50):.1f} p95={pct(lat, 95):.1f} '
                     f'max={pct(lat, 100):.1f} ms, settle p50={pct(st, 50):.1f} max={pct(st, 100):.1f} ms')
    if 'drag' in res:
        moves, frames, axis = res['drag']
        s, lags = drag_stats(moves, frames, axis)
        with open(f'{a.out}/{name}-drag.csv', 'w') as f:
            f.write('kind,t_ms,x,y_or_pos\n')
            t0 = moves[0][0]
            f.writelines(f'move,{(t - t0) * 1000:.2f},{x},{y}\n' for t, x, y in moves)
            f.writelines(f'frame,{(t - t0) * 1000:.2f},,{"" if p is None else p}\n' for t, p in frames)
        lines.append('drag: ' + ' '.join(f'{k}={v}' for k, v in s.items()))
    if S.rec:
        names = {v: k for k, v in pids.items()}; names[os.getpid()] = 'uilat'
        lines.append(S.rec.report(names))
    txt = '\n'.join(lines)
    print(txt)
    open(f'{a.out}/summary.txt', 'a').write(txt + '\n')


if __name__ == '__main__':
    main()
