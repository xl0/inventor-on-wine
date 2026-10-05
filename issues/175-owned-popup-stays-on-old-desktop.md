# 175 Moving Inventor to another virtual desktop leaves the splitter bar behind
Status: fixed on fix/175 (2 commits on integ b5d75449ffe, commit 1 reworked after the adversarial review; first version kept as fix/175-v1), probes only — Inventor not verified (licence seat in use) · Owner: worker-175 · Branch: fix/175 (wt/175) · Found in: user's laptop (awesome + picom)

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
| mutter (GNOME guest, Xwayland; reviewer's run, inst/175-review/out/gnome-*.txt) | owned windows end up on the owner's workspace, with and without the fix | like openbox for Wine: while the owner is on another workspace its managed owned windows are withdrawn (no `_NET_WM_DESKTOP`), i.e. winex11 minimizes the owner there too (179); they come back when that workspace is viewed | |
| KWin | not established | | |
Baseline (integ) with the probe: awesome leaves every managed owned window on the old tag (each move of the
baseline runs; a dialog created while the owner is on the other tag appears with the owner, then stays there when
the owner comes back). openbox moves them, but see "Not fixed here".

## Windows ground truth (Win11 VM, `owned.exe auto`: 0 failures; Wine: 8, see draft 180)
Minimizing the owner (ShowWindow(SW_MINIMIZE) and WM_SYSCOMMAND SC_MINIMIZE alike) hides every window in its
owner chain — the layered popup, dialogs, tool window, the WS_EX_NOACTIVATE / plain popups and the dialog owned
by a dialog (WM_SHOWWINDOW 0 / SW_PARENTCLOSING each) — and restoring shows them again (SW_PARENTOPENING).
Hiding the owner (SW_HIDE) leaves them visible. So on Windows the owned windows of a minimized owner are never
left on screen. Virtual desktops: an owned window is on its owner's desktop (the premise of this issue, Windows'
documented behaviour; not probed, the VM run has no virtual-desktop step).

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
    (win32u hides owned popups), and so is a hidden desktop under openbox and mutter, which report the owner as
    Iconic there (179); a hidden tag under awesome is not: it only unmaps its frame (no event for the client).
    It would need a new "X-unmapped but Win32-visible" state driven by `_NET_WM_DESKTOP` vs
    `_NET_CURRENT_DESKTOP`, and restacking against the owner's frame;
  - the popup is clickable and a click activates it; `is_window_managed()` returns TRUE for the active window,
    so it would turn managed on its next SetWindowPos unless that rule got an exception too;
  - other applications: the bits are what any WPF `Window` with WindowStyle=None + AllowsTransparency has once it
    is a tool window (custom-chrome tool palettes, docking-library floating frames and drop indicators, OSDs).
    Those that are activated stay managed either way; a palette shown without activation would lose its WM
    handling (moving with Mod-drag, stacking, focus by click). Menus, tooltips and combo lists are unmanaged
    already and unaffected. No concrete regression case at hand — the argument against (c) is the cost above,
    not a known victim.
  The 2008 upstream change went the same way (tool windows no longer forced unmanaged).

## Fix (fix/175 on integ b5d75449ffe; applies cleanly to integ b8f013d4fbb with 171 + 173)
1. 0ce9a9fdd15 `winex11: Keep owned windows on the desktop of their owner.`
   - Every window remembers the desktop it is on (`has_net_wm_desktop`, `net_wm_desktop` in the window data):
     the `_NET_WM_DESKTOP` the window manager gave it, read on PropertyNotify (new atom, new handler next to
     the other property handlers in event.c) with the window data locked and the X window compared, or the
     one winex11 last requested for it.
   - Only when the value differs from the remembered one, `window_net_wm_desktop_notify()` (window.c) sends the
     EWMH `_NET_WM_DESKTOP` client message (source 1) for the top-levels owned by the window — through other
     owned windows, and through owners that are child windows (GA_ROOT at each step, like WM_TRANSIENT_FOR; at
     most 32 steps) — that are managed, not withdrawn and were on the desktop the window comes from, and
     records the new desktop for them at once.
   - Windows of other processes in the owner chain are requested whatever desktop they are on (see the cases).
2. 31c518e6b7d `winex11: Don't set a window opacity of 0.` `sync_window_opacity()` uses 1 (of 2^32) instead of
   0: awesome then copies it to the frame at manage time too. On base an alpha-0 window (LWA_ALPHA 0, or 062's
   "hidden" surface) under awesome + picom is drawn as if it had no opacity at all, border included (reviewer:
   22860 changed pixels vs 0), and setting 0 again once it is managed doesn't help: no change for awesome.
