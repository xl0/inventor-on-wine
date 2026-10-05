# 184 winex11: window jumps (position doubled) after its X window was recreated by another thread
Status: fixed on fix/182 (wine-src, rebased onto integ d8e4d0f72d2: commits 9a4424f1e3d `winex11: Ignore structure events
of X windows that another thread has replaced.` and cffd27540ee `winex11: Don't map the events of a recreated window
through its previous host window.`; unchanged since the review, which takes them as they are; before the rebase
8f1eca7003e and 4c181523f06 = fix/182-v1), verified · Found in: review of 173 (`inst/173-review/vstate.exe loop 60`, kept as
tests/r184/vstate.c) · upstream code · needs a reparenting WM that lets the new window's first ConfigureNotify through
before it reparents (openbox)

## Symptom
A top-level window at 260,190 whose X window is recreated from a thread that doesn't own it (first
UpdateLayeredWindow: ARGB visual; SetLayeredWindowAttributes / WS_EX_LAYERED reset: default visual) sometimes ends up at
520,380 (or 524,410): Win32 position = twice the real one, the X window follows.
`vstate.exe loop 60` (120 recreations per run), 6 runs per build and WM:
| WM | fix/173 (wt/173-build) | fix/182 |
|---|---|---|
| openbox (Xvfb) | 70 of 720 moved (6, 18, 17, 21, 3, 5 per run; several runs in parallel, the draft had 3-4 of 120 on an idle host) | 0 of 720 |
| awesome 4.3 (Xvfb) | 0 of 720 | 0 of 720 |
| no WM (Xvfb) | 0 (the parent is always the root window) | 0 of 240 |
| mutter 50.1 / Xwayland (vmwl, GNOME session) | 0 of 360 | 0 of 360 |
| KWin 6 / Xwayland (vmwl, Plasma session) | 0 of 360 | 0 of 360 |
Only openbox shows it: awesome, mutter and KWin reparent the new window before any ConfigureNotify of it reaches us.

## Cause (trace `inst/173/out/vs-trace.out`, WINEDEBUG=+x11drv,+event on integ; verified by reading)
```
00dc X11DRV_WindowPosChanged win 0x10052/e00005 ...            <- other thread: new X window e00005, config 260,190
0024 host window .../200175 ConfigureNotify (260,190)-(680,500) <- owner: the OLD frame is still data->parent
0024 208 ConfigureNotify for hwnd/window 0x10052/e00005         <- real (not synthetic) event of the new window, root-relative
0024 handle_state_change ... mismatch config (520,380)-(584,444)/208, expected (260,190)-(324,254)/200
0024 208 ReparentNotify ... set_window_parent window e00005, parent 2001d8   <- only now the parent is replaced
0024 window_update_client_config config changed (260,190) -> (520,380)       <- Win32 window moved there
```
`data->parent` is the host window (WM frame or embedder) that winex11 tracks to turn the parent-relative coordinates of
real ConfigureNotify / GravityNotify events into root coordinates (`host_window_map_point`). Host windows belong to
the owner thread (its display's context, its StructureNotify selection, no locking), so `destroy_whole_window` called
from another thread cannot release the parent; since b862f2b9444 it sets `data->parent_invalid` and leaves the pointer
for the owner to replace "later", which only `set_window_parent` (ReparentNotify, XEmbed) did. `create_whole_window`
then creates the new X window as a child of the root window. Until its ReparentNotify is handled, every event of the
new window is root-relative, but `X11DRV_ConfigureNotify` (and `X11DRV_GravityNotify`) honoured the flag only for
`host_window_configure_child` and still mapped the position through the stale parent = the old frame: frame origin
added twice. `window_update_client_config` takes that as a move by the window manager and moves the Win32 window.

## Fix
- cffd27540ee: the first ConfigureNotify / GravityNotify the owner thread handles while `parent_invalid` is set
  drops the stale parent with `set_window_parent( data, root_window )` (releases the old host window in the owner
  thread, clears the flag): from there on `data->parent` is what it claims to be, NULL = root, until the
  ReparentNotify sets the frame. The `!data->parent_invalid` tests in the two handlers go away. This is what the comment
  in destroy_whole_window ("let the thread eventually replace it with the proper one") asks for; the experiment
  (`tests/r184/parent-invalid-try.patch`: NULL instead of the parent in one mapping call) did the same for
  ConfigureNotify's mapping only and kept the stale host window referenced when no ReparentNotify ever comes (no WM,
  unmanaged windows).
  Why root is right: the new window is always created under root_window (the virtual desktop window in desktop mode),
  and X delivers the window's ReparentNotify before any event generated after the reparenting, so "root until
  ReparentNotify" holds for every event in the stream, whatever the WM does and however late we read it. Synthetic
  ConfigureNotify events are absolute and were never mapped.
