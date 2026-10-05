# 175 Moving Inventor to another virtual desktop leaves the splitter bar behind
Status: fixed on fix/175 (2 commits on integ b5d75449ffe), probes only — Inventor not verified (licence seat in use) · Owner: worker-175 · Branch: fix/175 (wt/175) · Found in: user's laptop (awesome + picom)

## Symptom
Laptop: awesome WM on X11 (NVIDIA), picom (`--backend glx --vsync --no-use-damage`), 144 DPI, Wine
`wine-11.18-537-gb5d75449ffe`. Moving Inventor's main window to another virtual desktop (awesome tag)
leaves a window on the old desktop: "No window title, just the split bar outline" — the outline of the
splitter between the browser pane and the viewport.

## Why it is a window, and why the WM sees it
### On Windows it is a separate top-level too, by Autodesk's design
Static analysis of Inventor 2027.1 (Autodesk code only; `InvDockUI.dll` IL + BAML, `InvDock.dll` / `FwUI.dll`
with Ghidra; WPF from the public dotnet/wpf `release/10.0` source). Established unless marked inferred.
- Inventor's frame is not WPF: `MFCxDocFrameWnd` (FwUI.dll) derives from MFC `CMDIFrameWndEx`, the panes are
  `InvDockablePane : CDockablePane`, the real splitter is `InvPaneDivider : CPaneDivider` (InvDock.dll) — all
  Win32 child HWNDs, the viewport a Direct3D child. There is no WPF visual tree the grip could be an element of.
- On top of each MFC divider Inventor puts a WPF window: `Autodesk.Inventor.InvDockUI.PaneBorder :
  System.Windows.Window` (`Bin/InvDockUI.dll`, 40 KB, pure IL). The native side asks for it over an in-process
  message channel ("DockUI"): `InvPaneDivider::OnShowWindow` -> `CreatePaneBorder(hwnd, flags)`, `OnMove` /
  `OnSizeParent` -> `MovePane`, `OnSize` -> `ResizePane`, `OnShowWindow` -> `ShowPane` (`Window.Show()/Hide()`),
  `OnDestroy` -> `RemovePaneBorder`. The dividers between tiled documents (`InvDividerWnd`) use the same class.
  `InvDockCommTerm.CreatePaneBorder`: `new PaneBorder(dir)` (5.0 WPF units thick), `SetPaneDivider(hwnd)` =
  `new WindowInteropHelper(this).Owner = hwnd` (the divider's child HWND; Windows resolves that to the top-level
  frame — inferred, matches the observed owner), `SetDividerRect(...)`, `Window.Show()`.
