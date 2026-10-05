# 176 win32u: a window DC is brought up to date while a window surface is locked (surface lock -> user lock, -> winex11 window data)
Status: draft (found in the review of 171, not worked on) · upstream code · win32u, winex11 part matters for 173's rule

## Symptom
Deadlocks with a window surface lock in the cycle. Two places take the user lock (and, in winex11, block on
`win_data_mutex` through `pGetDC`) with a surface locked:
- `NtUserUpdateLayeredWindow` (window.c ~2891-2916): `window_surface_lock( surface )`, then
  `NtGdiAlphaBlend( hdc, ..., hdc_src, ... )` -> `update_dc( dcSrc )` -> `update_visible_region` -> `get_win_ptr` /
  `X11DRV_GetDC` -> `get_win_data`. Needs a window DC as the source (`GetDC( hwnd )`) whose visible region is dirty
  (the window, its parent or a child moved since the DC was fetched).
- `move_window_bits_surface` (dce.c ~1742): `window_surface_lock( old_surface )`, then
  `NtGdiSetDIBitsToDeviceInternal( hdc, ... )` -> `update_dc` -> the same. Every resize that keeps valid bits and
  gets a new surface goes through it; the DC is dirty often (seen in plain user32 tests).
Both violate "nothing under a surface lock takes the user lock" (win32u itself flushes with the window pointer held:
`update_surface_region`) and 173's "nothing under a surface lock blocks on win_data".

## Evidence (lock-order debug build = 173's patch + call chains for user / surface pairs, inst/171-review/out/dbg-all.folded)
```
surface->mutex -> user_mutex      get_win_ptr < update_visible_region < NtGdiAlphaBlend < NtUserUpdateLayeredWindow
surface->mutex -> win_data_mutex  get_win_data < X11DRV_GetDC < update_visible_region < NtGdiAlphaBlend < NtUserUpdateLayeredWindow
surface->mutex -> user_mutex      get_win_ptr < update_visible_region < NtGdiSetDIBitsToDeviceInternal < move_window_bits_surface < apply_window_pos
surface->mutex -> win_data_mutex  get_win_data < X11DRV_GetDC < update_visible_region < NtGdiSetDIBitsToDeviceInternal < move_window_bits_surface
win_data_mutex -> surface->mutex  window_surface_flush / window_surface_set_shape < X11DRV_WindowPosChanged < apply_window_pos
```
Hangs seen (stacks in inst/171-review/out/):
- `fixa-swdc-hold-hang.txt` (fix/171 + flush outside the list lock, so no list lock involved): owner thread in
  `apply_window_pos -> update_surface_region -> window_surface_set_shape` (user lock, wants the surface) against another
  thread's `UpdateLayeredWindow( same window, window DC )` (surface, wants the user lock). `rv.exe stress 20 wdc`, every run.
- With fix/171 as it is the same edge closes a cycle through dce.c's `surfaces_lock` without two threads on one
  window: see 171's review (`rv.exe wdc 12`: 10 of 10 hang, integ 0 of 10; `h-fix-ob-8-hang.txt`: four threads,
  user -> surfaces_lock -> surface -> win_data -> user).
- Not seen, by reading: `X11DRV_WindowPosChanged` flushes the window's surface under win_data while another thread sits
  in `move_window_bits_surface` with that surface locked and waits for win_data (two threads on one window:
  cross-thread UpdateLayeredWindow re-installs the surface the owner copies from).

## Repro
`inst/171-review/rv.c` (build line in the file), `inst/171-review/run.sh BUILD DISPLAY TAG TIMEOUT inst/171-review/rv.exe stress 20 wdc`.

## Direction (not tried)
Bring the DC up to date before the surface is locked is not enough (it can get dirty again). Either blend into the
surface without the explicit lock held across the source read (read the source bits first, then lock and blend), or
make `update_dc` of the source impossible under the lock. For `move_window_bits_surface`: take the old surface's bits
pointer under its lock, draw after unlocking (the caller holds a reference and the old surface is no longer the
window's surface).