- 9a4424f1e3d, the "X window replaced" check the review of 173 suggested, for ConfigureNotify, GravityNotify and
  ReparentNotify: 173's `get_property_win_data` becomes `get_event_win_data( hwnd, window )` and these three handlers
  use it, so an event of the OLD X window that the owner had already looked up when the other thread replaced the
  window is dropped. It belongs to this fix: before, such a ConfigureNotify (frame-relative) was mapped through
  the stale parent, which is the right frame for it, and did little harm; with the stale parent gone it would be read
  as a root position and move the window to the frame's inner offset, and a stale ReparentNotify would set the old
  frame as the new window's parent again. MapNotify / UnmapNotify are not changed (no position or parent in them).
  The race is a few instructions wide (between XFindContext in call_event_handler and get_win_data); I have no probe
  that hits it, the check is by reading.
- Embedded windows (XEmbed, systray): their parent comes from XEMBED_EMBEDDED_NOTIFY / ReparentNotify through
  `set_window_parent` as before; `parent_invalid` is only ever set by a cross-thread destroy, after which the new
  window is not embedded any more (child of root until docked again). GravityNotify's embedded branch uses the same
  parent. Checked: a notification icon docks into awesome's tray with both builds (`tests/r184/tray.exe`, X window
  24x24 below "Awesome systray window", next to the base build's).
- 077's size-move detection reads `window_update_client_config` in ConfigureNotify and the frame position in
  GravityNotify's managed branch; neither changes unless `parent_invalid` was set, and then the managed branch sees
  no parent (no frame known yet) instead of the old frame.
Where it meets other work: event.c only; 175's `_NET_WM_DESKTOP` handler is added after
`handle_wm_normal_hints_notify` and calls nothing of this (the six property handlers' `get_property_win_data( hwnd,
event )` lines become `get_event_win_data( hwnd, event->window )`: a textual neighbour of 175's hunk, no overlap).

## Verification (wt/182-build at 4c181523f06, before the rebase)
see the table above, plus:
- `tests/sizemove_scen.sh` table (inst/173/sm.sh, own Xvfb + openbox / awesome, fix/182 vs fix/173): identical in all
  19 cases (openbox: modmove 1/1, modresize 1/1, caption 1/1, click 0/0, quick 5/5, kbmove 1/1, kbresize 1/1,
  selfmove 0/0, dblclick 1/1, stale 0/0; awesome: modmove 1/1, modresize 1/1, caption 1/1, click 0/0, quick 5/5,
  modfirst 1/1, selfmove 0/0, dblclick 1/1, stale 0/0).
- `tools/regress.sh unit user32:win` (window placement / restore tests), both arches: fail 4, todo 160 / 158, skip 1 =
  the b5d75449ffe-h26 baseline; user32:msg fail 1 = baseline; the other units of 182's list unchanged too.
- 173's probes (visual_race, visual_race text, uistress, lockstress with openbox) finish, see 182.
- After the rebase onto integ d8e4d0f72d2 (175 and 181 are in it; event.c merged by itself): `vstate.exe loop 60` x 6
  under openbox 0 of 720 moved, user32:win / user32:msg equal that tip's baseline.

## Repro
```
inst/182/x.sh start                                          # :1482 :1483 openbox, :1484 awesome
inst/182/vs.sh b173 1482 6                                   # vstate.exe loop 60, 6 runs: "moved N of 720"
inst/182/vs.sh fix 1483 6
vmwl/ssh.sh 'bash -s -- wt/182-build fix 3' < inst/182/vm-vs.sh    # in the VM session's Xwayland
```
