# 173 winex11: deadlock between win32u's user lock and the driver's win_data_mutex (icon drawn while another thread moves a window)
Status: fixed on fix/173 (5 commits on integ b5d75449ffe, tip 1b6964ae62d), verified except on Inventor with the plain fix
build (licence seat taken by the laptop, see Inventor) · awaiting review · Found in: review of 157
(inst/157-review/x11-s8-hang.txt, lockstress on Xvfb) · **upstream bug** (master 4e819f054dd hangs the same way; the code is
unchanged in 11.19)

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
`win32u client surfaces_lock (window.c)` -> `win_data_mutex` -> `win32u user lock` -> `dce.c surfaces_lock` (edge added by
fix/171) -> `window surface mutex` -> leaves (gdi_lock, font locks, display lock, xrender_mutex, ...).
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
  surface mutex (flush_window_surfaces, GDI on a window surface), must never wait for win_data: it would close the cycle
  win_data -> user -> dce surfaces_lock -> surface mutex -> win_data (the first three edges exist: styles read under
  win_data; fix/171's register_window_surface under the window pointer; flush_window_surfaces). Only 062's code touches
  win_data there and it uses a trylock with a posted retry (WM_X11DRV_SET_HIDDEN); commit 4 documents that at
  try_set_window_hidden.
- Sends / SetWindowPos / PostMessage in the driver come after release_win_data (event handlers, DestroyNotify,
  SystrayDockInsert, make_owner_managed): unchanged, checked by reading.
- X window lifetime: `set_window_visual()` destroys and recreates the X window in whatever thread calls it, under win_data.
  A window id is only good for an X request while win_data is held (and compared, if it came from an event).

## Call table
"seen" = debug build `tests/r173/lockorder-debug.patch` (every pthread mutex of win32u and winex11: a report with the call
chain whenever one is taken while another is held), folded with `tests/r173/lockorder.py`, cycles with `cycles.py`.
Runs on the final tree (fix/173 + fix/171's three commits): iconlock, visual_race (+ text), flushpost (+msg off / on),
ulwrace destroy / race / exit, lockstress 3000 seeds 1-5 and 600 with GL (without a WM and with openbox), lockstress 15000
in synchronous mode (`inst/173/out/e.folded`); x86_64 units user32:win msg input dce sysparams monitor clipboard menu
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
| user (icon object) | NtUserDrawIconEx -> blt -> update_dc -> pGetDC | win_data, blocking | seen = the hang; **fixed (commit 1)** |
| user (window ptr) | apply_window_pos -> update_surface_region -> window_surface_set_shape / set_clip -> flush | win_data trylock (062); xrender_mutex | seen; allowed, must not block |
| user | release_dc / free_dce / invalidate_dce -> release_dce -> pReleaseDC, set_dce_flags | xrender_mutex (font select); no win_data | seen |
| user (icon object) | NtUserGetIconInfo, create_small_icon -> copy_bitmap (memory DCs) | xrender_mutex | seen |
| user (window ptr), fix/171 | apply_window_pos / destroy_window -> register_window_surface | (dce.c surfaces_lock, no driver call) | seen |
| dce.c surfaces_lock, surface mutex | flush_window_surfaces, expose, UpdateLayeredWindow -> x11drv_surface_flush | win_data trylock; xrender_mutex; the posted retry takes nothing | seen |
| client surfaces_lock | client_surface_present / update / detach, present_offscreen_client_surfaces (061) | win_data, blocking (X11DRV_GetDC, get_whole_window, attach/detach) | seen; consistent: nothing takes it under win_data or the user lock (read + not seen) |
| display lock | update_display_devices -> X11DRV_UpdateDisplayDevices | xrandr_mutex (Xvfb), xinerama_mutex (seen on :99: the NVIDIA headless Xorg uses the Xinerama handler) | seen |
| display_dc_lock | get_text_metr_size -> font select | xrender_mutex | seen |
| none (read: acquire / release balanced before each call) | pWindowPosChanging, pWindowPosChanged, pCreateWindowSurface, pSetWindowStyle, pSetParent, pShowWindow, pSetWindowText, pSetWindowIcons, pSetWindowRgn, pSetLayeredWindowAttributes, pUpdateLayeredWindow, pDestroyWindow, pSetCapture, pSetCursor, pClipCursor, pMoveWindowBits, pGetWindowStyleMasks, pGetWindowStateUpdates, pSysCommand, pFlashWindowEx, pActivateWindow, pGetDC from NtUserGetDCEx | win_data | no `user -> win_data` chain seen after commit 1 |
Result: unfixed tree 1 cycle (user <-> win_data); final tree 0 cycles in the stress runs and 0 in the Inventor session
(blocking edges only; `cycles.py`). No `user -> win_data` (blocking), no `win_data -> client surfaces_lock`, no
`surface mutex -> user`, no `dce surfaces_lock -> user`. win32u-internal pairs seen: user -> {gdi, font, display, session,
display_dc, dce surfaces_lock, surface, winproc}, client surfaces_lock -> user, dce surfaces_lock -> surface,
display -> session, gdi_lock -> display.

## Fix (fix/173 on integ b5d75449ffe)
1. f86a6357aed `win32u: Don't hold the user lock while drawing in NtUserDrawIconEx().` The frame (bitmap handles, size) is
   copied and the icon object released before the first GDI call.
2. 179ab6c9f83 `winex11: Ignore property changes of X windows that another thread has replaced.` (BadWindow, X_GetProperty)
3. 6635a80202b `winex11: Get the virtual screen rect before locking the Xinerama monitors.` xinerama_mutex -> display lock
   (xinerama_get_fullscreen_monitors -> virtual_screen_to_root) against display lock -> xinerama_mutex
   (UpdateDisplayDevices with the Xinerama handler, seen on :99). Never seen hanging; found by the debug build, 4 lines.
4. 65d8a4f9784 `winex11: Document why the surface flush only tries to lock the window data.` Comment only.
5. 1b6964ae62d `winex11: Set the window text with the window data locked.` (BadWindow, X_ChangeProperty)

### Staleness
- Icon frame read before drawing: the bitmaps can be deleted by a DestroyIcon in another thread between the release and
  NtGdiSelectBitmap (before: DestroyIcon waited for the draw). The select then fails and the blt draws the DC's default
  1x1 bitmap, or, if the handle value was reused, another bitmap; no crash (GDI handles are validated, a selected bitmap's
  deletion is deferred). Destroying an icon while another thread draws it is the app's race on Windows too. Not re-validated.
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
| repro (10 s runs, without a WM / with openbox) | master | integ | fix/173 |
|---|---|---|---|
| `visual_race.exe 10` (GetProperty) | 1 of 5 / 1 of 5 die | 9 of 10 / 1 of 10 | 0 of 25 / 0 of 25 |
| `visual_race.exe 10 text` (ChangeProperty) | 5 of 5 / 5 of 5 | 10 of 10 / 10 of 10 | 0 of 10 / 0 of 10 |
| `tests/r171/ulwrace.exe destroy 2000 1 - x` | 5 of 5 / 5 of 5 (GetProperty) | 10 of 10 / 10 of 10 (GetProperty) | 0 BadWindow of 20 |
ulwrace on the fix build no longer dies of BadWindow and therefore runs into 171: 5 of 20 runs hang (1 of 10 without a
WM, 4 of 10 with openbox), all five with dce.c's surfaces_lock held by a thread inside flush_window_surfaces and the user
lock and win_data free (`inst/173/out/ulw-fix-*-hang.txt`) = 171's list race, not this issue. With fix/171 on top (debug
build): 17 of 17 ulwrace runs finish (destroy 13, race 2, exit 2).

## The flush fallback (062) under fix/171's edge
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
  dc_attr_lock. No `-> user_mutex`. `user_mutex -> dce.c surfaces_lock` (171's edge) is in the same logs.
Caveat: a driver message whose low word were >= 0xc000 would take the class lookup path under +msg; the driver range is
0x80001000-0x80001fff, so it cannot happen today.

## Verification (fix = wt/173-build at 1b6964ae62d unless noted; Xvfb, without a WM / with openbox)
- iconlock icon, 8 s: fix 25 of 25 / 25 of 25 ok; integ 0 of 10 ok (10 hangs); master 1 of 9 ok (8 hangs).
- BadWindow repros: table above.
- lockstress 15000 nogl (46500 operations): fix seeds 1-6 without a WM and 7-12 with openbox: 12 of 12 done, no X error.
  integ seeds 2-9 without a WM: 5 done, 2 X_GetProperty, 1 X_ChangeProperty, 0 hangs (the reviewer had 1 hang in 9).
- Debug build, 173 + 171: every run listed under "Call table" finished; 0 X errors; 0 cycles.
- `tools/regress.sh unit`, both arches, vs deps/regress/b5d75449...-h26: user32:win fail 4 / 4, user32:msg fail 1 / 1,
  user32:sysparams fail 6 / 6 (the baseline's counts), user32:input, dce, monitor, clipboard, win32u:win32u, d3d9:device,
  opengl32:opengl pass with the baseline's todo / skip counts: 0 worse of 20 (`inst/173/out/ut-fix.txt`).
- 077 size-move table (`inst/173/sm.sh` = 130's script, Xvfb + openbox / awesome, at commit 3): fix and integ identical
  in all 19 cases (modmove 1/1, modresize 1/1, caption 1/1, click 0/0, quick 5/5, kbmove 1/1, kbresize 1/1, modfirst 1/1,
  selfmove 0/0, dblclick 1/1, stale 0/0).
- Harness trap: one wineserver must not serve two X displays in a row (X_UnmapWindow BadWindow on the other display's
  window, as in 077): `inst/173/run.sh` restarts the server when the display changes. `inst/173/out/batch1.txt` (first
  session) has such artifacts in its openbox halves; the numbers above are from later, clean batches.

## Inventor (inv2, :99)
- Debug build (fix/173 + fix/171 + debug patch): `INV=inv2 tools/invscen/run.sh all` 13 of 13 PASS (125 steps, 0 fail),
  then the `uilat` setup (part in sketch edit). Lock pairs: only allowed ones, 0 cycles; `display lock -> xinerama_mutex`
  seen (the Xinerama handler is in use there), `xinerama_mutex -> display lock` gone.
- **Not done on the plain fix build**: after the suite Inventor showed "Device limit reached" (the laptop "mafa" holds the
  seat, this host "Product is paused"). Nothing in the dialog was clicked; Inventor closed with kill-inventor, inv2 back on
  build/, lease released. Screenshot (shows the account, not for the repo): inst/173/uilat/dbg-rubber.png. One uilat
  `rubber` run went blind into that screen before I looked: XTest clicks at 102,85 and 600,820 (both outside the dialog:
  the ribbon behind the modal dialog and the Home list below it), pointer moves along y=820 and two Escape presses; the
  dialog was unchanged afterwards (the laptop's "Pause product" link is at 885,557 and was not touched).
- Open: suite 13/13 on wt/173-build, rubber-band fps A/B vs build/ (`P=inv2 inst/round2/m.sh TAG`), drag / resize of the
  main window and a dialog (`uilat wmdrag --hz 60`, 061's black-pixel check). None of the five commits is on the
  per-frame path of a single-threaded drag (commit 1: one struct copy per DrawIconEx; 2: one compare per PropertyNotify;
  5: SetWindowText), so no fps change is expected, but it is not measured.

## Inventor hangs seen before (item 5)
None of the recorded ones shows this pair: 048's four hang dumps are the loader lock against coreclr's binder lock (app
race), 109 is rpcrt4's unregister wait (fixed), 074 and 082 are a crash and an E_FAIL cascade, not hangs. No issue file has
a stack with user_lock / get_win_data except 157 (Wayland) and 171. The condition here (a thread inside DrawIconEx with
a DC made dirty by a window change, while another thread is in WindowPosChanged of a top-level window) needs two threads
with windows; nothing we have captured from Inventor shows it.

## 171
No common lines: fix/171 changes win32u window.c (apply_window_pos, destroy_window, free_window_handle,
destroy_thread_windows) and a comment in dce.c; this series changes win32u cursoricon.c and winex11 event.c, xinerama.c,
window.c. The three 171 commits cherry-pick onto fix/173 without conflicts (wt/173-dbg). Its new edge user ->
dce.c surfaces_lock is in the rule above.

## Weak spots
- Inventor on the plain fix build is not verified (above).
- The icon frame race is argued, not closed.
- X window lifetime: two unlocked uses were found by running (property handlers, SetWindowText). By reading, the other
  X requests on a whole window are made under win_data or are covered by ignore_error (SendEvent, ConfigureWindow,
  ChangeWindowAttributes, SetInputFocus) or go to gdi_display (errors ignored); X11DRV_SetCapture / SetCursor / ShowWindow
  use the window under win_data. `X11DRV_SystrayDockInsert` and `hwnd_from_window` / `set_net_active_window` use a
  window id after the release with SendEvent only. Not exhaustively proven; any further site would show as an X error
  with a backtrace in a synchronous run of the debug build.
- "No win32u call under kbd_mutex / palette_mutex / xrandr_mutex" rests on a lexical scan plus the absence of reports.
- The debug build only tracks pthread mutexes of win32u and winex11 that go through win32u_private.h / x11drv.h; Xlib's
  display lock, the GL / Vulkan locks and server-side waits are outside it. Trylocks are tracked but not counted as edges.
- Other win32u functions that draw with a handle locked: none found by scanning (menus use unlocked pointers), none
  reported in user32:menu / cursoricon / win / msg; only DrawIconEx was exercised on purpose.
- 062's trylock in the flush stays; the rule depends on it.
- winemac has the same DrawIconEx exposure if its GetDC equivalent locks window data (not looked at); commit 1 covers it.
- Commit 5 sends the title on the owner's connection from other threads: same pattern as set_window_visual, not
  separately stressed beyond visual_race text and lockstress.

## Tools
`tests/r173/`: iconlock.c, visual_race.c, flushpost.c; lockorder-debug.patch (applies to fix/173 + fix/171; also prints
X errors with a backtrace instead of asserting under +synchronous, and counts the flush fallback), lockorder.py, cycles.py;
run.sh / batch.sh / ls.sh / x.sh / sm.sh (copies of inst/173/: watchdog runner with gdb stacks, batches on Xvfb :1410
(no WM) / :1411 (openbox), lockstress loop, X servers, size-move table). Builds: wt/173-build (fix/173), wt/173-dbg (detached:
integ + 173 + 171 + debug patch uncommitted) / wt/173-dbg-build.
