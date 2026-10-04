# 173 winex11: deadlock between win32u's user lock and the driver's win_data_mutex (icon drawn while another thread moves a window)
Status: fixed on fix/173 (3 commits), verification PAUSED half-way (sandbox restart, see "State at pause") · Found in: review of
157 (inst/157-review/x11-s8-hang.txt, lockstress on Xvfb) · **upstream bug** (master 4e819f054dd hangs the same way; code unchanged in 11.19)

## Symptom
A process with two threads that touch windows hangs for good: one thread is in win32u `NtUserDrawIconEx` (icon object =
user lock held) -> `NtGdiAlphaBlend` -> `update_dc` -> `update_visible_region` -> `X11DRV_GetDC` -> `get_win_data` (waits for
win_data_mutex); the other is in `X11DRV_WindowPosChanged` (win_data_mutex held) -> `sync_window_position` ->
`NtUserGetWindowLongW` -> `get_win_ptr` (waits for the user lock). Every other thread then blocks on the user lock.
Reviewer: lockstress 15000 nogl on integ, 9 runs = 6 done, 1 hang (this), 2 dead of X BadWindow (see below).

## Upstream or ours: upstream
- Both halves are upstream code: `NtUserDrawIconEx` keeps the icon object (`get_icon_frame_ptr`, user lock) until it has drawn;
  winex11 reads window styles / owner / text under win_data_mutex everywhere (Julliard 2012, 4289c74f508 ff.).
