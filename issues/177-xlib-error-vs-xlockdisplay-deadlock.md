# 177 winex11: process hangs in Xlib when an X error is read by one thread while another waits for a reply with the display locked (X11DRV_expect_error)
Status: fixed, on integ (b1e7b97cfed), reviewed (`winex11: Handle expected and ignored X errors without the display lock.`;
review data inst/177-review/; not run on Inventor) · Found in: review of 171 · upstream winex11 +
a libX11 bug (1.8.13 and git master), report text + reproducer + patch ready, not filed:
[attachments/177-libx11-bug-report.md](attachments/177-libx11-bug-report.md)

## Symptom
All threads that touch a display stop (surface flushes, clipping, window data users behind them). No X error is
printed, the process doesn't die. Always the same pair of stacks:
```
thread A: ... -> XSync / any request in synchronous mode -> _XReply -> handle_response -> handle_error -> _XError
          -> _XUserLockDisplay -> _XDisplayLockWait -> pthread_cond_wait          (reader: has read an X error)
thread B: X11DRV_GetImage (XGetImage) or create_shm_image (XSync), between X11DRV_expect_error and
          X11DRV_check_error -> _XReply -> pthread_cond_wait                      (holder of the display's user lock)
others:   waiting for the display
```
Readers seen in 44 dumps of integ cffd27540ee (this round): `destroy_whole_window`'s XSync (12, all of the hangs
without synchronous mode), and in synchronous mode the XSync after XShmPutImage (26), XSetClipRectangles (3),
XCreateGC (2), XSetGraphicsExposures (1). Holders: X11DRV_GetImage 21, create_shm_image 23.

## Cause (libX11; verified with a plain Xlib program and a patched library)
- `_XReply` hands out replies in request order: B waits on `dpy->xcb->reply_notify` until A, whose request is
  first in the queue, has processed its reply and every event and error that precedes it. A was already waiting
  for its reply when B called `XLockDisplay`; `_XReply` re-locks the display "ignoring user locks" for exactly that
  case. But for an error among the preceding responses A calls `_XError`, and `_XError` takes the user lock around
  the error handler call (since X11R6; it runs the handler with the display mutex released). A waits for B, B for A.
- `XLockDisplay` (src/LockDis.c) still has the code that was meant to let "the threads in the reply queue all get
  out" before it returns, but it tests `dpy->lock->reply_awaiters`, which nothing sets since the XCB transport
  (2008-2010): dead code. So this is a library bug, present in 1.8.13 and in git master (6d4432b1: `_XError`,
  `_XReply`, `handle_error`, locking.c, LockDis.c are the same; XlibInt.c and xcb_io.c differ in unrelated hunks; the
  patch applies). History: fd85aca "Ignore user locks after sleeping in _XReply and _XReadEvents" (2011-03-14)
  fixed this very pair for the plain lock wait; 83e1ba5 "Call _XErrorFunction without holding the Display lock"
  (a day later) put the user lock into `_XError` and reopened it for errors.