- XAML root: `AllowsTransparency="True" WindowStyle="None" Background=Transparent ResizeMode="NoResize"
  ShowInTaskbar="False" ShowActivated="False"`; no Topmost. Content: a `Thumb` (cursor SizeWE/SizeNS) whose
  template is two rectangles with `Opacity=0.01` — the measured alpha 3 / 6 — and a trigger
  `IsMouseOver` -> inner rectangle `Opacity=1` (`PaneDividerColor`, #1A1F26 in the dark theme): hovering makes
  the bar visible. Dragging the Thumb sends `OnDragPaneMove` back to the MFC divider.
- Why a top-level (inferred from the above, no comment says so): WPF "airspace". The grip has to blend over and
  take the mouse from the child HWNDs on both sides of the border; WPF content cannot draw over other HWNDs, and
  a child HWND cannot be per-pixel transparent over its siblings. A layered top-level is the only way.
- Style bits, 0x96080000 / 0x00080080:
  | bit | from |
  |---|---|
  | WS_SYSMENU, WS_CLIPCHILDREN | WPF default for every `Window`: `Window.cs` `CreateAllStyle()` "We always have the sysmenu", `_Style = WS_CLIPCHILDREN \| WS_SYSMENU` (l. 2413) |
  | no WS_CAPTION | `WindowStyle.None` (`CreateWindowStyle`, `CorrectStyleForBorderlessWindowCase`: "We should really be using WS_POPUP for borderless windows, but ...") |
  | WS_EX_LAYERED | `AllowsTransparency` -> `UsesPerPixelOpacity` (`HwndSource.cs` l. 247) |
  | WS_POPUP, WS_EX_TOOLWINDOW | Autodesk: `PaneBorder.OnSourceInitialized` -> `NativeMethods.ModifyBorderStyle`: `SetWindowLongPtr(GWL_STYLE, old \| 0x80000000)`, `(GWL_EXSTYLE, old \| 0x80)`, before the first show |
  | WS_CLIPSIBLINGS | Windows adds it to popups |
  | no WS_EX_NOACTIVATE / TOPMOST | it is not a WPF `Popup` (that would be `WS_EX_TOOLWINDOW \| WS_EX_NOACTIVATE`, `MA_NOACTIVATE`) |
  Shown with `SW_SHOWNA` (`ShowActivated=False`); a click activates it (no MA_NOACTIVATE), Autodesk gives the
  activation back in `StartDragBorder`: `SetActiveWindow(GetAncestor(divider, GA_ROOT))`.
- Same helper, same bits: `MiniFrameBorder : Window`, four per floating pane frame (left/top/right/bottom, 6.0
  thick, alpha-1 brushes plus visible drop shadows), owned by the floating frame (itself owned by the main
  window). So an undocked browser is a chain main -> mini frame -> 4 border popups.
- Nothing in InvDock/InvDockUI reacts to the owner being hidden, minimized or moved to another desktop: it relies
  on Win32 keeping owned windows with their owner.

### On X11 winex11 hands it to the window manager
`is_window_managed()` (dlls/winex11.drv/window.c): not activated on show, no caption, no thick frame, then
"popup with sysmenu == caption are managed": `WS_POPUP|WS_SYSMENU` returns TRUE before any tool-window or layered
consideration. History: the rule is abcbcc35da3 (2007, "Popup windows with system menu are managed"); until
1f99d80c069 (2008, "Don't force tool windows to unmanaged mode. The detection algorithm should be good enough
for that case now.") a `WS_EX_TOOLWINDOW` window without caption was forced unmanaged. Upstream master (11.19) has
the same function. So the WM gets a 5 px wide client and applies its policy to it: tag/desktop, border,
placement, focus. Verified on the probe (same style bits); the real window: 062 recorded "managed,
_NET_WM_WINDOW_TYPE_DIALOG, WM_TRANSIENT_FOR the main window"; xprop on Inventor itself is still to do (seat).

## What the X side looks like (tests/r175/owned.exe, awesome 4.3, integ)
| window (style) | X window | WM_TRANSIENT_FOR / group | type | _NET_WM_DESKTOP | awesome |
|---|---|---|---|---|---|
| owner (WS_OVERLAPPEDWINDOW) | managed | - / itself | NORMAL | set by the WM | client, tiled |
| bar (WS_POPUP\|WS_SYSMENU, LAYERED\|TOOLWINDOW, alpha 3-6) | managed, depth 32, opacity property | owner's whole window, at map time | DIALOG | set by the WM | client, floating, 1 px border, skip_taskbar |
| dlg (WS_POPUP\|WS_CAPTION, DLGMODALFRAME) | managed | owner | DIALOG | WM | same |
| tool (WS_CAPTION\|WS_THICKFRAME, TOOLWINDOW) | managed | owner | NORMAL | WM | same (awesome: type dialog) |
| early (WS_POPUP\|WS_SYSMENU shown before the owner) | managed | owner (the owner's X window exists unmapped) | DIALOG | WM | same |
| sub (dialog owned by dlg) | managed | dlg | DIALOG | WM | same |
| xproc (dialog of another process) | managed | owner | DIALOG | WM | same |
| noact (WS_POPUP, NOACTIVATE\|TOPMOST), plain (WS_POPUP) | override-redirect | owner | DIALOG | none | not a client: on every tag, above everything |
No window has WM_CLIENT_LEADER. So option (a) is moot: WM_TRANSIENT_FOR is correct and there before the map.

## What the window managers do
Plain X clients (tests/r175/xtransient.c: parent, transient, transient of the transient) and the Wine probe:
| | transients when a window changes desktop | a window on a hidden desktop | what Wine sees for the owner |
|---|---|---|---|
| awesome 4.3 | never moved: `awful.ewmh.tag` gives a transient its parent's tags only when it is mapped; `c:move_to_tag`, Mod4+Shift+N and a pager `_NET_WM_DESKTOP` message move one client (parent 1, child 0, grandchild 0) | frame unmapped, client IsUnviewable, WM_STATE stays Normal | only PropertyNotify `_NET_WM_DESKTOP`; Win32 state unchanged |
| openbox 3.6 | the whole transient tree moves, whichever member is asked (1,1,1) | WM_STATE Iconic | winex11 minimizes the window (SC_MINIMIZE; also on a plain desktop switch) and restores it when it is shown again; win32u hides the direct owned windows meanwhile |
| mutter, KWin | not run (the Linux VM was in use). From their sources as I remember them — both move a window's transients along on a workspace change and keep windows of other workspaces mapped; treat as unverified | | |
Baseline (integ) with the probe: awesome leaves every managed owned window on the old tag (each move of the
baseline runs; a dialog created while the owner is on the other tag appears with the owner, then stays there when
the owner comes back). openbox moves them, but see "Not fixed here".

## Windows ground truth (Win11 VM, `owned.exe auto`: 0 failures; Wine: 8, see draft 180)
Minimizing the owner (ShowWindow(SW_MINIMIZE) and WM_SYSCOMMAND SC_MINIMIZE alike) hides every window in its
owner chain — the layered popup, dialogs, tool window, the WS_EX_NOACTIVATE / plain popups and the dialog owned
by a dialog (WM_SHOWWINDOW 0 / SW_PARENTCLOSING each) — and restoring shows them again (SW_PARENTOPENING).
Hiding the owner (SW_HIDE) leaves them visible. So on Windows an owned window is never on screen without its
minimized owner's windows staying behind. Virtual desktops: an owned window is on its owner's desktop (the
premise of this issue, Windows' documented behaviour; not probed, the VM run has no virtual-desktop step).

## The outline
The window that draws it is awesome's frame around the popup: awesome gives every client a 1 px border
(`border_width` in the default rule), also this 5 px one — two lines 6 px apart, the popup's own pixels are 1-2 %
alpha. 062 hides such windows under a compositing manager with `_NET_WM_WINDOW_OPACITY` 0, which the WM copies to
its frame. That copy fails in awesome 4.3 for the value 0 when the property is already there at the time awesome
starts managing the window (plain X client, tests/r175/xopacity.c: value 0 set before map -> frame has no
property; 1 or 0x7fffffff before map, or 0 after -> copied), and picom only looks at the frame
(`detect-client-opacity` is off by default). winex11 keeps the property on its window across hide/show, so after
the first re-map (Inventor's ShowPane, a minimize/restore, a re-managed window) the border shows:
`00c800 000000 03c903 03c603 03c903 03c603 03c903 000000 00c800` across the bar on :100 (awesome + picom), with
tests/layered_splitter.exe and owned.exe from the first map on. On its own tag it sits between the browser and
the viewport and looks like a pane border; alone on the old tag it is "just the split bar outline".
Not from the outline but related: on mouse-over the bar is opaque by design (the Thumb trigger above).

## Options
- (a) fix WM_TRANSIENT_FOR: nothing to fix (table above); awesome ignores it for tags after the map.
- (b) follow the owner's `_NET_WM_DESKTOP`: chosen. It states the Win32 rule (an owned window is on its
  owner's desktop) in EWMH terms, is a no-op on WMs that already move transients, and covers every managed
  owned window (dialogs, floating panes and their border popups), not only this class.
- (c) make "owned, WS_EX_TOOLWINDOW|WS_EX_LAYERED, no caption, not activated" unmanaged: rejected. What it would
  buy: no WM policy at all on such helpers (no border, no placement — awesome's default rule moves every new
  floating client to free screen space, the splitter included —, no focus, no tag). What it costs:
  - an override-redirect window is on every desktop and above every other application's window (probe: `noact`
    and `plain` stay on screen on every tag, on top). For a tooltip that lives a second it is tolerable; this
    strip lives as long as the document. Without a compositor it is a black bar over whatever covers Inventor,
    with one an invisible 5 px strip that takes the clicks of other applications;
  - so winex11 would have to hide it itself whenever the owner is not viewable. A minimized owner is covered
    (win32u hides owned popups), a hidden tag/desktop is not: awesome only unmaps its frame (no event for the
    client), mutter/KWin keep the window mapped. It would need a new "X-unmapped but Win32-visible" state driven
    by `_NET_WM_DESKTOP` vs `_NET_CURRENT_DESKTOP`, and restacking against the owner's frame;
  - the popup is clickable and a click activates it; `is_window_managed()` returns TRUE for the active window,
    so it would turn managed on its next SetWindowPos unless that rule got an exception too;
  - other applications: the bits are what any WPF `Window` with WindowStyle=None + AllowsTransparency has once it
    is a tool window (custom-chrome tool palettes, docking-library floating frames and drop indicators, OSDs).
    Those that are activated stay managed either way; a palette shown without activation would lose its WM
    handling (moving with Mod-drag, stacking, focus by click). Menus, tooltips and combo lists are unmanaged
    already and unaffected. No concrete regression case at hand — the argument against (c) is the cost above,
    not a known victim.
  The 2008 upstream change went the same way (tool windows no longer forced unmanaged).

## Fix (fix/175 on integ b5d75449ffe)
1. 3e2c5738a98 `winex11: Keep owned windows on the desktop of their owner.` PropertyNotify `_NET_WM_DESKTOP` on
   a window (new atom, new handler next to the other property handlers in event.c) ->
   `window_net_wm_desktop_notify()` (window.c): reads the value with the window data locked and the X window
   compared, then sends the EWMH `_NET_WM_DESKTOP` client message (source 1) for every top-level whose owner
   chain reaches the window: same-process windows when managed and not withdrawn, windows of other processes
   by their whole window (the WM ignores non-clients). Owner cycles can't exist (server set_window_owner).
   0xFFFFFFFF (all desktops) is passed on as is.
2. 764362956c3 `winex11: Don't set a window opacity of 0.` `sync_window_opacity()` uses 1 (of 2^32) instead of
   0: awesome then copies it to the frame at manage time too. Covers LWA_ALPHA 0 windows as well.
Meets fix/173 in event.c: 173 changes the first line of each handle_*_notify to `get_property_win_data()`
and adds that helper above handle_wm_state_notify; mine adds handle_net_wm_desktop_notify + one line in
X11DRV_PropertyNotify (context only). After 173 the new function can use the helper instead of its own compare.
No overlap with 174 (mouse.c / size-move code).

## Verification (:100 NVIDIA Xorg, wt/175-build, driver .so swapped for A/B; logs inst/175/)
tests/r175/scen.sh (owned.exe; owner moved between desktops 0 and 1 ten times each way; awesome: `c:move_to_tag`,
openbox: pager message; screen checked by window colours):
| | base | fix |
|---|---|---|
| awesome | all 5 managed owned windows left behind on 2 of 2 moves away (base runs: 2 moves) | 20 moves, 0 left behind; old tag shows only the override-redirect noact/plain |
| awesome + picom | same | same; bar never visible, no border |
| openbox, openbox + picom | sub left on the old desktop on every move away | 1 of 10 (the first) |
Dialog created while the owner is on the other desktop: on the owner's desktop, and now comes back with it
(base: stayed), cross-process one too. Stacking (real X order): owned windows above the owner before and after,
same as base; clicks on owner / dialog / tool window activate them (X and Win32), same as base. Positions are not
touched by a tag move. Close: 0 X windows left.
Border (tests/r175/barpix.sh, awesome + picom): base `000000 03c903 ... 000000` with no frame opacity; fix all
`00c800`, frame and client opacity 1, also after SC_MINIMIZE + awesome restore.
062 check (tests/layered_splitter.sh drag / move / cycle x 4 WM configs x base / fix, inst/175/ls062.txt): same
drags, splits and clicks in all 24 runs; bar on screen without a compositor 030303 (both), openbox + picom 00c800
(both), awesome + picom base 03c903 -> fix 00c800. Screenshot in the middle of a Mod4+drag and after the drop
(inst/175/middrag.sh, awesome + picom): base two black columns 6 px apart (398/404, then 518/524), fix none.
077 table (tests/sizemove_scen.sh, 8 scenarios x awesome / openbox, inst/175/sm077.txt): ENTER/EXITSIZEMOVE
pairs and WM_WINDOWPOSCHANGED counts identical base vs fix in all 16.
tools/regress.sh unit, 2 runs per arch: user32:win 4 failures (baseline b5d75449ffe-h26: 4), user32:msg 1 (1),
user32:input pass (pass), i386 and x86_64: 0 worse.

## Not fixed here / limits
- Override-redirect owned popups (tooltips, menus, WS_EX_NOACTIVATE helpers) stay on screen on every tag, as
  before.
- awesome clients on several tags: EWMH has one desktop per window, the owned windows get the first tag only.
- A thread that doesn't pump gets the owner's PropertyNotify late; the owned windows follow then.
- Minimize/restore under awesome is broken on integ already, unchanged (tests/r175/minloop.sh, base = fix):
  WM_SYSCOMMAND SC_MINIMIZE then SC_RESTORE from the app leaves the owner iconic and its owned windows hidden
  5 of 5 (restore from the WM, `c.minimized = false`, works); with an owned dialog of another process also after
  ShowWindow(SW_MINIMIZE/SW_RESTORE). Draft 178.
- openbox marks windows on hidden desktops Iconic, so winex11 minimizes every Wine window on a desktop switch
  and restores it afterwards; windows that can't be minimized are re-mapped and land on the current desktop
  (the second-level dialog above), and under openbox + picom the owned windows of the probe were not all shown
  again. integ behaviour, draft 179.
- awesome's default placement rule moves new floating clients (the splitter too) to free screen space; Inventor
  re-places its splitters on layout changes only. Not seen as a problem in 077's Inventor run (maximized);
  check with a restored Inventor window.
- win32u doesn't hide owned windows like Windows when the owner is minimized (draft 180); that is what leaves
  windows on screen in the two items above, not the desktop handling.
- Moving an owned window alone (awesome: Mod4+Shift+N with a dialog or floating pane focused) is not undone;
  it rejoins its owner the next time the owner changes tag.
