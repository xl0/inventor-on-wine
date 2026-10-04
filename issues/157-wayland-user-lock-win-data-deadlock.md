# 157 winewayland: deadlock between win32u's user lock and the driver's win_data_mutex (two threads showing/hiding windows)
Status: fixed on fix/157 (5 commits on integ e00a74f6590), verified in the VM, awaiting review · Found in: review of fix/134 (inst/134-review, `rv rapid 300`) · predates fix/134 · **serious for real use**: any
app with two UI threads changing window visibility can hang for good

## Symptom
`rv.exe rapid 300` (inst/134-review/rv.c: main thread hides/shows A and its owned popups 300 times, a second thread does the
same with its own popup) never reaches "rapid loop done": reviewer 4 of 7 runs on integ and 7 of 7 on fix/134, my runs on
fix/134 4 of 5. Both threads sit in pthread_mutex_lock forever, the Wayland event thread too.

## Stacks (fix/134 build, `gdb -p PID -ex 'source tools/gdb/winesyms.py' -ex 'thread apply all bt'`, inst/134/157-rapid-stacks.txt)
Thread 1 holds the user lock (window pointer), wants win_data_mutex:
```
wayland_win_data_get (window.c)  <- get_window_surface_contents <- wayland_window_surface_flush
<- window_surface_flush (dce.c) <- window_surface_set_shape <- update_surface_region (win32u/window.c)
<- apply_window_pos <- set_window_pos <- NtUserSetWindowPos <- swp_owner_popups <- set_window_pos <- show_window
```
Thread 4 holds win_data_mutex, wants the user lock:
```
user_lock <- get_user_handle_ptr <- get_win_ptr <- get_window_long_size <- NtUserGetWindowLongW
<- wayland_win_data_get_config <- wayland_win_data_create_wayland_surface <- WAYLAND_WindowPosChanged
<- apply_window_pos <- set_window_pos <- NtUserSetWindowPos <- show_window
```
(the reviewer saw the same pair with `wayland_surface_make_toplevel` -> `NtUserInternalGetWindowText` as the second path.)
The Wayland event thread then blocks in `wayland_win_data_get` (xdg_toplevel_handle_configure), so the process stops
handling compositor events as well.

## Cause
Lock order. win32u calls the driver's surface flush with the user lock held (`update_surface_region` keeps the WND pointer
across `window_surface_set_shape`), and the flush takes win_data_mutex. `WAYLAND_WindowPosChanged` takes win_data_mutex and
then calls NtUser functions that take the user lock (window style in `wayland_win_data_get_config`, window text in
`wayland_surface_make_toplevel`). Upstream a334c147f81 ("Avoid ABBA deadlocks between win_data_mutex and user_mutex") fixed two such
calls (`NtUserIsWindowVisible`) and names the rule (user lock first); these are the remaining ones. fix/134 adds no call that
takes the user lock under win_data_mutex (its property/atom calls are plain server requests; the owner lookup runs before
the lock).

## Lock order rule
win32u's locks first, the driver's window data last:
`client surfaces_lock` (win32u window.c) -> `user lock` -> `window surface lock` -> `win_data_mutex` ->
`pointer / keyboard / text_input / seat mutex`; under win_data_mutex also win32u's `display lock` -> `output_mutex` (leaf).
- The fix is on the driver side, not in `update_surface_region`. win32u calls the driver with a lock held in two places: the
  surface flush with the window pointer (user lock) held (`apply_window_pos` keeps `win` across `update_surface_region` ->
  `window_surface_set_shape` / `set_clip` -> flush; upstream master still does), and the client surface callbacks (update,
  detach, present, destroy) with the client `surfaces_lock` held. In this driver both need win_data: the window contents and
  the wayland_surface live in it. Making win32u drop its locks around every driver surface callback is a much larger change
  to shared code, and upstream already picked this direction for this driver: a334c147f81 "Avoid ABBA deadlocks between
  win_data_mutex and user_mutex" ("always acquire the user_mutex lock on the user32 side first, and then the win_data_mutex").