- Measured: `issues/attachments/177-libx11-reproducer.c` (reader thread: failing request + XSync; holder thread:
  XLockDisplay + XSync + XUnlockDisplay) hangs after 0 to 7 round trips in 8 of 8 runs (distribution package and an
  own build of 1.8.13; stacks as above, `inst/177/out/xerrlock-plain-stacks.txt`). With the patch
  ([attachments/177-libx11-xerror-user-lock.patch](attachments/177-libx11-xerror-user-lock.patch): `_XError` does
  not wait for a user lock that another thread holds, `_XLockDisplay` checks `error_threads` before the user lock
  wait) 5 of 5 runs finish (280000 round trips each), and unmodified Wine (build-next) on the patched library
  passes the stress that hangs every time: `+synchronous` gdistress 4 of 4 without a WM, 20 of 20 under openbox
  (integ on the distribution's library: 0 of 24).
- My reading, not measured: the same pair with Xlib's own sequence sync as the holder's reply wait (182), and with
  an error that kills the process (see "Not closed").

## Who holds the display's user lock across a reply wait (survey)
| holder | display | reply wait |
|---|---|---|
| X11DRV_GetImage (bitblt.c), X11DRV_ExtFloodFill (graphics.c) | gdi_display | XGetImage: every BitBlt / GetPixel / flood fill from a DC drawn by the X11 driver |
| create_shm_image (bitblt.c) | gdi_display | XShmAttach + XSync: every new window surface image |
| GLX: create_glxcontext, glXSwapIntervalEXT (opengl.c) | gdi_display | XSync, and whatever the GL library does inside |
| xvidmode.c (6 regions), xrandr.c XRRQueryVersion | gdi_display | extension queries, at init and on mode / gamma changes |
| get_host_window (window.c) | own thread display | XGetWindowAttributes, XQueryTree |
| clipboard.c XGetAtomNames | clipboard thread display | XGetAtomNames |
| wintab.c (XOpenDevice, XGetDeviceButtonMapping, the device loop of the context) | own thread display | XInput queries |
| lock_xid_alloc (182) | gdi_display, a thread display from another thread | none of ours; Xlib's sequence sync in XUnlockDisplay (kept away by 182's pre-sync), Xcursor's first calls and XcursorLibraryLoadCursor (round trips inside the lock) |
| SPI_SETSCREENSAVEACTIVE (x11drv_main.c) | gdi_display | XGetScreenSaver inside XLockDisplay |
| BRUSH_DitherColor (brush.c), set_window_cursor (mouse.c) | gdi_display | none |
| GL libraries under their own XLockDisplay | gdi_display | not ours to see |
All X11DRV_expect_error / X11DRV_check_error users are in winex11 (nothing else calls them, nor XLockDisplay).

