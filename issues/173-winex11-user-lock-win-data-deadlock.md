# 173 winex11: deadlock between win32u's user lock and the driver's win_data_mutex (icon drawn while another thread moves a window)
Status: fixed on fix/173, second version after the adversarial review (6 commits on integ b5d75449ffe, tip 17c01c01b3f;
the first version is kept as fix/173-v1 = 1b6964ae62d), verified except on Inventor (licence seat on the laptop, see
Inventor) · Found in: review of 157 (inst/157-review/x11-s8-hang.txt, lockstress on Xvfb) · **upstream bug** (master
4e819f054dd hangs the same way; the code is unchanged in 11.19)

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
  master 4e819f054dd (wt/regress-master-build-h26): 8 of 9 batch runs (3 of 4 without a WM, 5 of 5 with openbox);
  integ build/: 10 of 10 without a WM; stacks `inst/173/out/icon-integ-1-hang.txt`, `icon-master-1-hang.txt`
  (user_mutex owner = drawing thread, win_data_mutex owner = moving thread).
- Our commits (`git log master..integ -- dlls/winex11.drv dlls/win32u`, 45): none adds a win32u call under
  get_win_data..release_win_data (085b's make_owner_managed, 077's wm_size_move_begin, 085's X11DRV_SetCursor fallback all run
  with the window data released) and none adds a blocking window-data access under the user lock. One of ours touches the
  window data on a path win32u runs with the user lock held: 062 (32e116bc7e5) `try_set_window_hidden` in the surface flush,
  a trylock + posted retry, so it cannot deadlock (see rule and "The flush fallback").
- origin/master (11.19, 455e3509b98): no change in cursoricon.c / winex11 window.c, event.c, xinerama.c since our base.

