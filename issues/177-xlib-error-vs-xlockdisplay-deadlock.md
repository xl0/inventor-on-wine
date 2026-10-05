# 177 winex11: process hangs in Xlib when an X error on gdi_display is read by one thread while another waits for a reply inside X11DRV_expect_error
Status: draft (found in the review of 171, not worked on) · upstream code (winex11 + libX11 1.8.13) · same family as 173's X error deaths, but a hang

## Symptom
All threads that touch gdi_display stop (surface flushes, clipping, window data users behind them). No X error is
printed, the process doesn't die. Seen with `inst/171-review/rv.exe stress 20 rgn gl` (windows created / destroyed, threads
exiting with windows, cross-thread UpdateLayeredWindow) when four instances run in parallel (`inst/171-review/par.sh`),
rarely when one runs alone: fix/171 6 of 80 runs; fix/171 with the surface list unlocked during flushes 12 of 40, the
same skipping unregistered surfaces 5 of 20, a variant of 171 that never nests the list lock in the user lock 6 of 20.
So about 30 % when surface changes are not serialized behind flush passes (as fix/171 happens to do), and not a
matter of flushing into windows that are gone (skipping those changed nothing). integ can't be measured with this
stress (it dies of 171 first).

## Stacks (inst/171-review/out/h-fix-3-hang.txt, p2-fixa-7-hang.txt, p2-fix-ob-3-hang.txt, ...; always the same pair)
```
thread A: X11DRV_ThreadDetach (x11drv_main.c:707) -> XSync( gdi_display ) -> _XReply -> _XError -> pthread_cond_wait
thread B: create_shm_image (bitblt.c:1626, between X11DRV_expect_error and X11DRV_check_error) -> XSync -> _XReply -> waits
others:   XShmPutImage / XSetClipRectangles / ... waiting for the display
```
Reading: `X11DRV_expect_error` takes `XLockDisplay( gdi_display )` and keeps it until `X11DRV_check_error`. Thread A is
the one reading the connection (it waits for its own reply) when an error arrives; `_XError` takes the display's user
lock around the error handler call, which thread B holds, and B waits for a reply that only the reader (A) can
deliver. Which request failed is not known (an error of a surface flush into a destroyed window, which winex11 means
to ignore, or the XShmAttach error B expects: both would do).

## Notes
- Not caused by fix/171: the pair needs no win32u lock, and fix/171 shows it least.
- The debug build of 173 prints X errors in the handler; here the handler is never reached.
- Direction (not tried): don't hold XLockDisplay across the round trip (XSync first, then lock + check), or make
  ThreadDetach's sync not use gdi_display.

## With 173 applied (173 worker, 2026-10-04)
Still there, and reproducible on demand: `WINEDEBUG=+synchronous tests/r173/visual_race.exe 12 text` on the 173 debug
build (fix/173 + tests/r173/lockorder-debug.patch, no 171; Xvfb without a WM) hangs 2 of 2 (the reviewer of 173: 2 of 2).
Synchronous mode makes every request a round trip, which is what the pair needs. Stacks `inst/173/out/s177-1-hang.txt`:
```
thread A: flush_window_surfaces -> x11drv_surface_flush -> put_shm_image -> XShmPutImage -> XSync -> _XReply -> _XError -> pthread_cond_wait
thread B: X11DRV_CreateWindowSurface -> create_shm_image (between X11DRV_expect_error and X11DRV_check_error) -> XSync -> _XReply -> waits
```
So here the failing request is known: the flush into an X window that another thread has just replaced
(set_window_visual), an error winex11 means to ignore on gdi_display. 173's series removes two unlocked uses of a
replaced window (property reads, window text); this one is the surface flush, which still draws into the old window.


## Without synchronous mode, and what 182's fix does to it (182 worker, 2026-10-05)
- Reachable in normal mode on unmodified integ (b8f013d4fbb, wt/182-base-build): `tests/r182/gdistress.exe 20 6 SEED`
  under openbox on Xvfb (workers draw on the screen DC and read it back with BitBlt, and recreate a window of the main
  thread through UpdateLayeredWindow / SetLayeredWindowAttributes) hangs with this pair in 2 of 47 runs; the same on
  fix/182 in 2 of 45 (alternating runs, `inst/182/ab.sh`; in all fix/182 runs of that kind about 30 of 330).
  Stacks (`inst/182/out/ab-ob-base-1480-s7-hang.txt`, ...; `inst/182/classify.py` sorts hang dumps):
  ```
  thread A: destroy_whole_window (window.c "make sure XReparentWindow requests have completed") -> XSync( gdi_display ) -> _XReply -> _XError -> waits for the user lock
  thread B: X11DRV_GetImage (bitblt.c, between X11DRV_expect_error and X11DRV_check_error) -> XGetImage -> _XReply -> waits behind A
  ```
  So the holder need not be create_shm_image: every X11DRV_expect_error region with a reply in it will do
  (X11DRV_GetImage = any BitBlt / GetPixel from a DC drawn by the X11 driver), and the reader need not be a flush.
  With `WINEDEBUG=+synchronous` the same stress hangs 6 of 6 on both builds.
- 182's fix takes `XLockDisplay( gdi_display )` around every request that allocates a resource id. Those requests have
  no reply, and in synchronous mode the lock is not taken, so the fix adds no reply wait of its own under the lock.
  What remains is Xlib's own sequence sync when it falls into a locked `LockDisplay` (about 3 requests in 65000). None
  of the roughly 40 hang dumps of fix/182 has a holder inside lock_xid_alloc (all X11DRV_GetImage / create_shm_image).
- A way to close it at the root, not tried: Xlib calls `dpy->async_handlers` for every error before `_XError` takes
  the user lock (Xlibint.h `_XAsyncHandler`, the mechanism GDK uses for its async requests). A handler on each display
  that consumes the errors winex11 ignores anyway (ignore_error) would keep the reader out of the user lock; the
  expected errors of a X11DRV_expect_error region are only read by its own thread.