## Fix (b1e7b97cfed, x11drv_main.c only)
The user lock in X11DRV_expect_error is what attributes an error to the expecting thread (no other thread can send
a request on that display between expect and check, so every error with a serial from `err_serial` on is ours; the
handler runs in whatever thread reads it). That stays. What changes is the reader's side: it no longer needs the
user lock for the errors Wine handles.
- Every display winex11 opens (gdi_display, each thread's display) gets a private extension record (`XAddExtension`)
  with an error hook (`XESetError`, the documented Xlib extension interface, Xlibint.h): `reply_error_handler`.
  `_XReply` calls these hooks for every error it reads, in the reading thread, with the display mutex held, before
  `_XError` and its user lock. The hook builds the XErrorEvent and runs the same check as the error handler (the
  shared `handle_error()`: expected by the current X11DRV_expect_error region, or ignored); if so the error is
  consumed there. Anything else goes on to `_XError` and the error handler as before.
- The hook returns 0 while the display has Xlib async handlers: XGetWindowAttributes, XGetAtomNames, XInternAtoms,
  XLoadQueryFont, XReconfigureWMWindow need to see the errors of their own requests in `_XError`. Without that a
  consumed BadWindow in get_host_window leaves the BadDrawable of the following GetGeometry unsuppressed = a fatal X
  error (checked with a plain Xlib program; the first version of the commit had this bug).
- X11DRV_expect_error / X11DRV_check_error set and clear their statics with the display mutex held (`LockDisplay` /
  `UnlockDisplay`), because the hook reads them in other threads without the user lock.
- The hook costs nothing per request and covers every holder in the table, also the ones outside
  X11DRV_expect_error and the foreign ones, for the errors Wine expects or ignores.

Rejected:
- Async handler of our own (`dpy->async_handlers`; my note below and the first thing built,
  `inst/177/v0-async-handler.patch`). Permanent: Xlib then keeps a record per request sent and asks xcb about each
  at the next reply: 80 -> 450 ns per request, 11 ms bursts at a sync (plain Xlib, `xerrlock bench`). Installed only
  inside X11DRV_expect_error: the cost goes away with a flush before, but a handler added after XLockDisplay
  misses the reader that has read its error in between (`xerrlock hang 8 tlate` hangs), and added before it still
  misses a reader that already waits for the user lock because a third thread held it for an allocation and loses
  the race for it to us (by reading; needs an error read during a lock_xid_alloc).
- Not holding the user lock across the reply wait (send under the lock, sync outside, serial range check): works
  for XShmAttach + XSync, not for requests whose send and reply are one Xlib call (XGetImage, every query) nor for
  what GL libraries do inside; without the lock errors of other threads' requests fall into the range and
  callbacks that accept everything (xshm, GLX, XVidMode) would take them for theirs.
- Checked xcb requests: no error handler involved at all, but every region would have to be rewritten on xcb, and
  the GLX / XInput / XVidMode ones can't be.
- A private display for the readers or for the expecting requests: XGetImage on another connection needs a sync of
  gdi_display first (an extra round trip per BitBlt), XShm / GLX resources belong to gdi_display.
- A driver mutex around every round trip on gdi_display: a new rule for every call site, cannot cover the
  libraries, and in synchronous mode every request is one.

## Verification (wt/177-build at b1e7b97cfed, base = build-next cffd27540ee; Xvfb :1770-:1781 of inst/177/x.sh,
8 to 10 displays in parallel; hang dumps sorted with inst/182/classify.py)
| probe | base | fix |
|---|---|---|
| plain Xlib, `xerrlock hang SECS MODE` | plain: HANG 7 of 7 | ext (the hook): done 3 of 3, 290000-470000 reader syncs, all errors in the hook |
| gdistress 20 6 SEED, openbox, with recreation, alternating | 66 of 80; 10 x 177, 4 x 191 | 79 of 80; 1 x 191. + 60 of 60 fix only |
| the same without a WM | 80 of 80 | 80 of 80 |
| the same with `+synchronous`, no WM | 0 of 12, all 177 | 12 of 12 |
| the same with `+synchronous`, openbox | 0 of 12, all 177 | 19 of 32; 13 x 191 (see below) |
| `visual_race.exe 12 text` with `+synchronous` | 0 of 8, all 177 | 8 of 8 |
| `iconrace.exe dcchurn 10 N`, N = 2, 4, 8 (182) | | 60 of 60, 0 assertions |
| `tests/r177/expect.exe 4 5` (BitBlt over the screen corner: XGetImage BadMatch expected) | 74002 rounds, 74002 expected errors, 0 mismatches | 74291, 74291, 0 |
| 173's probes (inst/177/p173.sh: iconlock, visual_race, visual_race text, flushpost, uistress, iconrace draw / wdraw / cursor, lockstress 15000 nogl), no WM / openbox | | 23 of 23 / 23 of 23 |
0 X errors and 0 `_XAllocID` assertions in all of them. No fix run has a thread in `_XError`.
- The 13 hangs of the fix in synchronous mode under openbox are [191](191-xic-destroyed-from-other-thread.md): owner
  thread in XCheckIfEvent on its own display mutex, another thread in destroy_whole_window's XFlush. The 7 runs
  repeated with `+seh` each have exactly one `handle_syscall_fault` in libX11's XFilterEvent (+0xe3 five times, +0x88
  twice). Base never gets that far in this mode (177 first). It is not an effect of the fix: the fix on an own
  build of the same libX11 source (unpatched, inst/182/x11-build) 10 of 10, on the patched one 10 of 10, base on the
  patched one 20 of 20; only the distribution's binary faults on the freed filter node.