fix/173 (now on integ) changed the first line of the other handle_*_notify functions to
`get_property_win_data()`; the new function does the same compare itself and could use the helper. No overlap
with 174.

### Review round: what changed in commit 1 and why (reviewer's findings, inst/175-review/)
The first version (fix/175-v1, 3e2c5738a98) sent the request to every managed owned window on every
`_NET_WM_DESKTOP` PropertyNotify of the owner. Reproduced by the reviewer:
- a tool window the user moved to another screen jumped back to the owner's screen on each owner tag change
  (awesome's handler also sets the client's screen; i3 has workspaces per output too);
- awesome rewrites the property with the same value whenever a tag is toggled on the client: a dialog moved
  alone to another tag was pulled back, a dialog on tags [1,4] collapsed to [1], and every managed-window map
  cost a top-level enumeration;
- a popup whose owner is a child of the main window (SetWindowLongPtr(GWLP_HWNDPARENT, child)) stayed behind.
The cases, as decided:
- First PropertyNotify of a window: there is no previous desktop, so nothing was moved: the value is only
  remembered. A new transient is placed with its parent by the window manager itself (awesome, openbox).
- Withdrawn and re-mapped window: the remembered value is deliberately kept (awesome and openbox delete the
  property on withdraw; the delete is ignored). It means "the desktop this Win32 window was last on": if the
  window comes back on another desktop (shown again while another tag is viewed, restored from minimized —
  winex11 re-maps through Withdrawn —, or, by the code, its X window recreated), that is a move, and the owned
  windows still on the old desktop join it (probe inst/175/rv/cases.sh: owner hidden on tag 1, shown viewing
  tag 3 -> dialogs on 3, base: left on 1; minimized on tag 1 and restored viewing tag 4 -> all on 4, base: the
  second-level dialog left on 1). While withdrawn, a window is never requested itself; a dialog hidden while
  the owner moves away appears on the owner's tag when shown (the window manager places it) and follows again.
- Sticky: 0xFFFFFFFF is a value like any other (owned windows on the old desktop become sticky with the owner
  and return with it). openbox handles the whole transient tree itself (same result base and fix); awesome
  ignores 0xFFFFFFFF requests and keeps the number of sticky clients, so nothing happens there.
- Owner chains: one enumeration at the moved window's notify covers all levels (a second-level dialog follows a
  real move, also through a hidden first-level one); it doesn't rely on the window manager's answers
  cascading. Each window is judged by the desktop it was on, so a dialog the user moved alone stays, and its
  own dialogs stay with it.
- Windows of other threads: their remembered desktop is kept by their own thread's PropertyNotify, read here
  under the window data lock; a dialog of another thread follows (probe `thr`). One whose thread has not
  handled its first PropertyNotify yet (mapped, then not pumping) has no desktop and is left where it is.
- Windows of other processes: requested unconditionally on a real move. Their previous desktop can't be known
  cheaply: reading the property needs a round trip on an X window that the other process can destroy at any
  time (BadWindow ends this process; trapping the error means X11DRV_expect_error, which holds the display lock
  over the round trip — the hang of 177), and their own driver doesn't see the owner's PropertyNotify. Not
  following at all would bring the reported bug back for helper-process dialogs (a modal dialog left on the old
  tag, its owner dead on the new one). So: they follow, and one that the user parked elsewhere is pulled along
  on a real owner move (probe: `xproc` on tag 3 -> owner to tag 2 -> xproc on 2; the same-process `late` stays
  on 3). The precise version is a driver message to the owned window's thread ("your owner went from A to B"),
  to do if this case ever matters.
- Two owner moves within the Wine-to-WM latency (the reviewer's known weaker spot of v3: a child judged by
  the desktop the window manager last *reported* is still on the old one when the second move arrives, and
  stays one desktop behind): measured with inst/175/rv/rapid.sh, two pager messages back to back, 25 times:
  15 of 25 left the dialog and its own dialog behind. Hence the one difference to v3: a window that is
  asked to follow is recorded on the new desktop at once. 0 of 25 then (see Verification). The same line
  keeps the followed window's own PropertyNotify from counting as a move of its own, which made every owned
  window enumerate all top-levels again and re-request its owned windows (cost table below).
  What it costs: a window the window manager refuses to move is believed moved until its next PropertyNotify;
  it is then asked again at the owner's next move, which is the intent anyway. With overlapping moves a
  second-level window can be sent to the intermediate desktop and back before it settles (every write of the
  window manager produces a PropertyNotify, the last one read is the final state).
