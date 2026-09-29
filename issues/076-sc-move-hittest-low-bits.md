# 076 SC_MOVE with low bits != HTCAPTION doesn't move the window (Wine's own move loop)
Status: fixed · Owner: worker-061 · Branch: fix/061 (11a48992263) · Found in: 061 WM comparison (awesome)

## Symptom
Under awesome 4.3 (the user's WM) dragging Inventor's app-drawn caption does nothing.
awesome doesn't implement _NET_WM_MOVERESIZE (not in _NET_SUPPORTED), so X11DRV_SysCommand
declines and win32u's sys_command_size_move() runs. Inventor sends WM_SYSCOMMAND 0xf011
(SC_MOVE | HTCLIENT); the loop only offsets the rect for HTCAPTION/HTBORDER (and for 0xf01a
etc. resizes the matching border instead).

## Windows ground truth
tests/sc_move_hittest.exe (WM_LBUTTONDOWN -> SC_MOVE | n, SendInput drag from another thread):
f011/f012/f013/f014/f01f all move with the mouse; f010 (keyboard move) doesn't.
Posted-message loops (like test_SC_SIZE) don't work for SC_MOVE on Windows: it needs a real press.

## Fix
`win32u: Move the window on SC_MOVE whatever the hittest in the low bits.`: after the
keyboard-move setup, SC_MOVE always uses HTCAPTION. Test: user32 win.c test_SC_MOVE_hittest
(SendInput, MA_NOACTIVATE popup so winex11 keeps it unmanaged and uses the win32u loop on any
WM): VM pass; Wine without the fix 3 failures (f011 not moved, f01a / f01f resized), with it pass.
Inventor under awesome: caption drag 60 fps, lag p50 3.5 ms (061 table).