- Units, both arches (`tools/regress.sh unit`, inst/177/out/ut.txt): gdi32 dc (fail 3 / 3) bitmap clipping, user32 win
  (fail 4 / 4) msg (fail 1 / 1) input cursoricon dce clipboard, opengl32 opengl: all 20 lines equal
  deps/regress/d8e4d0f72d22...-h26 (status, failures, todo, skip). Against deps/regress/cffd27540ee85...-h26 (the
  real base) user32:win differs (base there: i386 fail 1, x86_64 pass): the unit is flaky, 3 more runs per arch give
  base 3 of 6 and fix 4 of 6 with the same 4 failures (win.c:11003 / 11004, mouse input), the others pass.
- Cost, one thread, app / Xvfb / wineserver pinned to a CPU each, 6 alternating rounds of 2 s (inst/177/cost.sh,
  tests/r177/cost.exe; us per call, min / median):
  | | base | fix |
  |---|---|---|
  | GetPixel (screen DC) | 16.09 / 17.04 | 14.26 / 16.18 |
  | SetPixel + GetPixel | 17.43 / 18.61 | 18.26 / 18.55 |
  | 16 lines + GetPixel | 24.87 / 24.99 | 24.91 / 25.07 |
  | 256 lines + GetPixel | 115.54 / 116.72 | 118.19 / 119.68 |
  | BitBlt 16x16 screen -> memory | 18.97 / 19.08 | 18.26 / 19.35 |
  | BitBlt 256x256 | 71.85 / 73.56 | 72.64 / 73.25 |
  | resize with a new window surface | 1107.76 / 1133.20 | 1132.89 / 1168.82 |
  Differences are within the run-to-run spread (up to 3 %). The fix adds two uncontended display mutex round trips
  per X11DRV_expect_error region and nothing per request (plain Xlib: `xerrlock bench`, 82.0 vs 82.6 ns per
  request with and without the hook). Not measured: bystander latency under contention (no lock was added that a
  drawing operation takes).
