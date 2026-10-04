#!/usr/bin/env python3
"""Pointer and key jitter for the winewayland stress runs (157; after the review's inst/157-review/jitter.py).
  jitter.py host SECS [X Y]   host session (x/wayland.sh): winj.py socket; relative moves +1/-1 as fast as it takes
                              them, a click at the start (focus), a Shift_L press every 50 moves
  jitter.py vm SECS [X Y]     vmwl guest over QMP (vmwl/qmp.sock): absolute move (usb-tablet) + click, then relative
                              moves through the PS/2 mouse and a shift key press every 50 moves
Relative motion is what zwp_relative_pointer_v1 reports; absolute moves alone produce none. Prints the rate."""
import json, os, socket, sys, time
mode, secs = sys.argv[1], float(sys.argv[2])
x = int(sys.argv[3]) if len(sys.argv) > 3 else 400; y = int(sys.argv[4]) if len(sys.argv) > 4 else 300
s = socket.socket(socket.AF_UNIX)
if mode == 'host':
    s.connect('/tmp/wl-xdg/winj.sock'); f = s.makefile('rw')
    def cmd(c): f.write(c + '\n'); f.flush(); f.readline()
    home = lambda: cmd('move %d %d' % (x, y))
    rel = lambda d: cmd('rel %d 0' % d)
    key = lambda: cmd('key Shift_L')
    click = lambda: cmd('click')
else:
    s.connect(os.path.join(os.path.dirname(os.path.abspath(__file__)), '../../vmwl/qmp.sock')); f = s.makefile('rw')
    json.loads(f.readline())
    def q(name, **args):
        f.write(json.dumps({'execute': name, 'arguments': args}) + '\n'); f.flush()
        while True:
            r = json.loads(f.readline())
            if 'return' in r or 'error' in r: return r
    q('qmp_capabilities')
    ev = lambda *e: q('input-send-event', events=list(e))
    ax = lambda t, a, v: {'type': t, 'data': {'axis': a, 'value': v}}
    btn = lambda down: {'type': 'btn', 'data': {'down': down, 'button': 'left'}}
    home = lambda: ev(ax('abs', 'x', int(x * 0x7fff / 1279)), ax('abs', 'y', int(y * 0x7fff / 799)))
    rel = lambda d: ev(ax('rel', 'x', d))
    key = lambda: q('send-key', keys=[{'type': 'qcode', 'data': 'shift'}], **{'hold-time': 1})
    def click(): ev(btn(True)); time.sleep(0.05); ev(btn(False))
home(); click()
end = time.time() + secs; n = 0
while time.time() < end:
    rel(1 if n % 2 == 0 else -1); n += 1
    if n % 50 == 0: key()
    if n % 2000 == 0: home()
print('%d moves, %.0f/s' % (n, n / secs))
