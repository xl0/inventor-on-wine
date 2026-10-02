#!/usr/bin/python3
"""Input injector daemon for the headless mutter (x/wayland.sh) via
org.gnome.Mutter.RemoteDesktop. Holds the RD session (it dies with this
process) and serves one-line commands on $XDG_RUNTIME_DIR/winj.sock:
  move X Y | rel DX DY | down|up|click [BTN=1] | scroll N | key NAME | keydown NAME | keyup NAME
  type TEXT
NAME is an X keysym name (Escape, Return, a, F5, Super_L). Replies "ok" or "err ...".
"""
import os, socket, sys, threading, time
import gi
gi.require_version('Gdk', '3.0')
from gi.repository import Gio, GLib, Gdk

BTN = {1: 0x110, 2: 0x112, 3: 0x111}  # linux BTN_LEFT/MIDDLE/RIGHT
bus = Gio.bus_get_sync(Gio.BusType.SESSION)

def call(dest, path, iface, method, args=None, sig=None):
    return bus.call_sync(dest, path, iface, method, GLib.Variant(sig, args) if sig else None,
                         None, Gio.DBusCallFlags.NONE, -1)

RD, SC = 'org.gnome.Mutter.RemoteDesktop', 'org.gnome.Mutter.ScreenCast'
rd = call(RD, '/org/gnome/Mutter/RemoteDesktop', RD, 'CreateSession').unpack()[0]
sid = call(RD, rd, RD + '.Session', 'Get', None) if False else \
    bus.call_sync(RD, rd, 'org.freedesktop.DBus.Properties', 'Get',
                  GLib.Variant('(ss)', (RD + '.Session', 'SessionId')), None, 0, -1).unpack()[0]
sc = call(SC, '/org/gnome/Mutter/ScreenCast', SC, 'CreateSession',
          ({'remote-desktop-session-id': GLib.Variant('s', sid)},), '(a{sv})').unpack()[0]
stream = call(SC, sc, SC + '.Session', 'RecordMonitor', ('Meta-0', {}), '(sa{sv})').unpack()[0]
call(RD, rd, RD + '.Session', 'Start')
S = RD + '.Session'
def n(m, args, sig): call(RD, rd, S, m, args, sig)

def keysym(name):
    k = Gdk.keyval_from_name(name)
    if not k: raise ValueError('unknown keysym ' + name)
    return k

def handle(line):
    w = line.split(None, 1)
    c, a = w[0], (w[1] if len(w) > 1 else '')
    p = a.split()
    if c == 'move': n('NotifyPointerMotionAbsolute', (stream, float(p[0]), float(p[1])), '(sdd)')
    elif c == 'rel': n('NotifyPointerMotionRelative', (float(p[0]), float(p[1])), '(dd)')
    elif c in ('down', 'up', 'click'):
        b = BTN[int(p[0]) if p else 1]
        for s in ((1,) if c == 'down' else (0,) if c == 'up' else (1, 0)):
            n('NotifyPointerButton', (b, bool(s)), '(ib)'); time.sleep(0.03)
    elif c == 'scroll': n('NotifyPointerAxisDiscrete', (0, int(p[0])), '(ui)')
    elif c in ('key', 'keydown', 'keyup'):
        k = keysym(p[0])
        for s in ((1,) if c == 'keydown' else (0,) if c == 'keyup' else (1, 0)):
            n('NotifyKeyboardKeysym', (k, bool(s)), '(ub)'); time.sleep(0.03)
    elif c == 'type':
        for ch in a:
            k = Gdk.unicode_to_keyval(ord(ch))
            for s in (1, 0): n('NotifyKeyboardKeysym', (k, bool(s)), '(ub)')
            time.sleep(0.02)
    else: raise ValueError('unknown command ' + c)

path = os.path.join(os.environ['XDG_RUNTIME_DIR'], 'winj.sock')
if os.path.exists(path): os.unlink(path)
srv = socket.socket(socket.AF_UNIX); srv.bind(path); srv.listen(8)
def serve(conn):
    with conn, conn.makefile('rw') as f:
        for line in f:
            try: handle(line.strip()); f.write('ok\n')
            except Exception as e: f.write('err %s\n' % e)
            f.flush()
def accept():
    while True: threading.Thread(target=serve, args=(srv.accept()[0],), daemon=True).start()
threading.Thread(target=accept, daemon=True).start()
GLib.MainLoop().run()
