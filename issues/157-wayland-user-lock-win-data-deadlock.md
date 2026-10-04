# 157 winewayland: deadlock between win32u's user lock and the driver's win_data_mutex (two threads showing/hiding windows)
Status: fixed on fix/157 (5 commits, rebased onto integ b5d75449ffe with 132; old tip kept as fix/157-v1), verified in the VM and with 132's probes, awaiting review · Found in: review of fix/134 (inst/134-review, `rv rapid 300`) · predates fix/134 · **serious for real use**: any
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

## Fix (winewayland.drv only; hashes here and in the table are the first series = fix/157-v1 on e00a74f6590, rebased hashes below)
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

## Rebase onto integ b5d75449ffe (132: client surfaces of other processes), 2026-10-04
Old tip 8641c9d1fba = branch `fix/157-v1`. New series (each commit compiles):
`a9d7aa29907` styles and text before the lock, `99b964dc032` cursor clipping after the release, `4f3a0256f3f` client surfaces
updated unlocked, `3bc7d04ebb6` cursor info before pointer.mutex, `7fb257635be` win_data before text_input.
Conflicts, all in WAYLAND_WindowPosChanged / the scale handler, both intents kept:
- commit 1: 132 added `wayland_win_data_lock/unlock` next to `wayland_win_data_get_config` (kept, with the new `style`
  parameter) and `wayland_remote_sinks_update(toplevel)` in the "no win data" early return (kept, after the state reads).
- commit 2: end of WindowPosChanged: sinks update first, then the cursor clipping.
- commit 3: 132's `toplevel` variable + `stale`; the stale block (detach client surfaces, destroy, run again) comes before
  the sinks update, so the second pass does it once for the new surface. Scale handler: `update_client_surfaces` and
  `wayland_remote_sinks_update` both after the release.
- commits 4, 5 applied as they were.

### 132's code against the lock order rule (rows 20-27 of the call table)
| # | held | caller | callee | lock it takes | |
|---|---|---|---|---|---|
| 20 | win_data | wayland_remote_sinks_detach / _update / _destroy (from wayland_surface_destroy, WindowPosChanged, scale handler, DestroyWindow) | list walk, wl requests; eventfd write after the unlock | none | read + debug build |
| 21 | win_data | wayland_remote_process_events (event thread): remote_sink_read_wakes, remote_sink_update, remote_sink_set_buffers, remote_sink_destroy | recv; NtDuplicateObject, NtQuerySection, wine_server_handle_to_fd, NtUnmapViewOfSection, NtClose; wayland_win_data_get (recursive); wl requests | ntdll / server only | read + debug build (traced run: 3 sinks, 730 buffer sets, 1622 placements, 428 hides) |
| 22 | none (lock dropped mid-walk) | remote_sink_get_geometry | NtUserGetWindowThread (shared memory), get_visible_region requests | none | read; only the event thread unlinks sinks, `next` is re-read after the relock |
| 23 | none | wayland_remote_sink_create (window thread, WM_WAYLAND_REMOTE_SURFACE) | NtUserGetWindowThread, NtUserGetAncestor (user lock) before win_data | user, then win_data | read |
| 24 | none | wayland_remote_window_changed <- WAYLAND_SetWindowStyle, WAYLAND_SetParent | NtUserGetAncestor (user lock) before win_data | user, then win_data | read |
| 25 | win_data | buffer_release (event thread listener) | interlocked operations on the shared block | none | read |
| 26 | win32u client surfaces_lock | wayland_client_surface_update / detach / destroy -> wayland_client_surface_set_remote | source_mutex; under it NtCreateSection, NtMapViewOfSection, socketpair, wine_server_fd_to_handle, NtClose; NtUserPostMessage after the unlock | source_mutex (leaf) | read + debug build (no pair with source_mutex reported) |
| 27 | source_mutex (from lock_remote_buffer to unlock_remote_buffer) | wayland_drawable_present_remote | eglQuerySurface, glFinish, eglMakeCurrent, glReadPixels | host GL only, no win32u or driver lock | read + debug build |
Result: no win32u lock is taken under a driver mutex in 132's code and no inversion with the reordered paths:
source_mutex is only taken with no driver lock held or under win32u's client surfaces_lock, and nothing is taken under it;
the sink paths take win_data only. Order with 132: `client surfaces_lock` -> `source_mutex`; `user` -> `win_data`.
(On integ without 157 the role change still called update_client_surfaces under win_data: win_data -> surfaces_lock ->
source_mutex; gone with commit 3.) Observation, not a bug here: source_mutex is one mutex for all client surfaces of the
process and is held over the frame readback, so client surface updates of other windows (under win32u's surfaces_lock)
wait for a glReadPixels.

### Role change with live sinks
The surface taken out of the win data is destroyed after the release; `wayland_surface_destroy` detaches the sinks
(132) before the wl_surface goes, the second pass creates the new surface and marks the sinks dirty. In the gap the event
thread finds no surface for the toplevel and hides the sink (its subsurface is still a child of the live old surface).
Checks on the host session (gnome-shell 50, NVIDIA EGL, renderer=gl), `tests/r157/r132.sh BUILD TAG`:
- `xp.exe host flip=8` with an idle and a live source: both sinks back in place after the role change; pix.py output
  identical to integ `build/`.
- `tests/r132/clip.sh` (13 steps) and `geo.sh` (11 steps): pix.py output identical to integ in every step; 0 protocol errors.
- `tests/r157/sinkstress.sh` (lockstress with xp.exe sources in A, in the role-flipping popup F and in D; 61500 operations):
  23 of 32 runs DONE with 0 protocol errors and, on the debug build, only allowed lock pairs. 9 failed, neither a lock order
  problem nor 132's code (both also without sources, `NOSRC=1`): 4 x protocol error = draft 170 (reproduces on unfixed integ
  with one UI thread: `lockstress glhide 3000`, 6 of 6), 5 x hang = draft 171 (win32u NULL write in
  register_window_surface with its list lock held; user lock and win_data free in the gdb dumps).

### Stress on the rebased build (vmwl guest, wt/157-build 7fb257635be; `inst/157/results2/`)
| compositor | rv rapid 300 | rv rapid 3000 | lockstress 3000 |
|---|---|---|---|
| GNOME 50 | 10 / 0 / 0 of 10 | 10 / 0 / 0 of 10 | 5 / 0 / 0 of 5 |
| KDE | 5 / 0 / 0 of 5 | 5 / 0 / 0 of 5 | 3 / 0 / 0 of 3 |
| sway | 5 / 0 / 0 of 5 | 5 / 0 / 0 of 5 | 3 / 0 / 0 of 3 |
(ok / hang / exited early.) 0 protocol errors; `vmwl/wl_xowner.sh gnome`: 26 PASS / 0 FAIL, protocol errors 0.
Debug build of the rebased tree on the host session (132's clip/geo/flip + 4 sink stress runs): reports only
`win_data -> win32u:display`, `win_data -> pointer / keyboard / text_input`, `seat -> data_device` (`inst/157/r132/dbg.folded`).

### Changes to shared scripts
`tests/r132/{env,geo,clip,run2}.sh` take `R132_PREFIX` / `R132_OUT` (defaults unchanged) so the probes can run on another
build without touching inst/132. WAYLAND_DEBUG=1 cannot be used with them: the trace lands in the same file as the
probe's handle line and splits it (the scripts then start `wl_winctl.exe` without a window and hang).
