# 157 winewayland: deadlock between win32u's user lock and the driver's win_data_mutex (two threads showing/hiding windows)
Status: fixed on fix/157 (7 winewayland commits on integ b5d75449ffe), reviewed once, review fixes verified; short re-review of the changed role-change design pending · Found in: review of fix/134 (inst/134-review, `rv rapid 300`) · predates fix/134 · **serious for real use**: any
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
`client surfaces_lock` (win32u window.c) -> `user lock` -> dce.c `surfaces_lock` (window surface list) ->
`window surface lock` -> `win_data_mutex` -> `pointer / keyboard / text_input / seat mutex`; under win_data_mutex also
win32u's `display lock` -> `output_mutex` (leaf). 132's `source_mutex` is a leaf under the client surfaces_lock.
- The fix is on the driver side, not in `update_surface_region`. win32u calls the driver with locks held: the surface flush
  with the window pointer (user lock) held (`apply_window_pos` keeps `win` across `update_surface_region` ->
  `window_surface_set_shape` / `set_clip` -> flush; upstream master still does), every flush from `flush_window_surfaces`
  with dce.c's `surfaces_lock` held, and the client surface callbacks (update, detach, present, destroy) with the client
  `surfaces_lock` held. In this driver all of them need win_data: the window contents and the wayland_surface live in it.
  Making win32u drop its locks around every driver surface callback is a much larger change to shared code, and upstream
  already picked this direction for this driver: a334c147f81 "Avoid ABBA deadlocks between win_data_mutex and user_mutex"
  ("always acquire the user_mutex lock on the user32 side first, and then the win_data_mutex").
- winex11 uses the opposite order (window data -> user lock): its rule and its own deadlock are in issue 173. Not a model
  for this driver.
- winemac mixes both like winewayland did (surface.c's flush takes win_data; window.c reads styles under it, e.g.
  macdrv_WindowPosChanged): same latent bug by reading, not tested, not touched.
- `get_user_handle_ptr` takes the user lock for every handle (lock, look up, unlock), also for windows of other processes,
  so every `get_win_ptr` user counts.

## Call table
"seen" = reported by the debug build (`tests/r157/lockorder-debug.patch`: backtrace whenever a win32u lock is acquired while
the thread holds a driver mutex) on the unfixed tree, folded output `inst/157/vm/dbg0/all.folded`; "read" = by reading only.
Rows 20-27 are 132's code (wayland_remote.c), checked after the rebase: nothing to fix there.