- winex11 has the opposite order: it calls NtUserGetWindowLongW / GetWindowText / GW_OWNER under its win data lock everywhere
  (set_wm_hints, sync_window_style, create_whole_window...) and gets away with it because its surface flush and client
  surface callbacks never take the win data lock. Not a model for this driver.
- winemac mixes both like winewayland did (surface.c's flush takes win_data; window.c reads styles under it, e.g.
  macdrv_WindowPosChanged): same latent bug by reading, not tested, not touched.
- `get_user_handle_ptr` takes the user lock for every handle (lock, look up, unlock), also for windows of other processes,
  so every `get_win_ptr` user counts.

## Call table
"seen" = reported by the debug build (`tests/r157/lockorder-debug.patch`: backtrace whenever a win32u lock is acquired while
the thread holds a driver mutex) in rapid / lockstress on the unfixed tree, folded output `inst/157/vm/dbg0/all.folded`;
"read" = by reading only.

Driver holds a lock and calls into win32u (before the fix):
| # | held | caller | callee | lock it takes | | fixed by |
|---|---|---|---|---|---|---|
| 1 | win_data | WAYLAND_WindowPosChanged -> wayland_win_data_create_wayland_surface | NtUserGetWindowLongW (GWL_EXSTYLE, GWL_STYLE) | user | seen, hang stacks | a267640e53b |
| 2 | win_data | ... -> wayland_win_data_get_config | NtUserGetWindowLongW (GWL_STYLE) | user | seen, hang stacks | a267640e53b |
| 3 | win_data | ... -> wayland_surface_make_toplevel | NtUserInternalGetWindowText | user | seen | a267640e53b |
| 4 | win_data | ... -> reapply_cursor_clipping | NtUserGetClipCursor, NtUserClipCursor | display (seen); user via get_present_rect when the clip rect is empty (read); then pClipCursor -> win_data, pointer.mutex | seen | 9106309981b |
| 5 | win_data | ... role change -> update_client_surfaces | win32u | client surfaces_lock, then user (NtUserGetAncestor, get_client_surface_rects; the update callback's NtUserIsWindowVisible) | seen | 56eebc11c39 |
| 6 | win_data | wp_fractional_scale_handle_scale (event thread) -> update_client_surfaces | win32u | same as 5 | read (needs a scale change) | 56eebc11c39 |
| 7 | pointer.mutex | wayland_set_cursor -> wayland_pointer_set_cursor_shape / wayland_pointer_update_cursor_buffer | NtUserGetIconInfo | user (get_icon_ptr) | read (no pointer over the windows in the VM runs) | 4eb976fc813 |
| 8 | text_input.mutex | WAYLAND_SetIMECompositionRect | wayland_win_data_get | win_data, while wayland_surface_destroy takes text_input.mutex under win_data (seen): driver-internal ABBA, no win32u lock | read | 8641c9d1fba |
| 9 | win_data | set_window_surface_contents / wayland_client_surface_present -> wayland_surface_reconfigure -> wayland_surface_get_rect_in_monitor | NtUserMonitorFromRect, NtUserGetMonitorInfo | display only | seen | allowed (leaf) |
| 10 | win_data | wayland_surface_create | NtUserGetLayeredWindowAttributes | none (server request) | read | - |
| 11 | win_data | 134: exported_handle, wayland_surface_clear_role, wayland_surface_import_parent (also via wayland_surface_mapped -> update_owned_toplevels in the flush) | NtUserSetProp, NtFindAtom, NtUserRemoveProp, NtUserGetProp, NtQueryInformationAtom | none: server requests; GetProp reads the shared window object (seqlock) and a per-thread cache | read + no report in runs that export on every map | - |
| 12 | win_data | keyboard_handle_enter | NtUserPostMessage | none (server request) | read | - |
| 13 | win_data | WAYLAND_SetWindowIcons, flush, ensure_contents | NtGdi* (regions, DCs, bitmaps), NtCreateSection | GDI handle lock | read | - |
| 14 | win_data | WAYLAND_CreateWindowSurface | window_surface_release, window_surface_create | none seen (no window surface lock) | no report | - |
| 15 | win_data | wayland_surface_destroy (WindowPosChanged) | - | pointer / keyboard / text_input mutex (driver order) | seen | - |
| 16 | text_input.mutex | text_input_leave / text_input_done | NtUserMessageCall(WINE_IME_POST_UPDATE) | imm_mutex + posted message, no user lock | read | - |
| 17 | seat.mutex | WAYLAND_ClipboardWindowProc -> wayland_data_device_init | - | data_device.mutex | seen | - |
| 18 | data_device.mutex, keyboard.mutex, xkb_layouts_mutex | | no win32u calls inside | | read | - |
| 19 | output_mutex (under display lock) | WAYLAND_UpdateDisplayDevices | device manager callbacks, NtUserGetSystemDpiForProcess | registry, no user lock | read | - |
Already correct: set_client_surface and wayland_client_surface_update read NtUserIsWindowVisible before the lock (a334c147f81);
is_window_managed, the owner lookup and 134's is_foreign_owner run before the lock; wayland_configure_window and the
configure / scale handlers call send_message, NtUserSetRawWindowPos, NtUserPostMessage, NtUserExposeWindowSurface after the release.

win32u holds a lock and calls the driver:
| held | path | driver lock taken |
|---|---|---|
| user (window pointer) | apply_window_pos -> update_surface_region -> window_surface_set_shape / set_clip -> flush | window surface lock, win_data (seen in every hang) |
| client surfaces_lock | client_surface_present, update_client_surfaces, client_surface_update, use_window_client_surface, get_unused_client_surface, detach_client_surfaces, client_surface_release -> present / update / detach callbacks | win_data (read; a GL swap goes through it) |
| display lock | lock_display_devices -> pUpdateDisplayDevices | output_mutex |
Called without the user lock (read at the call sites): pWindowPosChanging, pWindowPosChanged, pSetWindowStyle, pSetWindowText,
pSetWindowIcons, pDestroyWindow (both sites, after release_win_ptr / user_unlock), pSetCursor, pClipCursor, pSetCursorPos,
pSetLayeredWindowAttributes, pUpdateLayeredWindow, pCreateWindowSurface, pSysCommand.

## Fix (branch fix/157 on integ e00a74f6590, winewayland.drv only)
1. a267640e53b `winewayland: Get the window styles and text before locking the window data.` WAYLAND_WindowPosChanged reads
   style, ex style and (for windows with a surface) the text next to the existing is_window_managed / owner lookup and passes
   them down (rows 1-3).
2. 9106309981b `winewayland: Reapply the cursor clipping with the window data unlocked.` (row 4).
3. 56eebc11c39 `winewayland: Update the client surfaces with the window data unlocked.` A surface that has to change its role
   is taken out of the win data under the lock; after the release the client surfaces are detached (update_client_surfaces)
   and the surface destroyed, then WindowPosChanged runs again to create the new one. The scale handler updates after the
   release (rows 5, 6).
4. 4eb976fc813 `winewayland: Get the cursor info before locking the pointer.` One NtUserGetIconInfo before pointer.mutex instead
   of up to two under it (row 7). Without it the cycle is user -> win_data (flush), win_data -> pointer.mutex (surface destroy),
   pointer.mutex -> user (cursor set, e.g. on pointer enter in the event thread): three threads.
5. 8641c9d1fba `winewayland: Lock the window data before the text input.` (row 8). Not a user-lock problem but the same kind of
   inversion on win_data_mutex, 10 lines; drop the commit if it should be its own issue.
Row 9 stays: the display lock is taken under win_data, and by reading nothing under the display lock takes the user lock or
win_data (lock_display_devices: registry, driver UpdateDisplayDevices -> output_mutex, GPU enumeration).

## Staleness of what is now read before the lock
- Style / ex style: they join `managed` (computed from the same styles), the owner and the rects, which were already taken
  before the lock: one snapshot per call instead of styles read at two later points. Style writers (SetWindowLong from
  any thread of the process) never took win_data, so the lock did not order them against this read before either. The one
  ordering lost: two WindowPosChanged for the same window in two threads; the later locker now applies its earlier snapshot
  where it used to read the styles under the lock. Its rects, flags, managed and owner were already per-call arguments with
  exactly that behaviour, and win32u runs apply_window_pos on the window's own thread (SetWindowPos / ShowWindow from another
  thread are sent as messages), UpdateLayeredWindow from a foreign thread being the exception.