- Multi-monitor (awesome numbers desktops over all screens: tag 1 of screen 2 = 9; inst/175/rv/screen2.sh with
  two fake screens): a tool window parked on screen 2 stays there while the owner changes tags on screen 1
  (reviewer's screens.sh: base = final); an owner moved to a tag of the other screen (`c:move_to_tag`,
  `c:move_to_screen`) takes the dialogs that were on its tag along to that screen (base: they stay); when the
  owner lands on the very tag the parked window is on and leaves again, that window is on the owner's desktop
  and goes with it.

## Verification (:100 NVIDIA Xorg, wt/175-build = fix/175 31c518e6b7d, driver .so swapped for A/B; logs inst/175/, inst/175/rv/out/)
"base" = integ's driver, "v1" = the first version (fix/175-v1), "final" = fix/175 as committed (the tested .so is
byte-identical to a rebuild of the branch head).

Reviewer's table (his probes edge.sh / screens.sh with own2.exe, copied to inst/175/rv/; awesome):
| case | base | v1 | final |
|---|---|---|---|
| popup owned by a child of the owner, owner changes tag | left | left | follows |
| dialog moved alone to tag 3, then tags 4 / 5 toggled on the owner | stays on 3 | pulled back to 1 | stays on 3, its own dialog with it |
| dialog on tags [1,4], tag toggled on the owner | [1,4] | [1] | [1,4] |
| tool window parked on screen 2, owner changes tag on screen 1 | stays | jumps to screen 1 | stays |
| dialog and dialog of the dialog, owner changes tag | left | follow | follow |
| after all that a real move of the owner | all left | all but the child-owned popup follow | the parked dialog (+ its own) stays, the rest follows |
Own cases (inst/175/rv/cases.sh, screen2.sh, sticky.sh, rapid.sh):
- dialog of another thread follows; a dialog hidden while the owner moves away is on the owner's tag when shown
  again and follows afterwards; owner hidden and shown on another tag / minimized and restored viewing another
  tag: all owned windows there (base: left behind, or only the direct ones);
- owner to a tag of the other screen and back, `c:move_to_screen`: the dialogs come along (base: left);
- owner to all desktops and back under openbox: same as base (openbox moves the tree itself);
- dialog of another process parked on tag 3, owner tag 1 -> 2: pulled to 2 (the documented limit; the
  same-process dialog next to it stays);
- two owner moves back to back (pager messages, dialog + its own dialog, 25 rounds each): v3-style judging by
  reported desktops 15 of 25 (1 -> 2 -> 3) and 17 of 25 (1 -> 2 -> 1) left behind; final 0 of 50 and 0 of 25.

tests/r175/scen.sh (owned.exe; owner moved between desktops 0 and 1 ten times each way; awesome: `c:move_to_tag`,
openbox: pager message; screen checked by window colours), final:
| | base (earlier runs) | final |
|---|---|---|
| awesome | all 5 managed owned windows left behind on every move away | 20 moves, 0 left behind; old tag shows only the override-redirect noact/plain |
| awesome + picom | same | 20 moves, 0 left behind; bar never visible, no border |
| openbox, openbox + picom | second-level dialog left on the old desktop on every move away | on the first of 10 moves away |
The openbox leftover is not a missed follow: openbox moves the whole tree itself (spy on the dialog:
desktop 0 -> 1, Iconic), then winex11 withdraws and re-maps the Iconic window it can't minimize ("remapping to
workaround Mutter issues"), and openbox manages it anew on the current desktop (draft 179); it joins its owners
when their desktop is viewed.
Dialog created while the owner is on the other desktop: on the owner's desktop, and comes back with it (base:
stayed), cross-process one too. Stacking (real X order): owned windows above the owner before and after, same
as base; clicks on owner / dialog / tool window activate them (X and Win32), same as base. Close: 0 X windows left.

