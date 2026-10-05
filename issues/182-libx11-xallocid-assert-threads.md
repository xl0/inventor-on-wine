# 182 Process aborts with `_XAllocID: Assertion 'ret != inval_id' failed` when several threads create DCs (libX11 race on the shared gdi_display)
Status: draft, root-caused (libX11 1.8.13 bug, reproduced without Wine), not fixed · Found in: review of 173
(`inst/173-review/iconrace.exe dcchurn 10 4`) · upstream winex11 + upstream libX11 (unfixed on libX11 master) · **can hit real
apps**: any process with two threads that create memory DCs, pixmaps, GCs, pictures or windows at a high rate

## Symptom
```
iconrace.exe: ../../src/xcb_io.c:635: _XAllocID: Assertion `ret != inval_id' failed.
```
then SIGABRT (Wine prints "Exception frame is not in stack limits"). `iconrace.exe dcchurn 10 4` = 4 threads looping
`CreateCompatibleDC( 0 ); DeleteDC()`: integ (build/, b5d75449ffe) 4 of 4 runs within seconds (reviewer 4 of 4, mine 2 of 2
plus the gdb run); with 2 threads it survived a 20 s run once.

## Where (gdb at __assert_fail, `inst/173/out/dcchurn-gdb.txt`)
```
asserting thread: _XAllocID <- XCreateGC <- create_x11_physdev (winex11.drv/init.c:91) <- X11DRV_CreateCompatibleDC <- NtGdiCreateCompatibleDC
other threads:    pthread_cond_wait <- (libX11 display lock wait) <- XCreateGC / XSetGraphicsExposures <- create_x11_physdev
```
Every memory DC gets an X GC on the process-wide `gdi_display` (`XCreateGC` + `XSetGraphicsExposures` in
create_x11_physdev, `XFreeGC` on delete): one XID allocation and three requests without a reply per DC, from whichever
thread creates the DC. Other XID allocations on gdi_display from arbitrary threads: window DC GCs, pixmaps and GCs of
window surfaces (bitblt.c), XRender pictures and glyph sets (xrender.c), client windows (window.c), cursors.

## Cause: libX11, not Wine
Reproduced with plain Xlib, no Wine: `tests/r182/xallocid.c` (N threads `XCreateGC` / `XFreeGC` on one Display after
`XInitThreads`), libX11 1.8.13-1 (Ubuntu 26.04), Xvfb: 4 threads 3 of 3 abort, 2 threads 3 of 3 abort, 1 thread 0 of 3.
Reading libX11-1.8.13 (unchanged on its master):
- `_XAllocID` (xcb_io.c) hands out `dpy->xcb->next_xid`, sets it to `inval_id` and asserts it wasn't `inval_id`. The
  refill is `_XIDHandler`, called at the start of every `LockDisplay`: `_XLockDisplay` (locking.c) does
  `_XIDHandler( dpy ); _XSeqSyncFunction( dpy );` in that order.
- `_XSeqSyncFunction` (XlibInt.c): once 65535 - BUFSIZE/4 requests were sent since the last reply was read, it sends
  GetInputFocus and calls `_XReply`, and `_XReply` **unlocks the display** around `xcb_wait_for_reply64`.
- So: thread A locks the display (next_xid valid), the sequence sync fires and A waits for the reply with the display
  unlocked; thread B locks (its `_XIDHandler` sees a valid id), allocates the id (`next_xid = inval_id`) and unlocks;
  A gets its reply, returns from `LockDisplay` into `XCreateGC` and calls `XAllocID`: assertion. Nothing refills
  between the sync and the caller's allocation.
- It needs a stream of requests without replies (a sync every ~65000 requests) and a second thread allocating an XID in the
  round trip's window, so the rate grows with the request rate on the shared display: DC churn hits it in seconds,
  ordinary multi-threaded GDI use rarely. libX11 fixed two neighbours before (cc19618 "Fix XAllocID race: hold the user
  display lock until we have a new XID", 2af660c "Two threads can request sequence sync and XID fetch simultaneously").

## Directions
- libX11: in `_XLockDisplay` run `_XSeqSyncFunction` first and `_XIDHandler` after it (the id refill takes the user-level
  display lock, so ordinary lockers wait behind it), or re-run `_XIDHandler` after the sync. Report upstream with
  `tests/r182/xallocid.c`.
- winex11 workaround, not tried in Wine: a user-level lock around the allocating call keeps other threads out during the
  sync's unlocked wait. In the plain Xlib reproducer `XLockDisplay` / `XUnlockDisplay` around `XCreateGC` gives 3 of 3
  clean 10 s runs with 4 threads (`xallocid 4 10 lock`). It would have to cover every XID allocation on gdi_display (the
  list above), and `XLockDisplay` on gdi_display interacts with X11DRV_expect_error and draft 177: weigh before doing it.
  Cheaper for the worst offender: memory DCs don't need their own GC until something draws through the X11 driver.
- 173 note: commit 1 of fix/173 draws icons on *window* DCs from private bitmap copies, i.e. six short-lived memory DCs
  per such draw; several threads drawing icons on window DCs therefore reach this sooner than before (not seen in
  `iconrace wdraw 6 4`, 4 threads, 6 s, with and without openbox).

## Repro commands
```
inst/173/x.sh start                                    # Xvfb :1410 ...
inst/173/run.sh integ 1414 dcchurn 60 inst/173-review/iconrace.exe dcchurn 10 4
eval "$(tools/sysroot.sh env)"; gcc -O2 -o tests/r182/xallocid tests/r182/xallocid.c -lX11 -lpthread
DISPLAY=:1414 tests/r182/xallocid 4 10          # aborts;  ... 4 10 lock: DONE
```