- Repro `tests/r173/iconlock.exe SECS icon` (main thread: DrawIconEx on GetDC(B) of another thread's window B; thread 2 moves
  B; thread 3 moves its child inside B, which makes B's DCs dirty): hangs within ~1 s.
  master 4e819f054dd (wt/regress-master-build-h26): 3 of 4 clean runs without a WM, 5 of 5 with openbox (+ 8 of 8 by hand);
  integ build/: 10 of 10 without a WM (+ 4 of 5 in a second batch), stacks `inst/173/out/icon-integ-1-hang.txt`,
  `icon-master-1-hang.txt` (user_mutex owner = drawing thread, win_data_mutex owner = moving thread).
- Our commits (`git log master..integ -- dlls/winex11.drv dlls/win32u`, 45): none adds a win32u call under
  get_win_data..release_win_data (085b's make_owner_managed, 077's wm_size_move_begin, 085's X11DRV_SetCursor fallback all run
  with the window data released) and none adds a blocking window-data access under the user lock. One of ours touches the
  window data on a path win32u runs with the user lock held: 062 (32e116bc7e5) `try_set_window_hidden` in the surface flush,
  a trylock + posted retry, so it cannot deadlock (see rule).
- origin/master (11.19, 455e3509b98): no change in cursoricon.c / winex11 window.c, event.c, xinerama.c since our base.

## Lock order rule for winex11 (differs from winewayland, 157)
`win32u client surfaces_lock (window.c)` -> `win_data_mutex` -> `win32u user lock` -> `dce.c surfaces_lock` (edge added by
fix/171) -> `window surface lock` -> leaves (gdi_lock, font locks, display lock, xrender_mutex, ...).
- win_data first, user lock after: upstream winex11 calls NtUserGetWindowLongW / GetWindowRelative / GetAncestor /
  InternalGetWindowText / GetForegroundWindow / IsWindowVisible / FindWindowEx / NtUserGetDCEx under win_data in dozens of
  places (set_wm_hints, sync_window_position, update_net_wm_states, create_whole_window, window_set_wm_state,
  X11DRV_CreateWindowSurface, ...). Reordering the driver is a rewrite; win32u's side is the one that is already almost
  consistent: every driver entry is called with the window pointer released (apply_window_pos, NtUserGetDCEx calls
  update_visible_region after user_unlock, destroy_window), and upstream fixes go that way (a1cf6a68385 "win32u: Release
  internal OpenGL drawables outside of the user lock", 8bbb829c724, 54d82ed4d54).
- So: **win32u must not enter a driver function that locks the window data (or draw on a window DC, which may call
  pGetDC) while it holds the user lock.** NtUserDrawIconEx was the one place found that did.
- What win32u does call with the user lock (window pointer) held: the surface flush / set_shape / set_clip
  (update_surface_region), and pReleaseDC (release_dce). These, and anything run under dce.c's surfaces_lock or a window
  surface lock (flush_window_surfaces, GDI on a window surface), must never block on win_data: with fix/171's edge the cycle
  would be win_data -> user -> dce surfaces_lock -> surface lock -> win_data. Today only 062's code touches win_data there and
  it uses a trylock with a posted retry (WM_X11DRV_SET_HIDDEN); that trylock is load-bearing, keep it (or replace it by
  "always post").
- Sends / SetWindowPos / PostMessage in the driver come after release_win_data (event handlers, DestroyNotify,
  SystrayDockInsert, make_owner_managed): unchanged, checked by reading.

## Call table ("seen" = debug build `tests/r173/lockorder-debug.patch`, folded with `tests/r173/lockorder.py`;
runs: iconlock, lockstress 3000 (seeds 1-3, with and without openbox), user32:win msg input dce sysparams monitor clipboard
menu cursoricon, win32u:win32u, d3d9:device, opengl32:opengl on the fixed tree, Inventor start + invscen hello/tlb on :99)
Driver holds a lock and calls into win32u:
| held | where | takes | |
|---|---|---|---|
| win_data | WindowPosChanged (sync_window_position, set_wm_hints, update_net_wm_states, window_set_wm_state), create_whole_window, SetWindowStyle, SetParent, ShowWindow (hide_icon), SysCommand, GetWindowStateUpdates, CreateWindowSurface (NtUserGetDCEx), SetCursor (NtUserGetIconInfo), event handlers | user lock | seen, 129 chains: the allowed direction |
| win_data | same paths | gdi_lock, font locks, dc_attr_lock, display lock (virtual_screen_to_root, is_virtual_desktop), session_lock, display_dc_lock | seen |
| win_data | sync_empty_window_shape -> window_surface_set_shape | window surface lock (-> flush) | seen |
| win_data | create_shm_image, get_host_window | error_mutex + XLockDisplay (X11DRV_expect_error) | seen; nothing of win32u in between (read) |
| win_data | update_net_wm_fullscreen_monitors, window_update_client_config | xinerama_mutex -> display lock | seen; inverted against the next table, fixed (commit 3) |
| xrender_mutex | xrenderdrv_ExtTextOut -> UploadGlyph | gdi_lock, font_lock | seen, leaves |
| kbd_mutex, palette_mutex, xrandr_mutex, XLockDisplay(gdi_display) in set_window_cursor / brush.c | | no win32u call inside | read (lexical scan only) |
win32u holds a lock and calls the driver:
| held | path | driver lock | |
|---|---|---|---|
| user (icon object) | NtUserDrawIconEx -> blt -> update_dc -> pGetDC | win_data, blocking | seen = the hang; **fixed (commit 1)** |
| user (window ptr) | apply_window_pos -> update_surface_region -> window_surface_set_shape / set_clip -> flush | win_data trylock (062); xrender_mutex | seen; allowed, must not block |
| user | release_dc / free_dce / invalidate_dce -> release_dce -> pReleaseDC, set_dce_flags | xrender_mutex (font select); no win_data | seen |
| user (icon object) | NtUserGetIconInfo, create_small_icon -> copy_bitmap (memory DCs) | xrender_mutex | seen |
| dce.c surfaces_lock, surface lock | flush_window_surfaces, expose, UpdateLayeredWindow -> x11drv_surface_flush | win_data trylock; xrender_mutex | seen |
| client surfaces_lock | client_surface_present / update / detach, present_offscreen_client_surfaces (061) | win_data, blocking (X11DRV_GetDC, get_whole_window, attach/detach) | seen; consistent: nothing takes it under win_data or the user lock (read + not seen) |
| display lock | update_display_devices -> X11DRV_UpdateDisplayDevices | xrandr_mutex (Xvfb), xinerama_mutex (seen on :99, the NVIDIA headless Xorg uses the Xinerama handler) | seen |
| display_dc_lock | get_text_metr_size -> font select | xrender_mutex | seen |
| none (read: acquire/release balanced before each call) | pWindowPosChanging, pWindowPosChanged, pCreateWindowSurface, pSetWindowStyle, pSetParent, pShowWindow, pSetWindowText, pSetWindowIcons, pSetWindowRgn, pSetLayeredWindowAttributes, pUpdateLayeredWindow, pDestroyWindow, pSetCapture, pSetCursor, pClipCursor, pMoveWindowBits, pGetWindowStyleMasks, pGetWindowStateUpdates, pSysCommand, pFlashWindowEx, pActivateWindow, pGetDC from NtUserGetDCEx | win_data | no `user -> win_data` chain seen after commit 1 |
Fixed tree, all runs above: no `user -> win_data` (blocking), no `win_data -> client surfaces_lock`, no
`surface lock -> user`. win32u-internal pairs seen: user -> {gdi, font, display, session, display_dc, surface, winproc},
client surfaces_lock -> user, dce surfaces_lock -> surface, display -> session, gdi_lock -> display: no cycle.

## Fix (fix/173 on integ b5d75449ffe)
1. f86a6357aed `win32u: Don't hold the user lock while drawing in NtUserDrawIconEx().` The frame (bitmap handles, size) is
   copied and the icon object released before the first GDI call.
2. 179ab6c9f83 `winex11: Ignore property changes of X windows that another thread has replaced.` (the BadWindow deaths)
3. 6635a80202b `winex11: Get the virtual screen rect before locking the Xinerama monitors.` xinerama_mutex -> display lock
   (xinerama_get_fullscreen_monitors -> virtual_screen_to_root) against display lock -> xinerama_mutex
   (UpdateDisplayDevices with the Xinerama handler). Not seen hanging; found by the debug build, 4 lines.

### Staleness
- Icon frame read before drawing: the bitmaps can be deleted by a DestroyIcon in another thread between the release and
  NtGdiSelectBitmap (before: DestroyIcon waited for the draw). The select then fails and the blt draws the DC's default
  1x1 bitmap, or, if the handle value was reused, another bitmap; no crash (GDI handles are validated, a selected bitmap's
  deletion is deferred). Destroying an icon while another thread draws it is the app's race on Windows too. Not re-validated.
- Property events: the window comparison is made under win_data, and every destroy of a whole window happens under
  win_data on the same connection, so a matching window still exists when the property is read.
- Xinerama: the virtual screen origin is read before the monitor list is locked instead of inside; both come from
  different sources anyway (win32u's display cache vs the driver's list), no ordering was guaranteed before.

## The BadWindow deaths (item 4)
`X_GetProperty` BadWindow in the owner thread's PropertyNotify handlers (`handle_wm_hints_notify` -> XGetWMHints,
`handle_mwm_hints_notify`, wm_state, net_wm_state, xembed, wm_normal_hints): `set_window_visual()` destroys and recreates
the X window in the *calling* thread (UpdateLayeredWindow / SetLayeredWindowAttributes / WS_EX_LAYERED change from a thread
that doesn't own the window), the owner thread already looked the event's window up and then reads the property of the
destroyed window. Not covered by `ignore_error` (GetProperty on a thread display), so Xlib's default handler exits.
Upstream too. Debug-build backtrace: `inst/173/out/dbg0-ls1.out` (XERROR block); the 171 worker's `inst/171/xerr-bt.txt`
shows the same site. Repro `tests/r173/visual_race.exe SECS`: integ 5 of 8 without a WM (10-15 s runs), master 2 of 10,
fix (lock fix only) 3 of 3, fix with commit 2: 0 of 19 without a WM, 0 of 2 clean openbox runs.
Fix = commit 2: the handlers take the window data through `get_property_win_data()`, which drops events whose window is
no longer the window data's whole window.

## Verification so far
- iconlock icon, 8 s runs: fix 25 of 25 ok without a WM, 25 of 25 with openbox; integ 0 of 10 (10 hangs); master above.
- 077 size-move table (`inst/173/sm.sh` = 130's script, Xvfb + openbox / awesome, `inst/173/sm/`): fix and integ identical
  in all 19 cases (modmove 1/1, modresize 1/1, caption 1/1, click 0/0, quick 5/5, kbmove 1/1, kbresize 1/1, modfirst 1/1,
  selfmove 0/0, dblclick 1/1, stale 0/0).
- Debug build of the fixed tree, x86_64 units: user32:win fail 4, msg fail 1, sysparams fail 6, the others pass: the
  baseline's numbers (deps/regress/b5d75449...-h26).
- Harness trap: one wineserver must not serve two X displays in a row (X_UnmapWindow BadWindow on the other display's
  window, as in 077): `inst/173/run.sh` now restarts the server when the display changes. The first "openbox" halves of
  the vr / integ batches in `inst/173/out/batch1.txt` are that artifact, not results.

## Inventor hangs seen before (item 5)
None of the recorded ones shows this pair: 048's four hang dumps are the loader lock against coreclr's binder lock (app
race), 109 is rpcrt4's unregister wait (fixed), 074 and 082 are a crash and an E_FAIL cascade, not hangs. No issue file has
a stack with user_lock / get_win_data except 157 (Wayland) and 171. The condition here (a thread inside DrawIconEx with
a DC made dirty by a window change, while another thread is in WindowPosChanged of a top-level window) needs two threads
with windows; nothing we have captured from Inventor shows it.

## State at pause (2026-10-04 17:15, sandbox restart)
Done: upstream-or-ours, repro, table, rule, three commits on fix/173 (worktree clean), size-move table, part of the stress.
inv2 is back on build/ (x/prefixes.tsv unchanged), Inventor closed, lease released; my Xvfb :1410-:1416 and scratch
wineservers are stopped. Builds: wt/173-build = fix/173 tip (win32u + winex11 rebuilt); wt/173-dbg (detached: integ + debug
patch uncommitted + the three commits cherry-picked) / wt/173-dbg-build = the debug build.
Remaining, in order:
1. Stress: `inst/173/batch.sh fix icon 25 30 tests/r173/iconlock.exe 8 icon`, `... fix vr 15 30 tests/r173/visual_race.exe 10`
   (50 clean runs each way; x.sh starts Xvfb :1410 / :1411+openbox; the scripts are in inst/173/, copies in tests/r173/), `inst/173/ls.sh fix 1410 ls 1 2 3 4 5 6`,
   `ls.sh fix 1411 ls 7..12` (lockstress 15000 nogl), contrast `ls.sh integ ...` seeds 2-9; the 171 worker's
   `tests/r171/ulwrace.exe destroy 2000 1 - x` on the fix build (8 of 10 BadWindow on build/).
2. Debug build + fix/171 cherry-picked (edge user -> dce surfaces_lock): lockstress, ulwrace; expect no new pair into win_data.
3. `tools/regress.sh unit` for user32:win msg input dce sysparams monitor clipboard, win32u:win32u, d3d9:device,
   opengl32:opengl, both arches, on wt/173-build vs the h26 baseline (only the x86_64 debug-build runs exist).
4. Inventor on inv2: debug build session (only start + hello/tlb were logged: `inst/173/inv/dbg.folded`, no forbidden
   pair), then wt/173-build: `INV=inv2 tools/invscen/run.sh all` 13/13, rubber-band fps A/B vs build/ (inst/round2/m.sh,
   P=inv2), xdotool drag / resize of the main window and a dialog (061 check).
5. notes/wine/window-surfaces.md: the winex11 rule (not written yet); CODE.md line for tests/r173.

## Weak spots
- The icon frame race above is argued, not closed.
- "No win32u call under kbd_mutex / palette_mutex / xrandr_mutex" rests on a lexical scan, nested calls were not followed.
- The debug build only tracks pthread mutexes of win32u and winex11 that go through win32u_private.h / x11drv.h; Xlib's
  display lock, the GL/Vulkan locks and server-side waits are outside it.
- Other win32u functions that draw with a handle locked: found none by scanning (menus use unlocked pointers), but only
  DrawIconEx was exercised on purpose.
- 062's trylock in the flush stays; the rule depends on it.
- winemac has the same DrawIconEx exposure if its GetDC equivalent locks window data (not looked at).
