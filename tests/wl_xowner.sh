#!/usr/bin/env bash
# All tests/wl_xowner.exe cases (issues 134, 135) in the headless Wayland session (x/wayland.sh start first):
# screenshots are checked by colour (B green 400x300, C blue 300x200 must be fully visible above their owners,
# before and after clicking the owner), WAYLAND_DEBUG logs for protocol errors. PASS/FAIL per check on stdout.
#   WINE_BUILD=wt/134-build tests/wl_xowner.sh      (default wt/wayland-build; prefix /dev/shm/wl-prefix must exist)
# Output (logs, screenshots): $OUT, default /tmp/wl-xdg/xowner.
cd "$(dirname "$0")/.."
eval "$(x/wayland.sh env)"
export WINEPREFIX=${WINEPREFIX:-/dev/shm/wl-prefix} WINEDLLOVERRIDES="mscoree,mshtml="
B=${WINE_BUILD:-wt/wayland-build}; W=$B/wine; SRV=$B/server/wineserver
OUT=${OUT:-/tmp/wl-xdg/xowner}; mkdir -p $OUT
FULL_B=105592 FULL_C=49392   # client area pixels of a 400x300 / 300x200 WS_POPUP|WS_CAPTION window

# chk SHOT [COLOUR]: pixels + bbox per colour, and a click point well inside COLOUR (default A.red)
chk() { python3 - "$@" <<'EOF'
import sys
import numpy as np
from PIL import Image
im = np.asarray(Image.open(sys.argv[1]).convert('RGB')).astype(int)
cols = {'A.red': (255, 0, 0), 'A2.yellow': (255, 255, 0), 'B.green': (0, 255, 0), 'C.blue': (0, 0, 255)}
masks = {}
for name, c in cols.items():
    m = masks[name] = (im[:, :, 0] == c[0]) & (im[:, :, 1] == c[1]) & (im[:, :, 2] == c[2])
    ys, xs = np.nonzero(m)
    print(f'{name}: {m.sum()}' + (f' bbox ({xs.min()},{ys.min()}) {xs.max() - xs.min() + 1}x{ys.max() - ys.min() + 1}' if m.sum() else ''))
m = masks[sys.argv[2] if len(sys.argv) > 2 else 'A.red']
ys, xs = np.nonzero(m)
for i in np.argsort(-(xs + ys))[::997]:   # a point whose 61x61 neighbourhood is all that colour
    x, y = xs[i], ys[i]
    if x > 40 and y > 40 and m[y - 30:y + 31, x - 30:x + 31].all():
        print(f'click {x - 10} {y - 10}')
        break
EOF
}
shot() { x/wshot.sh $OUT/$1.png >/dev/null; chk $OUT/$1.png ${2:-A.red} >$OUT/chk.out; }
px() { awk -v c="$1:" '$1==c{print $2}' $OUT/chk.out; }
ck() { shot "$1" "${4:-A.red}"; [ "$(px "$2")" = "$3" ] && echo "PASS $1: $2 $3 px" || echo "FAIL $1: $2 $(px "$2") px, expected $3"; }
clickc() { x/wshot.sh move $(awk '/^click/{print $2, $3}' $OUT/chk.out) >/dev/null; x/wshot.sh click >/dev/null; sleep 1.5; }
start() { local n=$1; shift; (WAYLAND_DEBUG=1 WINEDEBUG=-all,err+all setsid nohup $W tests/wl_xowner.exe "$@" >$OUT/$n.log 2>&1 &); }
hwnd() { awk -F= '/^owner=/{print $2}' $OUT/$1.log | tr -d '\r'; }
fin() { local n; for n in "$@"; do echo "  $n: protocol errors $(grep -a -ciE 'wl_display@1\.error|protocol error' $OUT/$n.log)," \
        "set_parent $(grep -a -c 'set_parent(' $OUT/$n.log), set_parent_of $(grep -a -c 'set_parent_of' $OUT/$n.log)"; done; $SRV -k; sleep 1; }
$SRV -k 2>/dev/null; sleep 1

echo "== self: modal-style B owned by the disabled A"
start self self; sleep 5; ck self-1 B.green $FULL_B; clickc; ck self-2 B.green $FULL_B; clickc; ck self-3 B.green $FULL_B
echo "  foreground after clicking A: $(WINEDEBUG=-all $W tests/wl_winctl.exe 0 x | tail -1), B = $(WINEDEBUG=-all $W tests/wl_winlist.exe | awk '/B modal/{print $1}')"
fin self

echo "== chain: A owns B owns C"
start chain chain; sleep 5; ck chain-1 C.blue $FULL_C; b0=$(px B.green); clickc; ck chain-2 C.blue $FULL_C
[ "$(px B.green)" = "$b0" ] && echo "PASS chain-2: B unchanged ($b0 px)" || echo "FAIL chain-2: B $(px B.green) px, was $b0"
shot chain-2b B.green; clickc; ck chain-3 C.blue $FULL_C; fin chain

echo "== late: B shown before its owner A"
start late late 4000; sleep 2.5; ck late-1 B.green $FULL_B B.green; sleep 4; ck late-2 B.green $FULL_B; clickc; ck late-3 B.green $FULL_B; fin late

echo "== hide: A hidden, shown again, destroyed"
start hide hide 5000; sleep 3; ck hide-1 B.green $FULL_B; sleep 5; ck hide-2 B.green $FULL_B B.green
sleep 5; ck hide-3 B.green $FULL_B; clickc; ck hide-4 B.green $FULL_B; sleep 9; ck hide-5 B.green 0; fin hide

echo "== reowner: SetWindowLongPtr(GWLP_HWNDPARENT), then a stale parent that would make a loop"
start reowner reowner 8000; sleep 3; ck re-1 B.green $FULL_B; clickc; ck re-1b B.green $FULL_B; sleep 4; ck re-2 B.green $FULL_B A2.yellow; clickc; ck re-2b B.green $FULL_B
sleep 12; grep -a "set_parent(" $OUT/reowner.log | sed 's/^/  /'; fin reowner

echo "== cross-process: B in another process"
start xa owner; sleep 4; start xb $(hwnd xa); sleep 4
ck x-1 B.green $FULL_B; clickc; ck x-2 B.green $FULL_B; clickc; ck x-3 B.green $FULL_B; fin xa xb

echo "== cross-process, owner hidden and shown again"
start xha owner hide; sleep 3; start xhb $(hwnd xha); sleep 4
ck xh-1 B.green $FULL_B; sleep 7; ck xh-2 B.green $FULL_B B.green; sleep 4; ck xh-3 B.green $FULL_B; clickc; ck xh-4 B.green $FULL_B; fin xha xhb
