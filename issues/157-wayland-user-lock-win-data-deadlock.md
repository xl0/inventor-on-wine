# 157 winewayland: deadlock between win32u's user lock and the driver's win_data_mutex (two threads showing/hiding windows)
Status: draft · Found in: review of fix/134 (inst/134-review, `rv rapid 300`) · predates fix/134 · **serious for real use**: any
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
