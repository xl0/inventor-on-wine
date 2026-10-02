# 062 WPF per-pixel-alpha popup (browser pane splitter) drawn as a black bar
Status: compositor path fixed on fix/062 (f3c780105f5); without a compositing manager unchanged (black bar, by decision). Inventor check pending · Owner: worker-062 · Branch: fix/062 · Found in: UI latency pass (tools/uilat wmdrag)

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

## Reopened: don't draw nearly transparent layered windows, keep their input
User feedback (laptop, awesome, no compositor): the bar visibly lags behind window moves. On
Windows it is ~98 % transparent, so its lag (it only repositions when the move ends, same on
Windows) is invisible. Idea: without a compositor, for per-pixel-alpha layered windows, draw only
pixels above an alpha threshold (X Shape bounding region) and keep hit-testing for the remaining
alpha > 0 pixels with an InputOnly X window (or a ShapeInput region if it is not clipped by the
bounding shape) so dragging the splitter still resizes the pane. Windows hit-tests layered
per-pixel-alpha windows where alpha != 0.

## Fix (fix/062, 4 commits on integ 2016800d7a3; scoped down after review)
The first two versions (fix/062-v2 = 0058ab0721d: alpha < 128 cut from the X shape and manual
Composite redirection of the WM frame without a compositor) were dropped after an adversarial
review: a compositor couldn't start while a thread with a hidden window didn't pump, faint pixels
of mixed windows became click-through, alpha-100 popups and fades vanished under a compositor,
whole-surface rescans per flush, a crash on SetParent. X facts found on the way are kept in
notes/wine/window-surfaces.md.

Windows ground truth (VM; tests/layered_alpha.exe, layered_splitter.exe auto): a per-pixel-alpha
pixel takes clicks and WindowFromPoint for every alpha >= 1, alpha 0 falls through; the screen is
the plain blend (alpha 3-6 over green c8: 03c903). LWA_ALPHA 128 blends, takes clicks; a colour-key
hole falls through. UpdateLayeredWindow without AC_SRC_ALPHA ignores the alpha bytes (user32 test).

### What the user sees (awesome + picom)
integ, Xvfb, awesome (x/awesome-rc.lua) + `picom --config /dev/null --backend glx --no-use-damage`
(llvmpipe GLX; `--vsync` fails on Xvfb): the bar's pixels blend exactly like on Windows (03c903; ARGB
visual, awesome's frame is depth 32 too), but awesome draws its 1 px client border (the frame's X
border, black `border_normal`) around the popup: two black lines 6 px apart, which stay behind
during window moves. 8x zoom: ![border](attachments/062-awesome-picom-border.png)
Mid Mod4+drag, screen x 396-406 over the green pane: integ `c8 c8 000000 03c903 ... 000000 c8 c8`,
fix: all 00c800. openbox + picom on integ has no border (undecorated frame): 03c903.

### Design (only acts while a compositing manager owns _NET_WM_CM_Sn)
- win32u: `window_surface.alpha_faint`: UpdateLayeredWindow without a dirty rect sets it, the flush
  clears it when the dirty rect has a pixel with alpha >= 16 (no whole-surface scans; a dirty-rect
  update can only clear it). `shape_hidden` = per-pixel-alpha surface, faint, no client-surface area.
  16 (6 %): the splitter is 3-6; anything a user could notice stays as it was.
- winex11: a hidden window gets _NET_WM_WINDOW_OPACITY 0 while a compositing manager runs (awesome
  and openbox copy the property to their frame, so the border goes too); otherwise the opacity is
  the layered alpha as before (now kept in the window data, so it comes back). The state is set
  from the surface flush when the window data lock is free (else a posted WM_X11DRV_SET_HIDDEN
  re-flushes), so a window that fades in without pumping messages shows at once.
- Every thread selects XFixes selection events for _NET_WM_CM_Sn (clipboard.c's XFixes loading is
  shared, one handler for both selections) and re-applies the opacity of its hidden windows.
- Separate commit: UpdateLayeredWindow only uses the alpha channel with AC_SRC_ALPHA (alpha byte 0
  pixels were cut from the shape).
- Without a compositing manager nothing changes: the bar is black as on integ (left half of
  ![probe](attachments/062-probe-before-after.png); the right half was the dropped v2).

### Limits
- Hidden windows are fully invisible under a compositor (Windows: 1-2 % visible); all-alpha < 16.
- awesome doesn't shape its frame: alpha 0 holes of a managed layered window swallow clicks there.
- A thread that doesn't pump only gets the opacity change for a compositor start/stop when it
  pumps again. Without libXfixes the compositor state is the one at process start.
- UpdateLayeredWindowIndirect with dirty rects only: never hidden. A compositor that doesn't own
  _NET_WM_CM_Sn isn't seen.

### Tests (Xvfb; reviewer's lw.c/bench.c, tests/layered_splitter.sh, OLD = build/ d7799da4d5c)
lw.exe, 18 modes, OLD vs NEW output diff:
- openbox and awesome without compositor: identical except `noalpha 0` (AC_SRC_ALPHA fix: ffffff, was cut).
- openbox + picom, awesome + picom: `uni 3`, `uni 12`, `slwa` faint phase, `nopump`, unmanaged `uni 3`:
  03c903 / 0ccb0c -> 00c800 (hidden); `noalpha 0`; everything else identical: uni 20/100/255, mix,
  the three fades (pix pumped / not pumped, constant alpha), slwa 128 (006480), lwa, reparent survives.
- bench.exe 16x16 dirty ULWI: 0.09-0.12 ms old and new (1280x800 and 3840x2160); full ULW 7-8 ms both.
- picom started while `lw.exe nopump` sleeps: starts without error; the popup is blended until
  the thread pumps, then hidden. picom stopped/started under the splitter probe (awesome): bar
  000000 030303 000000 without, 00c800 with, both ways.
layered_splitter.sh drag / move / cycle with xdotool:
| | openbox | awesome | openbox + picom | awesome + picom |
|---|---|---|---|---|
| bar on screen | 030303 (as integ) | 030303 + border (as integ) | 00c800 (integ 03c903) | 00c800, no border |
| drag the bar (capture), after WM move, after hide/show, after resize | ok | ok | ok | ok |
| click on alpha 0 rows | main | frame swallows | main | frame swallows |
| shadow: screen / click | 000000 / tip | same | 00009c = VM / tip | same |
user32:win test_layered_window_alpha (alpha 100 everywhere still shown; no AC_SRC_ALPHA: opaque):
VM 0 failures; Wine passes without compositor and under openbox + picom. The whole unit under awesome
is noisy (33-34 failures with or without the fix).
regress subset (14 modules, 204 units) vs d7799da4d5c: 0 worse.
Not tried on the NVIDIA Xorg, nor in Inventor.
