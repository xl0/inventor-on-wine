#!/usr/bin/env python3
"""Drive the VM's keyboard/mouse over QMP. Coordinates are screen pixels.
Usage: vm/input.py click X Y | dclick X Y | key COMBO... | type TEXT
  COMBO: qcodes joined by '-', e.g. ret, tab, ctrl-alt-delete, alt-f4, meta_l-r
"""
import json, os, socket, sys, time

def qmp():
    s = socket.socket(socket.AF_UNIX)
    s.connect(os.path.join(os.environ.get('VM_DIR', os.path.dirname(os.path.abspath(__file__))), 'qmp.sock'))
    f = s.makefile('rw')
    json.loads(f.readline())  # greeting
    def cmd(name, **args):
        f.write(json.dumps({'execute': name, 'arguments': args}) + '\n'); f.flush()
        while True:  # skip async events
            r = json.loads(f.readline())
            if 'return' in r or 'error' in r:
                if 'error' in r: sys.exit(f"QMP error: {r['error']}")
                return r['return']
    cmd('qmp_capabilities')
    return cmd

def screen_size():
    # std VGA mode is whatever Windows set; read it from a screendump header.
    import subprocess, tempfile
    png = subprocess.check_output([os.path.join(os.path.dirname(os.path.abspath(__file__)), 'shot.sh'),
                                   tempfile.mktemp(suffix='.png')]).decode().strip()
    from PIL import Image
    w, h = Image.open(png).size; os.remove(png)
    return w, h

def keys(cmd, combo):
    ks = [{'type': 'qcode', 'data': k} for k in combo.split('-')]
    cmd('send-key', keys=ks)

def click(cmd, x, y, n=1):
    w, h = screen_size()
    ax = lambda axis, v: {'type': 'abs', 'data': {'axis': axis, 'value': v}}
    btn = lambda down: {'type': 'btn', 'data': {'down': down, 'button': 'left'}}
    cmd('input-send-event', events=[ax('x', int(x * 0x7fff / (w - 1))), ax('y', int(y * 0x7fff / (h - 1)))])
    for _ in range(n):
        cmd('input-send-event', events=[btn(True)]); time.sleep(0.05)
        cmd('input-send-event', events=[btn(False)]); time.sleep(0.05)

QCODE = {' ': 'spc', '\n': 'ret', '\t': 'tab', '.': 'dot', ',': 'comma', '/': 'slash', '\\': 'backslash',
         '-': 'minus', '=': 'equal', ';': 'semicolon', "'": 'apostrophe', '[': 'bracket_left', ']': 'bracket_right'}
SHIFTED = {':': 'semicolon', '_': 'minus', '"': 'apostrophe', '?': 'slash', '!': '1', '@': '2', '#': '3',
           '$': '4', '%': '5', '^': '6', '&': '7', '*': '8', '(': '9', ')': '0', '+': 'equal', '<': 'comma', '>': 'dot'}

def type_text(cmd, text):
    for c in text:
        if c in SHIFTED: ks = ['shift', SHIFTED[c]]
        elif c.isupper(): ks = ['shift', c.lower()]
        else: ks = [QCODE.get(c, c)]
        cmd('send-key', keys=[{'type': 'qcode', 'data': k} for k in ks]); time.sleep(0.02)

if __name__ == '__main__':
    op, args = sys.argv[1], sys.argv[2:]
    cmd = qmp()
    if op == 'click': click(cmd, int(args[0]), int(args[1]))
    elif op == 'dclick': click(cmd, int(args[0]), int(args[1]), 2)
    elif op == 'key': [keys(cmd, c) for c in args]
    elif op == 'type': type_text(cmd, ' '.join(args))
    else: sys.exit(__doc__)
