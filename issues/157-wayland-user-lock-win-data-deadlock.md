# 157 winewayland: deadlock between win32u's user lock and the driver's win_data_mutex (two threads showing/hiding windows)
Status: in progress, paused before the fix (see State at pause) · Found in: review of fix/134 (inst/134-review, `rv rapid 300`) · predates fix/134 · **serious for real use**: any
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

## Open
Component: winewayland.drv (window.c, wayland_surface.c): read style/exstyle/text/layered attributes before taking
win_data_mutex, or have win32u not hold the user lock across the flush. Not Wayland-compositor specific.

## State at pause (2026-10-03, machine reboot)
Nothing is fixed yet. Branch `fix/157` = `fix/134` tip 5d59ae5fddf, worktree `wt/157` clean, no commits.

### Established
- Reproduced in the VM on GNOME 50 with the unfixed fix/134 build (`wt/134-build`): `rv rapid 300` hung 4 of 7 and 2 of 4 runs
  (60 s watchdog, `tests/r157/g-run.sh`). Stacks (`inst/157/vm/gnome-134-rapid300-hang-{2,4}.txt`) are the pair above: one thread in
  `update_surface_region` -> flush -> `wayland_win_data_get`, the other in `WAYLAND_WindowPosChanged` ->
  `wayland_win_data_get_config` -> `NtUserGetWindowLongW` -> `user_lock`; the event thread blocked on win_data_mutex.
  KDE and sway not run yet.
- `get_user_handle_ptr` takes the user lock for every handle, foreign windows too (lock, look up, unlock), so every
  `get_win_ptr` user counts: GetWindowLong, GW_OWNER, GA_PARENT/GA_ROOT, IsWindowVisible, InternalGetWindowText, GetIconInfo.
- Rule (decided, not implemented): win32u's locks come first, win_data_mutex last: user lock -> window surface lock ->
  win_data_mutex, and win32u's client `surfaces_lock` (window.c) -> win_data_mutex. Reasons: upstream a334c147f81 states it for
  this driver; the Wayland flush needs win_data (window contents and the wayland_surface live there), and win32u calls the flush
  with the window pointer held (`apply_window_pos` holds `win` across `update_surface_region`, upstream master still does).
  winex11 has the opposite order (NtUser* calls under its win_data lock everywhere; its flush never takes win_data, see
  notes/wine/window-surfaces.md), so it is no model here. winemac mixes both (surface.c flush takes win_data, window.c reads
  styles under it): same latent bug by reading, not tested.
- Debug tool, not for commit: `tests/r157/lockorder-debug.patch` (apply to wt/157) makes win32u report every acquisition of the
  user / display / client surfaces / window surface locks to the driver, which prints `LOCKORDER held -> acquired` with a
  backtrace when it holds one of its own mutexes (also driver lock pairs). `tests/r157/lockorder.py LOG...` symbolizes and folds
  them. Output of one unfixed rapid run: `inst/157/vm/dbg0/rapid300.log`.

