# 079 SW_RESTORE of a window maximized under awesome leaves the X window maximized
Status: open (draft, seen once) · Owner: - · Branch: - · Found in: 061 WM comparison (awesome)

## Symptom
Inventor (inv4, :101) started under awesome 4.3 comes up maximized. ShowWindow(SW_RESTORE) +
SetWindowPos(40,40,1400,850) from another process: Win32 says 1400x850 at 40,40, but the X
window stays 1918x1063 with _NET_WM_STATE_MAXIMIZED_VERT/HORZ; Inventor draws its 1400x850 into
the top-left of a black 1918x1063 window and clicks land offset. xdotool windowsize and an
EWMH _NET_WM_STATE remove did nothing; awesome-client `c.maximized = false` fixed it.
Not seen under openbox (same steps).

## To check
Whether winex11 sends the _NET_WM_STATE remove on restore under awesome (WINEDEBUG=+x11drv
window state trace) and whether awesome 4.3 honours it; the user runs awesome.