- Window text: before, a title set by another thread could not be lost (the text was read under win_data, and pSetWindowText
  takes win_data). Now: thread 1 reads the old text, thread 2 sets the text and its pSetWindowText finds no toplevel yet,
  thread 1 creates the toplevel with the old title, wrong until the next SetWindowText. It needs the text to change in another
  thread than the one in WindowPosChanged, exactly while the window gets its toplevel role; win32u sets the text in the
  window's thread (WM_SETTEXT is sent), so it takes a direct DefWindowProc(WM_SETTEXT) on a foreign window or a foreign-thread
  UpdateLayeredWindow. Accepted, not re-validated (an exact fix needs a per-window serial and a retry).
- Role change: between the release and the second pass the window has no wayland_surface, a state the flush, the event
  handlers and the client surface callbacks already handle (same as a hidden child). If another thread's WindowPosChanged for
  the same window creates a surface inside that window before update_client_surfaces ran, the client subsurfaces stay on the
  surface that is then destroyed (wl_subsurface.place_above on a foreign sibling would be a protocol error): needs two threads in
  WindowPosChanged for one window during a role change with GL children, see above. The destroyed surface itself is safe: no
  other thread can reach it once it is out of the win data (wayland_win_data_destroy destroys surfaces unlocked as well).
- Cursor clipping after the release: WAYLAND_ClipCursor re-reads the surface under its own lock; it now also sees a surface
  created by this call (before, the new surface was not in the win data yet when it ran).