- user32:win is run-dependent, not build-dependent (review: x86_64 win.c:11003 / 11004 come and go on both builds).
- NVIDIA (review, :101; inst/177-review/out/RESULT-gpu*.txt): GLX stress (UseEGL=N) base 6 of 11 done, the hangs are
  177 (holder create_shm_image, reader X11DRV_ThreadDetach's XSync); fix 11 of 11. What the NVIDIA and Mesa
  libraries do on winex11's displays: notes/wine/xlib-locking.md.
- Not run: Inventor (licence seat elsewhere). The lock-order debug build was not rerun: the fix adds no mutex and
  takes none inside the hook.

## Not closed
- An error that Wine neither expects nor ignores still makes the reading thread wait for the user lock, as on
  base. That is an error that ends the process (X error message, exit), or one that another library waits for with
  a temporary error handler of its own (Mesa's software GLX around XShmAttach). If a holder waits for a reply at
  that moment the process hangs silently. By reading; not provoked. The libX11 patch closes it.
- The hook stands down while ANY thread has an async handler on the display, the lock holder itself included. So
  a holder that calls XGetWindowAttributes / XGetAtomNames / XInternAtoms / XLoadQueryFont / XReconfigureWMWindow
  with the display locked is still half of the pair: plain Xlib, holder XLockDisplay + XGetWindowAttributes +
  XQueryTree (what get_host_window does), reader BadDrawable + XSync hangs with the hook logic (review:
  `inst/177-review/rv177 4 gwa sync`, also `atoms sync`; `geom sync` runs). In winex11 this is not reachable today
  only because of 173's rule: get_host_window( create ) runs with the window data locked, and every request that
  another thread makes on a thread's display is made with the window data locked too, so that holder and a foreign
  reader exclude each other through win_data_mutex (read from the code, not provoked). A cross-thread round trip on
  a thread display outside the window data would reopen it. On gdi_display async handlers exist only at start-up
  (measured in the review: libXrender 1 round trip, libGLX / glvnd 2 per process, besides our XInternAtoms).
- Errors read on the event path don't go through the hook: XPending / XCheckIfEvent / XNextEvent, and also every
  XFlush, which polls and handles errors that way. gdi_display has such readers: every XFlush( gdi_display ), and
  init.c blocks in XWindowEvent( gdi_display ). No 177 cycle follows (such a reader is not in the reply queue; plain
  Xlib `sync flush`, `sync next`, `gwa next` of rv177 run). An upstream oddity stays: an expected error read that
  way by another thread is missed by the expecting one, e.g. while a thread blocks in that XWindowEvent an XShmAttach
  error that create_shm_image expects would be missed and then be fatal (derived, not provoked).
- Considered in the review and not taken (F2): the err_callback statics are protected by the expecting display's
  mutex only; handle_error() for an error on another display reads them with no common lock, as upstream always
  did: a spurious "expected error" in a window of a few instructions. `inst/177-review/v2-leaf-mutex.patch` (a leaf
  mutex instead of LockDisplay / UnlockDisplay, +17 -11, tested equal) is the upgrade if this ever shows or if
  upstream objects to LockDisplay in winex11; it also removes LockDisplay's side effects (XID refill, sequence sync)
  inside X11DRV_expect_error.
- Displays that winex11 doesn't open (connections of GL / media libraries) have no hook.
- A difference in behaviour, on purpose: the hook also acts while another library has temporarily put its own Xlib
  error handler in place (Mesa's software GLX does around XShmAttach, libXrender around its pixmap depth check), so
  errors Wine expects or ignores no longer reach that handler when they are read inside `_XReply`. The two known
  ones wait for BadAccess / BadValue, which Wine doesn't ignore (for Mesa an improvement: a stray BadDrawable of a
  winex11 XShmPutImage no longer reaches its handler and switches XShm off). Checking `_XErrorFunction` in the hook would keep
  the old behaviour and the hang with it for that time.
- 191 is now the hang that is left in these stresses (1 of 140 under openbox, 13 of 32 in synchronous mode under
  openbox with the distribution's libX11). 188 (flag `x`) and 190 were not seen in this round (190: the probe starts
  its threads one by one).
- Upstream acceptability rests on `#include <X11/Xlibint.h>` (for XESetError, LockDisplay and
  `display->async_handlers`): nothing else in Wine includes it. XESetError / XAddExtension are documented (Xlib
  manual, appendix C); looking at `async_handlers` is not.

## Tools
`tests/r177/`: `xerrlock.c` (plain Xlib: `hang SECS plain|ext|temp|tnofl|tlate|perm`, `bench SECS MODE [SYNC_EVERY]`),
`cost.c`, `expect.c`. `inst/177/`: x.sh (Xvfb :1770-:1777, 4 without WM, 4 with openbox), run.sh (watchdog + gdb dump;
`X11LIB=DIR` = another libX11), ab.sh (alternating base / fix; `BUILDS`, `EXE`, `ARGS`, `SEED0`), dcchurn.sh, p173.sh,
cost.sh, ut.sh; `src/libX11-fixed` + `x11-build-fixed` (1.8.13 with the patch), `v0-async-handler.patch` (first
variant). Results in inst/177/out (b1 = first batch with the commit before the async deferral, b2 = the main batch).

## Earlier observations
- 171's review: `inst/171-review/rv.exe stress 20 rgn gl`, four instances in parallel: 6 of 80 on fix/171, about 30 %
  when surface changes are not serialized behind flush passes; reader X11DRV_ThreadDetach's XSync( gdi_display ),
  holder create_shm_image (`inst/171-review/out/h-fix-3-hang.txt`, ...).
- 173: `WINEDEBUG=+synchronous tests/r173/visual_race.exe 12 text` 2 of 2; the failing request is the surface flush
  into an X window that another thread has replaced (set_window_visual).
- 182: reachable without synchronous mode on plain integ (gdistress under openbox 4 of 60, holder X11DRV_GetImage);
  27 of 34 hang dumps of fix/182 are this one.
