# 062 WPF per-pixel-alpha popup (browser pane splitter) drawn as a black bar
Status: wontfix (X11 without a compositing manager can't blend; Wine keeps hit-testing right) · Owner: worker-061 · Branch: - · Found in: UI latency pass (tools/uilat wmdrag)

## Symptom (integ 061fa687382, :98 openbox, no compositing manager)
In a part document a solid black 5 px vertical bar sits between the Model browser and the
viewport (maximized: x 240-245, y 145-1055), a second one between the viewport and the
Autodesk Assistant panel. When the main window is moved they stay at their old screen position
until the move ends (and punch the black trail of 061 into the viewport while it slides under).
Left Windows (VM), right Wine: ![splitter](attachments/062-splitter-windows-vs-wine.png)

## Window
`HwndWrapper[DefaultDomain;;...]`, 5 px x pane height, style 96080000
(WS_POPUP|WS_VISIBLE|WS_CLIPSIBLINGS|WS_CLIPCHILDREN|WS_SYSMENU), ex 00080080
(WS_EX_LAYERED|WS_EX_TOOLWINDOW), owned by Inventor's main window, Inventor.exe.
WPF AllowsTransparency window: UpdateLayeredWindow(ULW_ALPHA, AC_SRC_ALPHA, a 32 bpp DIB).
Content (Wine trace and Windows measurement agree): every pixel premultiplied 0x03030303 or
0x06030303, i.e. alpha 3-6 (1-2%): a hit-test surface, meant to be invisible.
X11 (winex11): ARGB visual (depth 32), managed (WS_POPUP|WS_SYSMENU counts as a caption in
is_window_managed), _NET_WM_WINDOW_TYPE_DIALOG, WM_TRANSIENT_FOR the main window,
SKIP_TASKBAR/PAGER, no decorations. openbox and awesome both keep it where Wine puts it.

## Windows ground truth (VM, tests/layered_popup_probe.exe, tests/layered_alpha.exe)
- Invisible: screen over it (CAPTUREBLT grab) = its neighbours. Composited alpha measured by
  grabbing it over our own white and black windows: 3-6, colour 3 (same bits as on Wine).
- Hit-testable: WindowFromPoint over it returns the popup; it's the pane-resize handle.
- Screen-fixed during a caption drag (the modal move loop): the popups' screen x doesn't change
  while the main window moves 240 px, and they jump to the new place after the button release.
  After a plain SetWindowPos move of the main window (another process) they stay behind for good.
- layered_alpha.exe (ULW_ALPHA popup with bands of alpha 0/1/16/64/128/255 over a red window):
  screen = red blended by alpha; WindowFromPoint and a SendInput click hit the popup for every
  alpha > 0, the window below for alpha 0.

## Cause
Without a compositing manager X can't blend: winex11 gives layered windows an ARGB visual
(which a compositor would blend correctly) and additionally cuts pixels with alpha == 0 out of
the window shape (dce.c set_surface_shape). Pixels with any other alpha are drawn opaque with
their premultiplied colour: alpha 3 of white = (3,3,3), black.
Wine on Xvfb (no WM) with layered_alpha.exe: screen 000000 for every alpha > 0, clicks right;
WindowFromPoint wrong for alpha 0/1 (080).

## Why no fix
An X window region (bounding shape) is both what is drawn and what gets input; the input shape
can only be a subset. So without a compositor a pixel is either drawn opaque or click-through.
Cutting low-alpha pixels out of the shape makes the bar invisible but kills the splitter:
tested by shaping the X window empty on :101 — xdotool drags at the splitter then no longer
resize the browser pane (with the black bar they do). Keeping input correct (current Wine)
is the better trade-off. Other routes (InputOnly sibling windows, background None, Wine
compositing its own windows) are hacks with worse failure modes.
With a compositing manager (picom etc.) the ARGB visual gives the Windows look (per the code;
not verified here, no compositor installed on the server).

## Related
- 061: the black trail during drags (fixed there, independent of the bar colour).
- 042: the same limitation for WPF popup shadows.
- 077: after WM-driven moves (awesome Mod4+drag) the popups stay at the old screen position.