- Cursor info: a private copy (bitmaps) made before pointer.mutex; a concurrent SetCursor is ordered by pointer.mutex as before.
- IME rectangle: computed before the focus check; a rectangle for a window that lost the text input focus is dropped as before.

## Verification (vmwl guest, 2026-10-04; fixed = wt/157-build 8641c9d1fba, unfixed = build/ = integ e00a74f6590)
Stress under a watchdog (`tests/r157/batch.sh`, `g-run.sh`: a run that does not exit in 60 s / 300 s gets `thread apply all bt`
and is killed by PID). ok / hang / exited early:
| compositor | build | rv rapid 300 | rv rapid 3000 | lockstress 3000 (seeds 1..n) |
|---|---|---|---|---|
| GNOME 50 | unfixed | 2 / 8 / 0 of 10 | 0 / 5 / 0 of 5 | 0 / 5 / 0 of 5 |
| GNOME 50 | fixed | 20 / 0 / 0 of 20 | 20 / 0 / 0 of 20 | 10 / 0 / 0 of 10 |
| KDE (KWin 6) | fixed | 10 / 0 / 0 of 10 | 10 / 0 / 0 of 10 | 5 / 0 / 0 of 5 |
| sway 1.11 | fixed | 10 / 0 / 0 of 10 | 10 / 0 / 0 of 10 | 5 / 0 / 0 of 5 |
- Unfixed hangs are the lock pair: `inst/157/vm/gnome-integ-rapid300-hang-1.txt` (flush vs wayland_win_data_get_config),
  `gnome-integ-stress-hang-1.txt` (flush vs NtUserGetWindowLongW in wayland_win_data_create_wayland_surface; lockstress stops
  within its first operations every time).