### Call table so far ("seen" = reported by the debug build in `rv rapid 300`; "read" = by reading only)
Driver holds win_data_mutex and calls win32u:
| caller (under win_data_mutex) | callee | lock taken | |
|---|---|---|---|
| WAYLAND_WindowPosChanged -> wayland_win_data_create_wayland_surface | NtUserGetWindowLongW (GWL_EXSTYLE, GWL_STYLE) | user | seen |
| ... -> wayland_win_data_get_config | NtUserGetWindowLongW (GWL_STYLE) | user | seen, hang stacks |
| ... -> wayland_surface_make_toplevel | NtUserInternalGetWindowText | user | seen |
| ... -> reapply_cursor_clipping (foreground window) | NtUserGetClipCursor, NtUserClipCursor | display lock; user (get_present_rect, empty clip rect only); then pClipCursor -> pointer.mutex | display seen, rest read |
| ... role change -> update_client_surfaces | win32u | client surfaces_lock, then user (NtUserGetAncestor, and the update callback's NtUserIsWindowVisible) | read |
| wp_fractional_scale_handle_scale (event thread) | update_client_surfaces | same | read |
| set_window_surface_contents, wayland_client_surface_present -> wayland_surface_reconfigure -> wayland_surface_get_rect_in_monitor | NtUserMonitorFromRect, NtUserGetMonitorInfo | display lock (user lock: not checked) | read |
| wayland_surface_create | NtUserGetLayeredWindowAttributes | none (server request) | read |
| 134: exported_handle, wayland_surface_clear_role, wayland_surface_import_parent | NtUserSetProp, NtFindAtom, NtUserRemoveProp, NtUserGetProp, NtQueryInformationAtom | none (server requests; GetProp reads the shared window object, `get_shared_window` still to be read) | read; no report in the rapid run, which exports on every map |
| keyboard_handle_enter | NtUserPostMessage | none | read |
| WAYLAND_SetWindowIcons | NtGdi* only | GDI | read |
| WAYLAND_CreateWindowSurface | window_surface_release, window_surface_create | not checked | |

Other driver locks:
| held | callee | lock taken | |
|---|---|---|---|
| pointer.mutex (wayland_set_cursor -> wayland_pointer_set_cursor_shape / wayland_pointer_update_cursor_buffer) | NtUserGetIconInfo | user | read; a user -> pointer.mutex path not found yet (pSetCursor, pDestroyWindow call sites unchecked) |
| text_input.mutex (WAYLAND_SetIMECompositionRect) | wayland_win_data_get | win_data, while wayland_surface_destroy under win_data takes text_input.mutex: driver-internal inversion | read; no user lock involved, draft issue or separate fix to decide |
| text_input.mutex (text_input_leave/done) | NtUserMessageCall(WINE_IME_POST_UPDATE) | not checked | |
| seat.mutex | data_device.mutex | driver only | seen |
| keyboard.mutex, xkb_layouts_mutex | no win32u calls | | read |
| output_mutex, data_device.mutex regions | | not read | |

win32u holds a lock and calls the driver:
| held | path | driver lock |
|---|---|---|
| user (window pointer) | apply_window_pos -> update_surface_region -> window_surface_set_shape / set_clip -> flush | window surface lock, win_data_mutex (seen) |
| client surfaces_lock | client_surface_present / update_client_surfaces / detach / release -> present, update, detach callbacks | win_data_mutex (read) |
Called without the user lock (read): pWindowPosChanging, pWindowPosChanged, pSetWindowStyle, pSetWindowText, pSetWindowIcons.
Not checked yet: pDestroyWindow, pSetCursor, pClipCursor, pSetLayeredWindowAttributes, pUpdateLayeredWindow, pCreateWindowSurface.

### Planned fix (design, nothing written)
- `WAYLAND_WindowPosChanged`: read style, ex style and window text before `wayland_win_data_get`, next to the existing
  `is_window_managed` / owner lookup (same pattern as a334c147f81), pass them down; move `reapply_cursor_clipping` after the
  release. Staleness: the values become one snapshot with `managed` / owner / rects, which are already taken before the lock;
  the open case is a title set by another thread between the read and the lock (needs a foreign-thread DefWindowProc(WM_SETTEXT)
  or UpdateLayeredWindow): state it, or add a serial.
- Role change and fractional scale: `update_client_surfaces` must run with win_data released. Two options: release the lock
  around it (`data` has to be looked up again), or drop the call and let `wayland_client_surface_attach` notice that its
  subsurface belongs to an older wayland_surface (a counter, not the pointer: address reuse).
- pointer.mutex: get the icon info before taking it.

### Built / environment
- `wt/157-build`: full Wayland-capable build of 5d59ae5fddf (configure line as for wt/134-build). Its win32u.so and
  winewayland.so were last built WITH the debug patch; the sources are back to clean, so
  `make -j40 dlls/win32u/all dlls/winewayland.drv/all` rebuilds them (gdi_driver.h changed back: other modules rebuild too).
- VM: powered off. Start with `VMWL_BIND="wt/134-build wt/134/nls wt/134/fonts wt/157-build wt/157/nls wt/157/fonts inst/157
  inst/134-review tools/gdb" vmwl/run.sh`. Guest prefixes `~/wp-134-build`, `~/wp-157-build`, logs `~/r157/`.
  `g-run.sh` sets `kernel.yama.ptrace_scope=0` in the guest (gdb attach), lost at guest reboot.
- `tests/r157/lockstress.c` (built as `inst/157/lockstress.exe`): my multi-thread stress (text, style, owner, minimize, layered,
  child <-> toplevel, managed <-> unmanaged with a GL child, short-lived threads). Written and compiled, NEVER RUN: untested.

### Remaining, in order
1. Run lockstress on the debug build (unfixed) for the inventory of the other callbacks; finish the unchecked rows above.
2. Write the fix as small commits on fix/157, rebuild, debug build again: zero `-> win32u:user` / `client_surfaces` reports.
3. Verify: rapid 300 / 3000 (20 runs GNOME, 10 KDE, 10 sway) vs unfixed on GNOME; lockstress; `vmwl/wl_xowner.sh` GNOME + KDE with
   WAYLAND_DEBUG; user32:win and user32:msg under Wayland before/after (2 runs each); regress units on the host if win32u changes.
4. Issue file (final table, numbers), notes/wine/wayland.md (lock order rule), final report; VM off.
