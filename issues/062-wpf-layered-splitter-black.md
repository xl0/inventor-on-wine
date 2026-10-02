# 062 WPF per-pixel-alpha popup (browser pane splitter) drawn as a black bar
Status: fixed on fix/062 (0058ab0721d), Inventor check by the coordinator pending · Owner: worker-062 · Branch: fix/062 · Found in: UI latency pass (tools/uilat wmdrag)

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

## Fix (fix/062, 4 commits on integ d7799da4d5c)
Left: old drawing, right: fix (tests/layered_splitter.exe, openbox, no compositor)
![probe](attachments/062-probe-before-after.png)

Windows ground truth (VM; tests/layered_alpha.exe, layered_splitter.exe auto): a per-pixel-alpha
pixel takes clicks and WindowFromPoint for every alpha >= 1, alpha 0 falls through; the screen is
the plain blend (alpha 3-6 over green c8: 03c903). LWA_ALPHA 128 blends, takes clicks; a colour-key
hole falls through.

### With a compositing manager (the user's setup: awesome + picom)
integ d7799da4d5c, Xvfb, awesome (x/awesome-rc.lua) + `picom --config /dev/null --backend glx
--no-use-damage` (llvmpipe GLX; `--vsync` fails on Xvfb: "Failed to load a swap control extension"):
the bar's pixels blend exactly like on Windows (03c903; ARGB visual, awesome's frame is depth 32 too;
shadow 00009c = VM), but awesome draws its 1 px client border (the frame's X border, black
`border_normal`) around the popup: two black lines 6 px apart, which stay behind during window
moves. That is the user's bar. 8x zoom: ![border](attachments/062-awesome-picom-border.png)
Mid Mod4+drag, screen x 396-406 over the green pane: integ `c8 c8 000000 03c903 ... 000000 c8 c8`,
fix: all 00c800. openbox + picom on integ has no such border (undecorated frame): 03c903.
Setting _NET_WM_WINDOW_OPACITY 0 on the client hides popup and border under picom (awesome and
openbox copy the property to their frame); input is unaffected.

### Without one
X facts (checked on Xvfb, scratch tests): the ShapeInput region is clipped by ShapeBounding, an
InputOnly child is clipped by its parent's shape, and an InputOnly/extra top-level would have to
follow the stacking the WM gives the frame. But a window that a client redirects with
XCompositeRedirectWindow(CompositeRedirectManual) while no compositing manager runs is not drawn,
doesn't clip what is below it (the server treats manually redirected windows as transparent) and
still gets all input, with its bounding shape as before. One X window, so coordinates, capture,
cursor and the WM's view are unchanged. With a WM the frame must be redirected instead: openbox's
frame is black, awesome's has no background (stale pixels) and the border.

### Design
- win32u: `window_surface.alpha_threshold` (winex11: 128) and `shape_hidden` = no pixel of a
  per-pixel-alpha surface reaches the threshold (client-surface areas count as visible). The shape
  keeps every alpha > 0 pixel then, as before. `alpha_cut` (winex11: no compositing manager):
  surfaces that aren't hidden leave pixels below the threshold out of the shape. Shapes of such
  surfaces are recomputed over the whole surface. Two side fixes it needs: ULW clears the surface
  padding (surfaces are rounded up to 128 px and start opaque white), and the padding no longer
  counts as client-surface area (fix/027 forced it opaque in the shape).
- 128 = 1-bit rounding of alpha, the nearest of the two things X can do alone; WPF's stock drop
  shadow peaks at alpha 113 (#71000000, from memory of the open-source WPF, not measured), so
  shadows go away completely instead of leaving a rim.
- winex11, hidden window (state arrives by a posted WM_X11DRV_SET_HIDDEN: the flush can run with
  the non-recursive window data mutex held): _NET_WM_WINDOW_OPACITY 0 on the window (kept across
  UpdateLayeredWindow calls, the constant alpha comes back when a pixel becomes visible), and, when
  _NET_WM_CM_Sn has no owner, manual redirection of the outermost X ancestor below the root (WM
  frame, or the window itself), redone on ReparentNotify.
- Compositing manager changes: every thread selects XFixes selection events for _NET_WM_CM_Sn on
  the root (clipboard.c's XFixes loading is shared, one handler for both selections). On a change
  the thread sets alpha_cut of its layered surfaces, re-flushes them and redirects / un-redirects.
- With a compositing manager windows with visible pixels are untouched (ARGB visual, alpha > 0 shape).

### Limits (X can't do better with one window)
- No compositor, window with both visible and faint pixels: the faint ones (0 < alpha < 128:
  shadows, AA edges) are not drawn and click-through; Windows hit-tests them. layered_alpha.exe:
  alpha 1/16/64 bands show the window below and click it. A translucent overlay below 50% inside a
  window that also has opaque pixels disappears. (With a compositor: blended and hit-tested.)
- Hidden windows are fully invisible with a compositor (Windows: 1-2 % visible).
- awesome doesn't shape its frame: alpha 0 holes of a managed layered window swallow clicks there
  (before and after; openbox and no-WM pass them through).
- Compositor start: it can only redirect the root's children once ours are un-redirected (one
  manual redirection per window, else BadAccess). picom takes the selection before redirecting,
  so the thread's XFixes event gets there first in the tests (4 starts, no error); a thread that
  doesn't pump messages would lose that race. Without libXfixes only new surfaces follow.
- First show: the frame is visible until ReparentNotify is processed (a few ms).
- UpdateLayeredWindowIndirect with a dirty rect on a fresh surface leaves the padding opaque:
  such a window is never hidden (old look). DPI-scaled surfaces likewise.

### Tests (Xvfb; tests/layered_splitter.sh drag/move/cycle drives layered_splitter.exe with xdotool)
| | no WM | openbox | awesome | openbox + picom | awesome + picom |
|---|---|---|---|---|---|
| bar pixels on screen | 00c800 = pane (was 030303) | 00c800 | 00c800, no border | 00c800 (integ 03c903) | 00c800, no border (integ: 03c903 between black lines) |
| press on the bar, drag 60 px out of it (capture), release | split 200->257 | same | same | same | same |
| click on the bar's alpha 0 rows | main | main | swallowed by the frame | main | swallowed by the frame |
| tooltip shadow / body | 0000c8 (was 000000) / f0f0f0 | same | same | 00009c = VM / f0f0f0 | same |
| click on shadow / body | main / tip | same | same | tip / tip (= Windows) | same |
| WM move (title drag; awesome Mod4+drag): mid-move, after | - | invisible, follows at the end, drag ok | same | same | same |
| hide + show, resize (new surface) | hide/show ok | ok | ok (awesome re-places a re-shown window) | ok | ok |
| picom killed / started while the probe runs | - | - | - | bar invisible, shadow 0000c8 <-> 00009c, drag ok, picom starts | same, border stays invisible |
layered_alpha.exe under picom: screen fe/ef/bf/7f like the VM, clicks like the VM; `kinds`
(LWA_ALPHA 128, colour key + hole, opaque ULW) unchanged everywhere (7f0080 under picom).
layered_child_gpu.exe (colour key + D3D child, lavapipe): child 00ff00. expose_present.exe ok.
user32:win test_layered_window_alpha (screen under alpha 3 pixels = the window below, mixed and
all-faint): VM 0 failures; Wine passes without compositor (old code: 030303) and under openbox+picom.
awesome's placement rule moves newly mapped managed popups into free screen space (seen with the
small probe window; Inventor repositions its popups afterwards): the probe re-places the bar.
The GLX backend ran on Xvfb/llvmpipe; not tried on the NVIDIA Xorg, nor in Inventor.
