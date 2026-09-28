# 040 Custom title bar (client over caption) replaced by WM decorations
Status: open (draft) · Owner: - · Branch: - · Found in: UI test campaign (main window)

## Symptom (integ d53133a66a1, :98 openbox)
Inventor's main window (AfxMDIFrame140u, style 0x15cf0000 = WS_CAPTION|
WS_THICKFRAME|WS_MAXIMIZE...) extends its client area over the caption
(WM_NCCALCSIZE): client origin is screen 0,0 like on Windows (window
-4,-4..1924,1084, client 1920x1078 at 0,0; VM: -8,-8, client at 0,0). The top
26 px of the client hold `AdImpApplicationFrame` with the Quick Access Toolbar
(QATHwndSource: New/Open/Save/Undo/Redo/Home...) and InfoCenter (Search Help &
Commands, account, trial counter). On Wine that strip is not shown: the X
window starts at y=26 (xwininfo), openbox draws its own 16 px title bar
(_NET_FRAME_EXTENTS 1,1,16,1) and the rest is black. QAT and InfoCenter are
unreachable by mouse (Ctrl+Z/Y etc. still work).
- VM: ![vm](attachments/040-titlebar-vm.png)
- Wine: ![wine](attachments/040-titlebar-wine.png)

## Cause (suspected)
win32u `get_visible_rect()` subtracts the style-based NC size
(adjust_window_rect with the driver's style mask) from the window rect even when
the app's client rect covers that area; winex11 then maps the X window at the
visible rect and asks the WM for a title (MWM_DECOR_TITLE). Only
`window == client` disables it. The visible rect should not cut into the
client area (or the window should be undecorated when the client covers the
caption). Registry `HKCU\Software\Wine\X11 Driver\Decorated=N` presumably
avoids it globally (not tried; campaign keeps defaults).

## Repro
Any window whose WM_NCCALCSIZE returns the window rect minus side borders only
(caption area in client), maximized, under a WM. Inventor: start it on :98.