Driver holds a lock and calls into win32u (before the fix):
| # | held | caller | callee | lock it takes | | fixed by |
|---|---|---|---|---|---|---|
| 1 | win_data | WAYLAND_WindowPosChanged -> wayland_win_data_create_wayland_surface | NtUserGetWindowLongW (GWL_EXSTYLE, GWL_STYLE) | user | seen, hang stacks | 166dc20fe8c |
| 2 | win_data | ... -> wayland_win_data_get_config | NtUserGetWindowLongW (GWL_STYLE) | user | seen, hang stacks | 166dc20fe8c |
| 3 | win_data | ... -> wayland_surface_make_toplevel | NtUserInternalGetWindowText | user | seen | 166dc20fe8c |
| 4 | win_data | ... -> reapply_cursor_clipping | NtUserGetClipCursor, NtUserClipCursor | display (seen); user via get_present_rect when the clip rect is empty (read); then pClipCursor -> win_data, pointer.mutex | seen | 4d2aff975a9 |
| 5 | win_data | ... role change -> update_client_surfaces (call removed, see Fix) | win32u | client surfaces_lock, then user (NtUserGetAncestor, get_client_surface_rects; the update callback's NtUserIsWindowVisible) | seen | 3ed44f42f3e |
| 6 | win_data | wp_fractional_scale_handle_scale (event thread) -> update_client_surfaces | win32u | same as 5 | read; at run time by the reviewer (scale toggle + GL swap: integ hangs 2/2, fix 3/3 clean) | 3ed44f42f3e |
| 7 | pointer.mutex | wayland_set_cursor -> wayland_pointer_set_cursor_shape / wayland_pointer_update_cursor_buffer | NtUserGetIconInfo | user (get_icon_ptr) | read (no pointer over the windows in the VM runs) | a8f1a76a26c |
| 8 | text_input.mutex | WAYLAND_SetIMECompositionRect | wayland_win_data_get | win_data, while wayland_surface_destroy takes text_input.mutex under win_data (seen): driver-internal ABBA, no win32u lock | read | 0046202e1aa |
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
| 20 | win_data | wayland_remote_sinks_detach / _update / _destroy (from wayland_surface_destroy, WindowPosChanged, scale handler, DestroyWindow) | list walk, wl requests; eventfd write after the unlock | none | read + debug build | - |
| 21 | win_data | wayland_remote_process_events (event thread): remote_sink_read_wakes, remote_sink_update, remote_sink_set_buffers, remote_sink_destroy | recv; NtDuplicateObject, NtQuerySection, wine_server_handle_to_fd, NtUnmapViewOfSection, NtClose; wayland_win_data_get (recursive); wl requests | ntdll / server only | read + debug build (traced run: 3 sinks, 730 buffer sets, 1622 placements, 428 hides) | - |
| 22 | none (lock dropped mid-walk) | remote_sink_get_geometry | NtUserGetWindowThread (shared memory), get_visible_region requests | none | read; only the event thread unlinks sinks, `next` is re-read after the relock | - |
| 23 | none | wayland_remote_sink_create (window thread, WM_WAYLAND_REMOTE_SURFACE) | NtUserGetWindowThread, NtUserGetAncestor (user lock) before win_data | user, then win_data | read | - |
| 24 | none | wayland_remote_window_changed <- WAYLAND_SetWindowStyle, WAYLAND_SetParent | NtUserGetAncestor (user lock) before win_data | user, then win_data | read | - |
| 25 | win_data | buffer_release (event thread listener) | interlocked operations on the shared block | none | read | - |
| 26 | win32u client surfaces_lock | wayland_client_surface_update / detach / destroy -> wayland_client_surface_set_remote | source_mutex; under it NtCreateSection, NtMapViewOfSection, socketpair, wine_server_fd_to_handle, NtClose; NtUserPostMessage after the unlock | source_mutex (leaf) | read + debug build (no pair with source_mutex reported) | - |
| 27 | source_mutex (from lock_remote_buffer to unlock_remote_buffer) | wayland_drawable_present_remote | eglQuerySurface, glFinish, eglMakeCurrent, glReadPixels | host GL only, no win32u or driver lock | read + debug build | - |
| 28 | user (update_surface_region path), dce.c surfaces_lock (flush_window_surfaces), window surface lock | wayland_window_surface_flush -> wayland_buffer_queue_get_free_buffer | wl_display_dispatch_queue on the queue of the surface | none; blocks until the compositor releases a buffer when all three are busy, with those win32u locks held | read (review) | left |
| 29 | win_data | WAYLAND_CreateWindowSurface -> window_surface_release(previous) -> wayland_buffer_queue_destroy | wl_display_dispatch_queue_pending: buffer release listeners (NtUnmapViewOfSection, NtGdiDeleteObjectApp) | none | read (review) | left |
Already correct: set_client_surface and wayland_client_surface_update read NtUserIsWindowVisible before the lock (a334c147f81);
is_window_managed, the owner lookup and 134's is_foreign_owner run before the lock; wayland_configure_window and the
configure / scale handlers call send_message, NtUserSetRawWindowPos, NtUserPostMessage, NtUserExposeWindowSurface after the release.

win32u holds a lock and calls the driver:
| held | path | driver lock taken |
|---|---|---|
| dce.c surfaces_lock (window surface list) | flush_window_surfaces -> window_surface_flush of every surface -> driver flush | window surface lock, win_data |
| user (window pointer) | apply_window_pos -> update_surface_region -> window_surface_set_shape / set_clip -> flush | window surface lock, win_data (seen in every hang) |
| client surfaces_lock | client_surface_present, update_client_surfaces, client_surface_update, use_window_client_surface, get_unused_client_surface, detach_client_surfaces, client_surface_release -> present / update / detach callbacks | win_data (read; a GL swap goes through it) |
| display lock | lock_display_devices -> pUpdateDisplayDevices | output_mutex |
Called without the user lock (read at the call sites): pWindowPosChanging, pWindowPosChanged, pSetWindowStyle, pSetWindowText,
pSetWindowIcons, pDestroyWindow (both sites, after release_win_ptr / user_unlock), pSetCursor, pClipCursor, pSetCursorPos,
pSetLayeredWindowAttributes, pUpdateLayeredWindow, pCreateWindowSurface, pSysCommand.

## Fix (branch fix/157 = integ b5d75449ffe + 7 commits, winewayland.drv only)
1. 166dc20fe8c `winewayland: Get the window styles and text with the window data unlocked.` WAYLAND_WindowPosChanged reads
   style and ex style next to the existing is_window_managed / owner lookup and passes them down. A toplevel is created
   without a title; after the release the text is read and set, unless a title was set since the toplevel was created
   (`has_title`) (rows 1-3).
2. 4d2aff975a9 `winewayland: Reapply the cursor clipping with the window data unlocked.` (row 4).
3. 3ed44f42f3e `winewayland: Don't update the client surfaces with the window data locked.` The surface that changes its role
   is still destroyed and replaced under the lock; the `update_client_surfaces` call that detached the client surfaces first
   is gone. Each wayland_surface has a `serial`: `wayland_client_surface_attach` re-creates a client subsurface that was made
   for another surface of the toplevel (win32u updates the client surfaces right after WindowPosChanged), and
   `wayland_surface_reconfigure_subsurface` only places a popup above the owner's client surface if that is attached to the
   owner's current surface. 132's sinks are detached by `wayland_surface_destroy`, under the lock. The scale handler updates
   the client surfaces after the release (rows 5, 6).
4. a8f1a76a26c `winewayland: Get the cursor info before locking the pointer.` One NtUserGetIconInfo before pointer.mutex
   instead of up to two under it, and only when the pointer is on the window (row 7). Without it the cycle is user ->
   win_data (flush), win_data -> pointer.mutex (surface destroy), pointer.mutex -> user (cursor set): three threads.
5. 0046202e1aa `winewayland: Lock the window data before the text input.` (row 8; a driver-internal inversion on
   win_data_mutex, no win32u lock).
6. 74a086bfff1 `winewayland: Check that the window has a surface when the pointer moves.` The relative motion handler used
   the surface of the focused window without a check (review finding; on integ the window between reading the focus and
   locking the data is tiny, an earlier version of commit 3 had widened it into a reproducible fault).
7. 750bb757812 `winewayland: Don't commit buffers to surfaces without a role.` Part of issue 170 (protocol error when a
   toplevel with a GL child is shown again): fixes the case of a window hidden for 5 ms or longer; see 170 for the rest.
Row 9 stays: the display lock is taken under win_data, and by reading nothing under the display lock takes the user lock or
win_data (lock_display_devices: registry, driver UpdateDisplayDevices -> output_mutex, GPU enumeration).
History: `fix/157-v1` (first series on e00a74f6590), `fix/157-v2` (rebased; it destroyed the old surface outside the lock,
the reviewed version with the relative-motion fault), `fix/157-surface-per-show` (experiment for 170).

## Staleness of what is read outside the lock
- Style / ex style: they join `managed` (computed from the same styles), the owner and the rects, which were already taken
  before the lock: one snapshot per call. Style writers (SetWindowLong from any thread of the process) never took win_data,
  so the lock did not order them against this read before either. The one ordering lost: two WindowPosChanged for the same
  window in two threads; the later locker applies its earlier snapshot. Its rects, flags, managed and owner were already
  per-call arguments with exactly that behaviour, and win32u runs apply_window_pos on the window's own thread
  (UpdateLayeredWindow from a foreign thread being the exception).
- Window text: no stale title in any order. The toplevel exists (under the lock) before the text is read; pSetWindowText
  takes win_data, sets the title and `has_title`; the late set in WindowPosChanged is skipped when `has_title` is set. If
  pSetWindowText ran before the toplevel existed, the text it stored is what the later read returns.
- Role change: atomic under the lock again. Between the destruction of the old surface and win32u's
  update_client_surfaces (the next thing apply_window_pos does) the client subsurfaces are children of a destroyed
  wl_surface (unmapped by the compositor, legal); every use of them goes through the serial check first.
- Cursor clipping after the release: WAYLAND_ClipCursor re-reads the surface under its own lock.
- Cursor info: a private copy (bitmaps) made before pointer.mutex. If the pointer is not on the window nothing is read;
  the enter handler applies the cursor stored in the surface (stored before the focus check, so one of the two sees it).
- IME rectangle: computed before the focus check; a rectangle for a window that lost the text input focus is dropped.

## Verification
Unfixed = integ `build/`; fixed = the final tip unless a series is named. ok / hang / exited early; a run is a hang when it
does not exit within its watchdog (60 s / 300 s), then `thread apply all bt` and the mutex owners are saved.

### vmwl guest (llvmpipe), `tests/r157/batch.sh`
| compositor | build | rv rapid 300 | rv rapid 3000 | lockstress 3000 |
|---|---|---|---|---|
| GNOME 50 | unfixed e00a74f6590 | 2 / 8 / 0 | 0 / 5 / 0 | 0 / 5 / 0 |
| GNOME 50 | fix v1 | 20 / 0 / 0 | 20 / 0 / 0 | 10 / 0 / 0 |
| KDE | fix v1 | 10 / 0 / 0 | 10 / 0 / 0 | 5 / 0 / 0 |
| sway | fix v1 | 10 / 0 / 0 | 10 / 0 / 0 | 5 / 0 / 0 |
| GNOME / KDE / sway | fix v2 | 10 / 5 / 5 ok | 10 / 5 / 5 ok | 5 / 3 / 3 ok |
| GNOME 50 | final, with jitter | 10 / 0 / 0 | 10 / 0 / 0 | 5 / 0 / 0 |
| KDE | final, with jitter | 5 / 0 / 0 (jitter 500 events/s; see below) | 5 / 0 / 0 | 3 / 0 / 0 |
| sway | final, with jitter | 5 / 0 / 0 | 5 / 0 / 0 | 3 / 0 / 0 |
- Jitter = `tests/r157/jitter.py vm`: relative pointer motion through the guest's PS/2 mouse and shift presses over QMP
  for the whole batch (guest IRQ counts confirm them), one click at 640,400 first. Unthrottled it sends ~2300 events/s:
  GNOME and sway ran that way. On KDE that rate starved the probe: one of 5 rapid 300 runs took 63 s (finished by
  itself; in three more such runs caught with a 20 s watchdog the UI thread is running in PeekMessage, draining input,
  no lock involved), and the first run after the guest reboot had no windows (not counted). The KDE rapid 300 number is the
  rerun at 500 events/s (jitter.py's default now); 84 more KDE rapid 300 runs at 2300/s with 15 / 20 s watchdogs: 80 in
  time, 3 slow as above, 1 waiting for wineboot at process start.
- Unfixed hangs are the lock pair (`inst/157/vm/gnome-integ-rapid300-hang-1.txt`, `gnome-integ-stress-hang-1.txt`).
- 0 Wayland protocol errors in every fixed run; the last two lockstress runs per batch ran with WAYLAND_DEBUG=1.
- `vmwl/wl_xowner.sh` on the final tip: GNOME 26 PASS / 0 FAIL twice in a fresh session, protocol errors 0, KDE 26 PASS / 0 FAIL, protocol errors 0 (v1 and v2: 26 PASS / 0 FAIL each).
  A GNOME run right after the 2300 events/s batch had 22 PASS / 4 FAIL (self x3: no window within 10 s, prefix update;
  hide-5: the probe ran ~12 s behind the script, no error in its log); not reproduced in the two fresh-session runs.
- user32:win, user32:msg under Wayland (GNOME), final tip vs integ b5d75449ffe: win 11 failures on 8 lines, msg 52 failures on 30 lines, identical line sets in 1 integ and 2 fixed runs each
  (v1 vs e00a74f6590: win 8 failing lines, msg 30, identical sets in 4 + 4 runs).
- Runner traps hit and closed: a guest session that did not come up (gnome-shell start timeout under host load) and a
  prefix whose explorer had no graphics driver both let `rv rapid` "finish" without a window; g-run.sh now refuses to run
  without a Wayland socket and counts runs without windows as failed. The numbers above are from runs with both checks,
  or from batches whose lockstress finished (it cannot without windows).

### Host session (gnome-shell 50 headless, NVIDIA EGL), final tip
- Reviewer's roleflip + jitter (`inst/157-review/rf-run.sh 157 TAG 1500 25`: one UI thread flips a popup managed /
  unmanaged under ~640 relative-motion events/s): 8/8 DONE, faults 0, protocol errors 0 (v2: 0/4, integ 3/3). With a GL
  child swapped by a second thread (600 flips, ~220k swaps): 3/3.
- GL child visible after each role flip (`roleflip 5 3000 gl`, screenshot after every flip): 5/5, 15000 px of the child
  each time (the serial re-attach works).
- First frame after show (`rv.exe other`, ShowWindow from wl_winctl, screenshot ~0.5 s later): 5/5 fully painted
  (45018 yellow px before, 0 hidden, 45018 after each show).
- 132's probes (`tests/r157/r132.sh`): clip.sh 13 steps and the role flip with an idle and a live source: pix.py output
  identical to integ run in the same session; geo.sh 11 steps: identical except the pixel counts cut by the sources'
  moving bar in 2 steps (integ differs from integ in the same lines); 0 protocol errors.
- Sink stress (`JITTER=1 LSFLAGS=noxulw tests/r157/sinkstress.sh`: lockstress with 132's sources in three of its windows,
  61500 operations, pointer and key jitter): 6/6 DONE, 0 protocol errors. (Before the probe ignored injected caption
  clicks: 5/6, the sixth sat in a system menu loop that a jitter click had opened; all five mutexes free.)
  `noxulw` leaves out UpdateLayeredWindow on another thread's window, the trigger of the win32u race 171, which hung 5 of
  32 earlier runs.
- Request sequences of simple cases (reviewer's seq.c, 40 cases, requests per interface.method): final tip vs integ: the
  cases that differ, differ between two integ runs in the same way (shm pools and extra flushes, cursor shape vs surface,
  lock vs confine depending on where the pointer was); show / hide / show again / owned popup / both role changes /
  GL parent show / destroy: equal counts.
- Lock-order debug build of the final tip (rapid 300, roleflip 600 gl, glhide 1000 5, sink stress x3, 132's probes, all
  with jitter): 154 reports, all `win_data -> win32u:display` (monitor functions, row 9), `win_data -> pointer / keyboard
  / text_input`, `seat -> data_device`. No `-> win32u:user`, `client_surfaces`, `window_surface`, nothing under
  pointer.mutex or source_mutex (`inst/157/r3/dbg/folded.txt`; unfixed: `inst/157/vm/dbg0/all.folded`).
- 170: `lockstress glhide 3000 5` 14/14 DONE (integ fatal 3/3); `glhide 3000` (hidden 0-2 ms) still fatal, see 170.
- No win32u change, so no host regress units. Inventor on Wayland not run.

## Weak spots / not covered
- 170's remaining case (hide and show within ~2 ms with a GL child on a GPU compositor) is still fatal; not a lock problem.
- Older than 157, now easier to fix with the serial but not done: a popup with the subsurface role keeps its wl_subsurface
  when its owner's surface is replaced by a role change (`wayland_surface_make_subsurface` only compares the owner HWND);
  the next place_above would be a protocol error.
- WAYLAND_ClipCursor (integ code) uses the wl_surface after releasing win_data and calls wayland_win_data_release() on a
  possibly NULL data.
- Row 7 (cursor info under pointer.mutex) was fixed by reading; with jitter the pointer is on the windows in the debug
  runs, which then show no user lock under pointer.mutex, but the old call was never caught at run time.
- "Display lock is a leaf" rests on reading lock_display_devices and a scan of the 31 locked regions in sysparams.c for
  direct window calls (none); helpers were not followed.
- Row 28: a flush can wait for the compositor with the user lock held; unchanged.
- winemac has the same pattern as the unfixed driver; untouched. winex11: 173.
- The debug patch only sees the locks it instruments (user, display, client surfaces, window surface, driver mutexes).
- 171 (win32u surface list race) hangs lockstress on a fast host unless `noxulw` is used; independent of this branch.
- One GNOME wl_xowner run (right after an unthrottled jitter batch) ran its probe ~12 s behind the script and failed 4 checks;
  two reruns in a fresh session passed. Taken as leftover input load, not proven.

## Tools and infra
- `tests/r157/`: lockstress.c (`N [SEED] [nogl] [noxulw]`, `glhide N [HIDE_MS]`), g-run.sh / g-utest.sh / batch.sh (vmwl
  guest; `JITTER=1`), jitter.py (`host` / `vm`), sinkstress.sh and r132.sh (host session; 132's sources and probes),
  lockorder-debug.patch + lockorder.py.
- vmwl/run.sh shares paths with one read-only virtiofsd each (no bwrap in the sandbox); `/host/.mounts` remounts after a
  guest reboot; leaving a KDE session needs a guest reboot; KDE autolock is off in the guest; gdb attach in the guest needs
  `kernel.yama.ptrace_scope=0` (g-run.sh sets it). After a guest boot under host load gnome-shell can miss systemd's start
  timeout: check the socket (`vmwl/session.sh gnome` restarts the session).
- `tests/r132/{env,geo,clip,run2}.sh` take `R132_PREFIX` / `R132_OUT`. WAYLAND_DEBUG=1 cannot be used with them (the trace
  splits the handle line they parse).
- x/wayland.sh still needs its private D-Bus config: the stock session config fails in the sandbox with "Failed to query
  AppArmor policy: Read-only file system" (retested 2026-10-04 after the AppArmor change).