- 0 Wayland protocol errors in every log; the last two lockstress runs per compositor ran with WAYLAND_DEBUG=1.
- The first KDE batch (same numbers) ran partly behind KDE's screen locker (5 min idle); the table row is the rerun with the
  locker disabled, desktop checked by screenshot afterwards. Raw summaries: `inst/157/results/*.txt`.
- `tests/r157/lockstress.c` (4 threads + short-lived ones, 12300 operations per run at N=3000): show/hide, minimize/restore/maximize,
  text (own thread, sent, direct DefWindowProc from another thread), style and ex style flips from another thread, owner changes,
  layered attributes and UpdateLayeredWindow (own and foreign thread), managed <-> unmanaged role change of a popup with a GL
  child that another thread swaps, child <-> toplevel, painting from a foreign thread, threads that exit with their windows.
  Its GL thread uses swap interval 0: with an interval the swap blocks while the popup is hidden (163, not a lock problem).
- Debug build of the fixed tree (lockorder-debug.patch), GNOME: wl_xowner.sh with its clicks, rapid 300, lockstress x2,
  user32:win, msg, input: 123 reports, all `win_data -> win32u:display` (monitor functions, row 9),
  `win_data -> pointer / keyboard / text_input / seat`, `seat -> data_device`. No `-> win32u:user`, `client_surfaces` or
  `window_surface` (`inst/157/vm/dbg2/all.folded`; unfixed: `dbg0/all.folded`).
- `vmwl/wl_xowner.sh` (fixed build): GNOME 26 PASS / 0 FAIL, KDE 26 PASS / 0 FAIL, protocol errors 0 in all 9 logs (WAYLAND_DEBUG=1).
- user32:win, user32:msg under Wayland (GNOME), 2 runs each: win 11 failures on 8 lines, msg 52 failures on 30 lines, the
  same line sets in all four unfixed and all four fixed runs (`inst/157/vm/ut/`). One more fixed + unfixed win run later in
  the session: same 8 lines. The debug-build win run once had win.c:4701/4707 on top (a pending WM_INPUTLANGCHANGEREQUEST
  right after the click test), not seen in the six clean runs.
- No win32u change, so no host regress units. Inventor on Wayland not run (belongs to the host session's owner).

## Weak spots / not covered
- Title race and the role-change window described under staleness: argued, not closed.
- Row 7 (cursor info under pointer.mutex) and row 6 (scale handler) were found and fixed by reading, never seen at run time:
  the unfixed debug runs had no pointer input (the icon code only runs while the pointer is over the window), the fixed debug
  run had wl_xowner's clicks but cannot show that the old call was reached; no scale change happened in any run.
- "Display lock is a leaf" rests on reading lock_display_devices and a scan of the 31 locked regions in sysparams.c for direct
  window calls (none); helpers were not followed.
- winemac has the same pattern (see rule); untouched.
- The debug patch only sees locks it was told about (user, display, client surfaces, window surface + driver mutexes); GDI
  handle locks, imm_mutex, the opengl drawable locks are outside it.

## Infra changes on the way
- vmwl/run.sh no longer uses bwrap (no user namespaces in the sandbox on the 26.04 host): one read-only virtiofsd per shared
  path; `/host/.mounts` remounts after a guest reboot; default build is `build/` (wt/wayland-build is gone).
- vmwl/wl_xowner.sh needs numpy from the package prefix (loads `tools/sysroot.sh env` now).
- Leaving a KDE session with session.sh does not work (plasma user services survive): reboot the guest. KDE autolock is
  disabled in the guest now. gdb attach in the guest needs `kernel.yama.ptrace_scope=0` (g-run.sh sets it).