## Lock order rule for winex11 (differs from winewayland, 157)
`win32u client surfaces_lock (window.c)` -> `win_data_mutex` -> `win32u user lock` -> `window surface mutex` -> leaves
(gdi_lock, font locks, display lock, xrender_mutex, ...).
- win_data first, user lock after: upstream winex11 calls NtUserGetWindowLongW / GetWindowRelative / GetAncestor /
  InternalGetWindowText / GetForegroundWindow / IsWindowVisible / FindWindowEx / NtUserGetDCEx under win_data in dozens of
  places (set_wm_hints, sync_window_position, update_net_wm_states, create_whole_window, window_set_wm_state,
  X11DRV_CreateWindowSurface, ...). Reordering the driver is a rewrite; win32u's side is the one that is already almost
  consistent: every driver entry is called with the window pointer released (apply_window_pos, NtUserGetDCEx calls
  update_visible_region after user_unlock, destroy_window), and upstream fixes go that way (a1cf6a68385 "win32u: Release
  internal OpenGL drawables outside of the user lock", 8bbb829c724, 54d82ed4d54).
- So: **win32u must not enter a driver function that locks the window data (or draw on a window DC, which may call
  pGetDC) while it holds the user lock.** NtUserDrawIconEx was the place found here that did.
- What win32u does call with the user lock (window pointer) held: the surface flush / set_shape / set_clip
  (update_surface_region), and pReleaseDC (release_dce). These, and anything run with a window surface locked, must
  never wait for win_data: a blocking lock in the flush would close user -> win_data (update_surface_region flushes
  with the window pointer held, the driver reads styles under win_data) and surface mutex -> win_data (the driver sets
  surface shapes under win_data: sync_empty_window_shape). Only 062's code touches win_data there and it uses a trylock
  with a posted retry (WM_X11DRV_SET_HIDDEN); commit 4 documents that at try_set_window_hidden.
- **Open violations in win32u, not part of this series: draft 176.** `NtUserUpdateLayeredWindow` blends from a
  caller-supplied DC with the surface locked, and `move_window_bits_surface` draws on a window DC with the old surface
  locked: both can update a dirty window DC there, i.e. surface mutex -> user lock and surface mutex -> win_data
  (blocking, through pGetDC). My stress runs did not exercise them (no report in my folds); 171's review did.
- dce.c's `surfaces_lock` (the window surface list) is held around the flushes in flush_window_surfaces. The first
  fix/171 took it under the user lock; that edge deadlocked in its review and 171 is being reworked without it, so it
  is not part of this rule.
- Sends / SetWindowPos / PostMessage in the driver come after release_win_data (event handlers, DestroyNotify,
  SystrayDockInsert, make_owner_managed): unchanged, checked by reading.
- X window lifetime: `set_window_visual()` destroys and recreates the X window in whatever thread calls it, under win_data.
  A window id is only good for an X request while win_data is held (and compared, if it came from an event).

## Call table
"seen" = debug build `tests/r173/lockorder-debug.patch` (every pthread mutex of win32u and winex11: a report with the call
chain whenever one is taken while another is held), folded with `tests/r173/lockorder.py`, cycles with `cycles.py`.
Runs on the second series alone (`inst/173/out/f.folded`): iconlock, visual_race (+ text), flushpost, the reviewer's
iconrace (draw, wdraw, info, cursor) and uistress, lockstress 3000 seeds 1-5 and 600 with GL, without a WM and with
openbox. Runs on the first series + the first fix/171 (`inst/173/out/e.folded`): the same of mine plus ulwrace destroy /
race / exit, flushpost with +msg, lockstress 15000 in synchronous mode; x86_64 units user32:win msg input dce sysparams monitor clipboard menu
cursoricon, win32u:win32u, d3d9:device, opengl32:opengl (fix/173 at commits 1-3, `inst/173/out/ut-all.folded`); Inventor on
:99: start, the 13 scenarios, a part in sketch edit (`inst/173/inv/dbg2.folded`). Unfixed tree: `inst/173/out/dbg1.folded`.
Driver holds a lock and calls into win32u:
| held | where | takes | |
|---|---|---|---|
| win_data | WindowPosChanged (sync_window_position, set_wm_hints, update_net_wm_states, window_set_wm_state), create_whole_window, SetWindowStyle, SetParent, ShowWindow (hide_icon), SysCommand, GetWindowStateUpdates, CreateWindowSurface (NtUserGetDCEx), SetCursor (NtUserGetIconInfo), event handlers | user lock | seen (84 chains in the stress runs, 143 in Inventor): the allowed direction |
| win_data | same paths | gdi_lock, font locks, dc_attr_lock, display lock (virtual_screen_to_root, is_virtual_desktop), session_lock, display_dc_lock | seen |
| win_data | sync_empty_window_shape -> window_surface_set_shape | window surface mutex (-> flush) | seen |
| win_data | create_shm_image, get_host_window | error_mutex + XLockDisplay (X11DRV_expect_error) | seen; no win32u call in between (read) |
| win_data | update_net_wm_fullscreen_monitors, window_update_client_config | xinerama_mutex (-> display lock before commit 3) | seen; was inverted against the next table |
| xrender_mutex | xrenderdrv_ExtTextOut -> UploadGlyph | gdi_lock, font_lock | seen, leaves |
| kbd_mutex, palette_mutex, xrandr_mutex, XLockDisplay(gdi_display) in set_window_cursor / brush.c | | no win32u call inside | read (lexical scan only); no pair with them held was reported |
win32u holds a lock and calls the driver:
| held | path | driver lock | |
|---|---|---|---|
| user (icon object) | NtUserDrawIconEx on a window DC -> blt -> update_dc -> pGetDC | win_data, blocking | seen = the hang; **fixed (commit 1)** |
| surface mutex | NtUserUpdateLayeredWindow (blend from a window DC), move_window_bits_surface -> update_dc | user lock, win_data (blocking) | **open: draft 176** (seen in 171's review, not in my runs) |
| user (window ptr) | apply_window_pos -> update_surface_region -> window_surface_set_shape / set_clip -> flush | win_data trylock (062); xrender_mutex | seen; allowed, must not block |
| user | release_dc / free_dce / invalidate_dce -> release_dce -> pReleaseDC, set_dce_flags | xrender_mutex (font select); no win_data | seen |
| user (icon object) | NtUserGetIconInfo, create_small_icon -> copy_bitmap (memory DCs) | xrender_mutex | seen |
| dce.c surfaces_lock, surface mutex | flush_window_surfaces, expose, UpdateLayeredWindow -> x11drv_surface_flush | win_data trylock; xrender_mutex; the posted retry takes nothing | seen |
| client surfaces_lock | client_surface_present / update / detach, present_offscreen_client_surfaces (061) | win_data, blocking (X11DRV_GetDC, get_whole_window, attach/detach) | seen; consistent: nothing takes it under win_data or the user lock (read + not seen) |
| display lock | update_display_devices -> X11DRV_UpdateDisplayDevices | xrandr_mutex (Xvfb), xinerama_mutex (seen on :99: the NVIDIA headless Xorg uses the Xinerama handler) | seen |
| display_dc_lock | get_text_metr_size -> font select | xrender_mutex | seen |
| none (read: acquire / release balanced before each call) | pWindowPosChanging, pWindowPosChanged, pCreateWindowSurface, pSetWindowStyle, pSetParent, pShowWindow, pSetWindowText, pSetWindowIcons, pSetWindowRgn, pSetLayeredWindowAttributes, pUpdateLayeredWindow, pDestroyWindow, pSetCapture, pSetCursor, pClipCursor, pMoveWindowBits, pGetWindowStyleMasks, pGetWindowStateUpdates, pSysCommand, pFlashWindowEx, pActivateWindow, pGetDC from NtUserGetDCEx | win_data | no `user -> win_data` chain seen after commit 1 |
Result: unfixed tree 1 cycle (user <-> win_data); 0 cycles on the second series (46 blocking edges, `cycles.py`), on the
first series + 171 in the stress runs and in the Inventor session. No `user -> win_data` (blocking) and no
`win_data -> client surfaces_lock` in any of them; `surface mutex -> user` / `-> win_data` were not reached by these
workloads (they exist: 176). Against the earlier graph the second series' runs add `client surfaces_lock ->
font_cache_lock` and `gdi_lock -> session_lock` (win32u-internal, from uistress' display / GL paths) and lose 171's
`user -> dce surfaces_lock`. win32u-internal pairs seen: user -> {gdi, font, display, session, display_dc, surface,
winproc}, client surfaces_lock -> user, dce surfaces_lock -> surface, display -> session, gdi_lock -> display.

## Fix (fix/173 on integ b5d75449ffe, second version)
1. 4be9255a9ba `win32u: Don't hold the user lock while drawing an icon on a window DC.` Reworked after the review, see
   "Commit 1" below.
2. c9c70c83d42 `winex11: Ignore property changes of X windows that another thread has replaced.` (BadWindow,
   X_GetProperty; unchanged)
3. 5a5ab451f97 `winex11: Get the virtual screen rect before locking the Xinerama monitors.` Code unchanged, message
   widened: the function runs on every window position change with any handler; the opposite order (display lock ->
   monitor list) is taken by win32u's device update when the Xinerama handler provides the devices (seen on :99).
   I did not find a path that takes the monitor list with the display lock held under the XRandR handler
   (xinerama_init runs from the device change event handler and process init, without that lock; on Xvfb the debug
   build shows display lock -> xrandr_mutex only), so the message does not claim one.
4. 520b34e0ed0 `winex11: Document why the surface flush only tries to lock the window data.` Comment only, reworded to
   what holds without 171: the flush runs with the surface locked and possibly the user lock held, and the window data
   is locked before both.
5. 9e00c3abd12 `winex11: Set the window text with the window data locked.` (BadWindow, X_ChangeProperty; unchanged)
6. 17c01c01b3f `user32/tests: Test drawing an icon from two threads at the same time.`

### Commit 1: what the review found and what it does now
The first version copied the frame struct and released the icon before drawing. The reviewer showed that the user lock
was also what serialized all users of an icon's bitmaps: a bitmap can be selected into one DC only, so a second
DrawIconEx / GetIconInfo / CopyIcon on the same icon failed its select and drew the DC's default 1x1 bitmap
(`inst/173-review/iconrace.exe draw 6 4`: 60-75 % wrong on the first version, 0 on integ and Windows 11).
Now: `if (get_gdi_object_type( hdc ) != NTGDI_OBJ_MEMDC)` the function takes private copies of the frame's bitmaps
with `copy_bitmap` under the lock (mask always, alpha and colour for DI_IMAGE; the fields not copied are zeroed, the
icon's own bitmaps are never selected or deleted outside the lock), releases the icon and draws from the copies, then
deletes them. For a memory DC it draws straight from the icon with the lock held, as upstream does.
Why the test is right for every DC kind:
- Only `update_dc` leads from a GDI call to pGetDC, and it only acts on DCs with a DCE (`dc->dce`): cache, class and
  private (CS_OWNDC) window DCs, with or without a clip region from GetDCEx. All of them are created by `alloc_dce` with
  NtGdiOpenDCW = NTGDI_OBJ_DC, never NTGDI_OBJ_MEMDC, so every DC that can reach the driver's window data takes the
  copies.
- Memory DCs never get a DCE (set_dc_dce is only called by dce.c), whatever bitmap is selected; a window surface's
  bitmap is only ever selected by win32u's own internal DCs, never into a DC an application can pass in. WM_PRINT /
  PrintWindow draw into the caller's memory DC: lock kept, no driver window data involved.
- Display DCs without a DCE (CreateDC "DISPLAY"), printer DCs (NTGDI_OBJ_DC) and enhanced metafile DCs
  (NTGDI_OBJ_ENHMETADC) also take the copies. They cannot reach pGetDC, so this is not needed for 173; it is the
  conservative side of a test that does not look inside the DC, and it keeps the user lock out of printer / metafile
  drivers. A 16-bit metafile DC is not a win32u DC: NtGdiCreateCompatibleDC fails on it and the function returns
  FALSE, as before (now after making and deleting the copies).
The cheaper alternative (select the icon's bitmaps under the lock, draw unlocked) is not correct without changing
win32u's one-DC-per-bitmap rule: a concurrent memory-DC draw of the same icon would fail its select. A separate icon
lock held while drawing would sit between win_data (the driver builds cursors from icons under win_data) and the
window data taken by the draw: a cycle. So copies.
Cost: a draw on a window DC goes from 14-16 us to 25-60 us on this host (iconrace wdraw 3 1: mask only 25, colour +
mask 40, alpha 60; six short-lived memory DCs per draw); memory-DC draws are unchanged. Side effect: each of those
memory DCs is an X GC on gdi_display, see draft 182.

### Staleness
- Icon bitmaps: none any more; the copies are taken under the lock that DestroyIcon needs. An icon destroyed right
  after is drawn completely (Windows and integ: all or nothing; iconrace destroy: 0 wrong images).
- Property events: the window comparison is made under win_data, and every destroy of a whole window happens under
  win_data on the same connection, so a matching window still exists when the property is read. A stale event is now
  dropped instead of being applied to the new window's state tracker (its serial belongs to the old window).
- Window text: now set on data->display with win_data held (as create_whole_window does), not on the caller's display
  after the lookup. For a caller that isn't the owner thread the request goes out on the owner's connection.
- Xinerama: the virtual screen origin is read before the monitor list is locked instead of inside; the two come from
  different sources (win32u's display cache vs the driver's list), no ordering was guaranteed before.

## The BadWindow deaths (item 4): two sites, same cause, upstream
`set_window_visual()` destroys and recreates the X window in the *calling* thread (UpdateLayeredWindow /
SetLayeredWindowAttributes / WS_EX_LAYERED change from a thread that doesn't own the window). Code that holds an X window
id without win_data then sends a request for the destroyed window; neither request is in `ignore_error`'s list, so Xlib's
default handler exits the process.
- X_GetProperty (both of the reviewer's deaths; the 171 worker's `inst/171/xerr-bt.txt`): the owner thread's PropertyNotify
  handlers (`handle_wm_hints_notify` -> XGetWMHints, mwm_hints, wm_state, net_wm_state, xembed, wm_normal_hints) read the
  property of the event's window. Debug-build backtrace `inst/173/out/dbg0-ls1.out`. Commit 2: the handlers take the window
  data through `get_property_win_data()`, which drops events whose window is no longer the data's whole window.
- X_ChangeProperty (found here: lockstress seed 3 on integ, backtrace from a synchronous debug run,
  `inst/173/out/d171-sync-ls2.out`): `X11DRV_SetWindowText` looked the window up, released win_data, then
  `sync_window_text` on the thread display. Commit 5.
| repro (10 s runs, without a WM / with openbox) | master | integ | fix/173 (both versions) |
|---|---|---|---|
| `visual_race.exe 10` (GetProperty) | 1 of 5 / 1 of 5 die | 9 of 10 / 1 of 10 | v1 0 of 25 / 0 of 25; v2 0 of 15 / 0 of 15 |
| `visual_race.exe 10 text` (ChangeProperty) | 5 of 5 / 5 of 5 | 10 of 10 / 10 of 10 | v1 0 of 10 / 0 of 10; v2 0 of 10 / 0 of 10 |
| `tests/r171/ulwrace.exe destroy 2000 1 - x` | 5 of 5 / 5 of 5 (GetProperty) | 10 of 10 / 10 of 10 (GetProperty) | 0 BadWindow of 30 (v1 20, v2 10) |
ulwrace on the fix build no longer dies of BadWindow and therefore runs into 171: 7 of 30 runs hang (v1 5 of 20, v2 2
of 10), all with dce.c's surfaces_lock held by a thread inside flush_window_surfaces and the user lock and win_data
free (`inst/173/out/ulw-fix-*-hang.txt`) = 171's list race, not this issue. With the first fix/171 on top (debug build
of v1): 17 of 17 ulwrace runs finished (destroy 13, race 2, exit 2).

## The flush fallback (062): takes no win32u lock
`x11drv_surface_flush` posts WM_X11DRV_SET_HIDDEN when the trylock fails, with the surface mutex (and in
flush_window_surfaces dce.c's surfaces_lock) held. It takes no user lock:
- Read: `NtUserPostMessage` (message.c) -> `is_pointer_message` (table) -> `get_window_thread` -> `get_user_object_thread`
  -> `get_user_entry` -> `read_acquire_user_entry` (window.c:118-140: lock-free read of the shared session's handle table)
  -> `is_exiting_thread` (one compare) -> `put_message_in_queue` (MSG_POSTED: a `send_message` server request, nothing else).
- With +msg tracing the TRACE calls `debugstr_msg_name` -> `SPY_GetMsgStuff`: for 0x80001005 the message is not in
  spy's tables, `NtQueryInformationAtom` (16-bit atom 0x1005, an integer atom) succeeds and it returns before
  `SPY_GetClassName` (which would take the user lock). The trace prints `msg 80001005 ("#4101")`.
- Forced: `tests/r173/flushpost.exe 15` (a layered window alternating between a faint-alpha and a visible image while
  another thread keeps win_data busy by recreating its own X window) on the 173 + 171 debug build: the fallback ran
  ~11100 times with tracing off, ~10300 with WINEDEBUG=+msg, ~10700 under openbox (FLUSHPOST counter in the debug patch);
  pairs with the surface mutex or dce surfaces_lock held in those runs: xrender_mutex, win_data (try), gdi_lock, font locks,
  dc_attr_lock. No `-> user_mutex`. (Those runs were on the first series + the first fix/171; on the second series
  alone flushpost ran the fallback again with the same pairs.)
Caveat: a driver message whose low word were >= 0xc000 would take the class lookup path under +msg; the driver range is
0x80001000-0x80001fff, so it cannot happen today.

## Verification of the second version (wt/173-build at 17c01c01b3f; Xvfb, without a WM / with openbox)
- iconlock icon, 8 s: 25 of 25 / 25 of 25 ok (integ 0 of 10 ok, master 1 of 9 ok).
- The reviewer's `iconrace.exe` (one icon used by several threads): draw 6 4: 0 bad of 160166 / 177568 / 184061
  (alpha / colour + mask / mask only); info 5 3: 0 of 412574; info 5 3 color: 0 of 460488; copy 5 3: 0 of 417219;
  cursor 5: 0 of 424126; wdraw 6 4 and 3 1: 0 bad; destroy 5: 0 wrong images (35 of 418000 nothing drawn = icon
  already destroyed). First version: draw 60-75 % bad, info color 27284 of 147741 (reviewer's numbers).
- `iconpix.exe` / `iconpix32.exe` (4774 cases: icon kind x flags x size x brush x step x destination, incl. window,
  EMF, RTL, transformed, 1-bpp and 8-bpp destinations): 0 different from integ, both arches.
- New test `user32:cursoricon` test_DrawIconEx_threads (two threads, 5000 draws each into their own DIB, compared with
  a reference): 0 failures on the fix build (both arches), on integ, and on the Windows 11 VM (x86_64 and i386:
  5212 / 5213 tests, 0 failures); on the first version 3395 of 10000 draws wrong.
- BadWindow repros: table above. `uistress.exe 30` (reviewer's mixed UI stress): 3 of 3 / 3 of 3 done.
- lockstress 15000 nogl (46500 operations), seeds 1-6 without a WM and 7-12 with openbox: 12 of 12 done, no X error.
  (integ, seeds 2-9: 5 done, 2 X_GetProperty, 1 X_ChangeProperty, 0 hangs; the reviewer had 1 hang in 9.)
- Debug build of the second series: every run listed under "Call table" finished, 0 X errors, 0 cycles, no new edge
  with a driver lock.
- `tools/regress.sh unit`, both arches, vs deps/regress/b5d75449...-h26: user32:cursoricon, static, menu, input,
  comctl32:imagelist, toolbar, gdi32:bitmap pass; user32:win fail 4 / 4, user32:msg fail 1 / 1, gdi32:dc fail 3 / 3
  = the baseline's failure, todo and skip counts in all 20 runs: 0 worse (`inst/173/out/ut2-fix.txt`). The first
  version's set (user32:win msg input dce sysparams monitor clipboard, win32u:win32u, d3d9:device, opengl32:opengl)
  was also 0 worse of 20.
- 077 size-move table (`inst/173/sm.sh` = 130's script, Xvfb + openbox / awesome): identical to integ in all 19 cases
  (modmove 1/1, modresize 1/1, caption 1/1, click 0/0, quick 5/5, kbmove 1/1, kbresize 1/1, modfirst 1/1, selfmove
  0/0, dblclick 1/1, stale 0/0).
- Harness traps: one wineserver must not serve two X displays in a row (X_UnmapWindow BadWindow, as in 077):
  `inst/173/run.sh` restarts the server when the display changes. Relinking a .so of a build while a test runs on it
  kills that process silently (lost one GL lockstress run on the debug build that way; rerun: done). The shared
  tests/r157/lockstress.exe disappeared during a batch (five runs "failed to open"); ls.sh now uses its own build,
  inst/173/lockstress.exe, of tests/r157/lockstress.c.

## Review round (2026-10-04, inst/173-review/)
1. Commit 1 reworked (above); test added (commit 6).
2. Docs: 171's edge removed from the rule, 176 listed as an open violation, commit 4's comment reworded.
3. Not done: the "X window replaced" check for ConfigureNotify / ReparentNotify / GravityNotify / MapNotify. It would
   mean changing the helper of commit 2 to take a window and five more call sites for a race I could not turn into a
   symptom: the stale event must be looked up by the owner thread within the few instructions before get_win_data,
   and it is then applied once. The position loss the reviewer saw after a recreation has another cause (184).
4. Commit 3's message widened as far as I could verify (see Fix).
5. Drafts: [182](182-libx11-xallocid-assert-threads.md) (`_XAllocID` assertion: a libX11 1.8.13 race between the
   sequence sync in LockDisplay and XID allocation, reproduced with plain Xlib, tests/r182/xallocid.c),
   [183](183-set-window-text-use-after-free.md) (set_window_text passes the string to the driver after releasing the
   window; by reading, a stress did not show it), [184](184-winex11-position-lost-after-cross-thread-recreate.md)
   (ConfigureNotify mapped through the stale host parent although parent_invalid is set; one-line experiment: 0 of
   360), and 177 gained a section: it reproduces with 173 applied (`WINEDEBUG=+synchronous visual_race.exe 12 text`
   on the debug build: 2 of 2, the flush's XShmPutImage into a replaced X window is the failing request).

## Inventor (inv2, :99)
- Debug build (fix/173 + fix/171 + debug patch): `INV=inv2 tools/invscen/run.sh all` 13 of 13 PASS (125 steps, 0 fail),
  then the `uilat` setup (part in sketch edit). Lock pairs: only allowed ones, 0 cycles; `display lock -> xinerama_mutex`
  seen (the Xinerama handler is in use there), `xinerama_mutex -> display lock` gone.
- All of this was on the first version of the series (+ the first fix/171 + debug patch).
- **Not done on the plain fix build, and not at all on the second version**: after the suite Inventor showed "Device limit reached" (the laptop "mafa" holds the
  seat, this host "Product is paused"). Nothing in the dialog was clicked; Inventor closed with kill-inventor, inv2 back on
  build/, lease released. Screenshot (shows the account, not for the repo): inst/173/uilat/dbg-rubber.png. One uilat
  `rubber` run went blind into that screen before I looked: XTest clicks at 102,85 and 600,820 (both outside the dialog:
  the ribbon behind the modal dialog and the Home list below it), pointer moves along y=820 and two Escape presses; the
  dialog was unchanged afterwards (the laptop's "Pause product" link is at 885,557 and was not touched).
- Open: suite 13/13 on wt/173-build, rubber-band fps A/B vs build/ (`P=inv2 inst/round2/m.sh TAG`), drag / resize of the
  main window and a dialog (`uilat wmdrag --hz 60`, 061's black-pixel check). What can cost time now is commit 1:
  every icon drawn on a window DC (captions and system menu icons of non-managed windows, toolbars and statics that
  paint without a memory DC) takes 10-45 us longer; commits 2 and 5 are one compare per PropertyNotify and the title
  set. Not measured in Inventor.

## Inventor hangs seen before (item 5)
None of the recorded ones shows this pair: 048's four hang dumps are the loader lock against coreclr's binder lock (app
race), 109 is rpcrt4's unregister wait (fixed), 074 and 082 are a crash and an E_FAIL cascade, not hangs. No issue file has
a stack with user_lock / get_win_data except 157 (Wayland) and 171. The condition here (a thread inside DrawIconEx with
a DC made dirty by a window change, while another thread is in WindowPosChanged of a top-level window) needs two threads
with windows; nothing we have captured from Inventor shows it.

## 171
No common lines with the first fix/171 (win32u window.c: apply_window_pos, destroy_window, free_window_handle,
destroy_thread_windows; a comment in dce.c); this series changes win32u cursoricon.c, winex11 event.c, xinerama.c,
window.c and the user32 cursoricon test. 171 is being reworked (registration counter, no user lock -> dce.c
surfaces_lock edge); nothing here depends on either version.

## Weak spots
- Inventor is not verified on the second version at all, and on the first only with the debug build (above).
- Commit 1 makes icon draws on window DCs 2-4 times slower and creates six short-lived memory DCs (X GCs on
  gdi_display) per draw: more requests on the shared display and more exposure to 182 for threads that draw icons on
  window DCs. A copy without DCs (GetDIBits-style) would avoid that; not done.
- Commit 1 does not check that the copies succeeded; out of memory draws a wrong or empty image instead of failing.
- The DC-kind test is by GDI object type, not by "has a DCE": correct by reading (alloc_dce is the only caller of
  set_dc_dce and creates NTGDI_OBJ_DC), conservative for display / printer / EMF DCs.
- 176 is open: win32u itself still breaks the rule under a surface lock.
- X window lifetime: two unlocked uses were found by running (property handlers, SetWindowText); the surface flush
  into a replaced window (177) and the stale host parent (184) remain. By reading, the other X requests on a whole
  window are made under win_data or are covered by ignore_error or go to gdi_display. Not exhaustively proven.
- Review item 3 (structure events of a replaced window) not done.
- "No win32u call under kbd_mutex / palette_mutex / xrandr_mutex" rests on a lexical scan plus the absence of reports.
- The debug build only tracks pthread mutexes of win32u and winex11 that go through win32u_private.h / x11drv.h; Xlib's
  display lock, the GL / Vulkan locks and server-side waits are outside it. Trylocks are tracked but not counted as edges.
- 062's trylock in the flush stays; the rule depends on it.
- Commit 5 sends the title on the owner's connection from other threads: same pattern as set_window_visual.
- notes/wine/window-surfaces.md: while editing my paragraph (21:50) I rewrote the file from its committed state and
  thereby dropped about ten uncommitted lines another worker had added to it (by the diff counts; most likely the
  181 worker's "Offscreen client surfaces" notes). That worker edited the file again at 21:59 and its section is there
  now; whether anything of the earlier text is missing I cannot tell. Only my own hunk is committed from here.

## Tools
`tests/r173/`: iconlock.c, visual_race.c, flushpost.c; lockorder-debug.patch (applies to fix/173; also prints
X errors with a backtrace instead of asserting under +synchronous, and counts the flush fallback), lockorder.py, cycles.py;
run.sh / batch.sh / ls.sh / x.sh / sm.sh (copies of inst/173/: watchdog runner with gdb stacks, batches on Xvfb :1410
(no WM) / :1411 (openbox), lockstress loop, X servers, size-move table). `tests/r182/xallocid.c`, `tests/r183/settext_race.c`,
`tests/r184/` (vstate.c, parent-invalid-try.patch) belong to the drafts. Builds: wt/173-build (fix/173), wt/173-dbg
(detached: fix/173 + debug patch uncommitted) / wt/173-dbg-build.

## Inventor check on integ cffd27540ee (2026-10-05, inv2 :99, build-next = second version)
- uilat on build-next vs build/ (fresh Inventor each): rubber 118.7 / 118.7 / 116.5 fps (build/ 118.5 / 115.2 / 118.7), lag p50 2.0 vs 2.1 ms;
  orbit 56.6 vs 53.3, pan 60.1 vs 60.1; splitter drag 59.6 / 54.7 vs 47.6 / 47.4 fps. No cost from the series visible. Numbers
  in inst/checks/uilat/ (summary.txt, log.txt, *.out). `view` scenario: PASS (open + SaveAsBitmap 1621x884).
- Mixed UI session, 11 min 46 s (07:09:48-07:21:34) on build-next with `WINEDEBUG=-all,err+all` (the default run.sh log is `-all`
  and would show nothing): per round all ribbon tabs, Shift+MMB orbit in the viewport, wheel zoom, Application Options open/close,
  Ctrl+O open dialog/Esc; after 4 min a drawing (Drawing1) opened via COM (dim141's later step failed with E_INVALIDARG from
  Documents.Open, scenario problem, the drawing itself was open), 3 more minutes on the drawing, then the part again: 43 rounds,
  Inventor alive at every one, no hang. New lines in inventor.log: 332, none of x11drv/win32u/X protocol/Bad* (0 matches);
  the err: lines are the known ones (TokenSecurityAttributes, RoGetActivationFactory, ole class not registered, d3d state
  table). **PASS.** The first attempt of the session (with `-all`) is not evidence for the log part. rubber after the session: 118.7 / 114.5 fps.
- Not done: wmdrag with a dialog (scenario can't find a free caption spot, see 181); lock-order debug build not used.