Cost (reviewer's count.sh: server requests of the process over one owner tag change, awesome):
| | `_NET_WM_DESKTOP` requests | get_window_tree | all server requests |
|---|---|---|---|
| 50 owned dialogs: base | 0 | 6 | 558 |
| 50 owned dialogs: v1 (= judging by reported desktops) | 50 | 618 | 1324 |
| 50 owned dialogs: final | 50 | 18 | 606 |
| chain of 10: base | 0 | 60 | 464 |
| chain of 10: v1 | 55 | 192 | 694 |
| chain of 10: final | 10 | 52 | 536 |
i.e. one top-level enumeration per real move; nothing but one GetProperty for any other `_NET_WM_DESKTOP`
PropertyNotify (a window's map: tests/r130 moveloop popup, X requests of the process: GetProperty 316 -> 317
under awesome, 317 -> 318 under openbox; the other request counts within the run-to-run spread, totals
7243 / 7267 and 7558 / 7601).

Minimize / restore of the owner under awesome (reviewer's minl.sh: ShowWindow minimize + restore, owned dialog,
tool window, layered bar, second-level dialog; "incomplete" = owner still iconic and owned windows hidden, the
failure of draft 178): base 0 of 200; final 2 of 160, the variant before it 2 of 80; final with 40 extra
GetProperty round trips per PropertyNotify 0 of 80, with a 5 ms sleep there 0 of 80; minimized / restored by the
window manager 0 of 20 + 0 of 20. Fisher's exact test: my unamplified runs alone (0 of 200 vs 4 of 240)
p = 0.13; with the reviewer's runs (base 1 of 152, patched 2 of 224) and the amplified ones base 1 of 352,
patched 6 of 624, p = 0.43. Same signature on base; the handler does nothing but read the property in this loop
(no desktop changes), and making it much slower didn't produce failures, so I take the difference for the
spread of a ~1 % race — but it is not shown to be equal.
Border (tests/r175/barpix.sh, awesome + picom; commit 2 unchanged since): base `000000 03c903 ... 000000` with no
frame opacity; fix all `00c800`, frame and client opacity 1, also after SC_MINIMIZE + awesome restore.
062 check (tests/layered_splitter.sh drag / move / cycle x 4 WM configs, final vs base, inst/175/ls062*.txt): same
drags, splits and clicks in all 12 runs; bar on screen without a compositor 030303 (both), openbox + picom 00c800
(both), awesome + picom base 03c903 -> 00c800. Screenshot in the middle of a Mod4+drag and after the drop
(inst/175/middrag.sh, awesome + picom, v1's build): base two black columns 6 px apart (398/404, then 518/524),
fix none.
077 table (tests/sizemove_scen.sh, 8 scenarios x awesome / openbox, inst/175/sm077*.txt): ENTER/EXITSIZEMOVE
pairs identical in all 16; WM_WINDOWPOSCHANGED counts identical except awesome Mod+resize 11 instead of 10 in
one run (one extra transient rect 199,199 next to 200,200); six more runs each: 10 on base and final.
tools/regress.sh unit, final and base driver in the same build and session, 4 runs of user32:win per arch:
identical (x86_64 4 failures = baseline b5d75449ffe-h26, i386 0 on both, baseline 4); user32:msg 1 (1) and
user32:input pass, 2 runs per arch. An earlier run of the intermediate variant had one extra user32:win line
(win.c:12747 "parent didn't get WM_NCDESTROY", a cross-thread destroy order check) in 1 of 4 while other tests
loaded the host; 0 of 8 for final and 0 of 8 for base afterwards.

## Still to do with Inventor (seat was in use; nothing below is verified)
On inv3 with wt/175-build (`tests/r175/wm.sh :100 awesome picom`, back to openbox / build afterwards):
- xprop / `tests/r175/xinfo.sh :100 ""` of the real splitters: managed, DIALOG, transient for the main window,
  opacity 1 on client and awesome frame; whether the frame had no opacity on integ (when: first map, after
  ShowPane, after minimize/restore) — the outline explanation above is from probes with the same style bits.
- Main window to tag 2 and back 5 times with a part open (`tests/r175/movetag.sh :100 "<title>" 2`): nothing on
  the old tag, splitters at the pane borders and draggable after each move; undocked browser (mini frame + its
  four MiniFrameBorder popups, a two-level owner chain) and an open dialog come along.
- Hover over a splitter: it turns opaque by design, awesome's border shows with it — acceptable?
- Restored (not maximized) main window: does awesome's placement rule move the splitters / border popups when
  they are mapped?

## Not fixed here / limits
- Override-redirect owned popups (tooltips, menus, WS_EX_NOACTIVATE helpers) stay on screen on every tag, as
  before.
- An owner on several tags: `_NET_WM_DESKTOP` is its first tag; toggling further tags on it changes nothing
  for the owned windows (no change of the property value), they are only on the first one.
- A thread that doesn't pump gets its window's PropertyNotify late: the owned windows follow when it handles
  the owner's; a window whose own first PropertyNotify isn't handled yet has no desktop and doesn't follow.
- Owned windows of other processes follow every real move of the owner, wherever the user put them (see the
  cases).
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
- An owned window the user moved alone (awesome: Mod4+Shift+N with a dialog or floating pane focused) stays
  where it was put, also when the owner changes tag later; it only moves with the owner again once it is on
  the owner's tag. Exception: windows of other processes (above).
- A dialog on several tags that include the owner's first one follows a real move of the owner and is then on
  that one tag (EWMH has one desktop per window).
