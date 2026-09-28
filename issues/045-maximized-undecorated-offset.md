# 045 Maximized undecorated window drawn shifted by its off-screen frame
Status: open (draft) · Owner: - · Branch: - · Found in: 040 (fix/040 build, Xvfb + openbox)

## Symptom
A maximized window whose Win32 rect hangs off-screen by the frame width (window
-4,-4..1028,772 on a 1024x768 screen, client at 0,0) and which gets no WM
decorations (visible rect == window rect: WM_NCCALCSIZE custom caption after 040,
window == client, or no WS_CAPTION with WS_THICKFRAME) is shown shifted: winex11
asks for _NET_WM_STATE_MAXIMIZED, openbox places the X window at the work area
(0,0 1024x768) while the surface origin is the visible rect (-4,-4). Wine's 4 px
frame shows at the top/left, the client loses 4 px at the right/bottom. Win32 rects
stay right (the config change is ignored as "fullscreen on the same monitor").
Windows: the frame is simply off-screen, client pixel (0,0) at screen (0,0).

## Repro
`tests/custom_caption.exe max 12` (maximizes 1 s after the first map) on the
fix/040 build under openbox; xwininfo shows the X window 1024x768+0+0.

## Ideas
Visible rect of a maximized undecorated window = window rect clipped to the
monitor (or the WM geometry mapped back with the off-screen offset). Check what
the window == client (Steam, bug 40930) case does before picking one.

## Related (unverified, same setup)
`custom_caption.exe plainmax 12` (default NC, decorated): SW_MAXIMIZE after map is
undone by openbox (Win32 ends restored). Likely WM_NORMAL_HINTS min == max because
the window rect covers the monitor (WINE_SWP_RESIZABLE dropped), so openbox refuses
MAXIMIZED and Wine syncs SC_RESTORE back. Maximizing before the first map leaves
WS_MAXIMIZE set with the restored X geometry.
