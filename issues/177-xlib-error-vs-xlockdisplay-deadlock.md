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
