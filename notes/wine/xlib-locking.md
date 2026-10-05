# Xlib locking as winex11 meets it — checked at libX11 1.8.13 and git master 6d4432b1 (`_XError`, `_XReply`, `handle_error`, locking.c, LockDis.c are the same there), wine-11.18-569

Sources: libX11 `src/locking.c`, `src/LockDis.c`, `src/XlibInt.c` (`_XError`), `src/xcb_io.c` (`_XReply`, `_XSend`).
A debug-info build is in `inst/182/x11-build` (`X11LIB=.../src/.libs inst/177/run.sh ...`).

- Two locks per Display: the display mutex (`LockDisplay` / `UnlockDisplay`, taken inside every Xlib call, not
  recursive) and the user lock (`XLockDisplay`: a level + owner thread + condition variable, recursive). `LockDisplay`
  waits while another thread holds the user lock. Neither is fair.
- A thread that waits for a reply (`_XReply`) has the display mutex released. Replies are handed out in request
  order: a second thread in `_XReply` waits on `dpy->xcb->reply_notify` until the first has processed its reply and
  every event / error that precedes it. After the wait `_XReply` re-locks with "ignore user locks": a thread that was
  already waiting when another one called `XLockDisplay` finishes its call under that foreign lock. `XLockDisplay`
  does not wait for such threads (its code for that tests `dpy->lock->reply_awaiters`, which nothing sets since the
  XCB transport).
- `_XError` takes the user lock around the error handler call (so the handler runs with the display mutex released
  and other threads kept out). That is the one place where such a leftover thread does wait for the foreign user
  lock, and if the holder waits for a reply itself the two wait for each other forever (177; plain Xlib repro
  `tests/r177/xerrlock.c`, report + patch in issues/attachments/177-libx11-*). Holders that wait for a reply: every
  `X11DRV_expect_error` region (XGetImage, XShmAttach + XSync, GLX context creation, XVidMode / XRandR queries), Xlib's
  own sequence sync inside `XUnlockDisplay` (182), Xcursor's first calls inside `lock_xid_alloc`,
  SPI_SETSCREENSAVEACTIVE, anything a GL library does under its own XLockDisplay.
- What Xlib does with an error it reads, in order: (1) only inside `_XReply`: the `XESetError` hooks of the display's
  extensions (`xcb_io.c handle_error`; newest extension first; with the display mutex held, before any user lock),
  (2) `_XError`: the display's async handlers (`dpy->async_handlers`), (3) the extension's wire-to-error hook, (4) user
  lock + the `XSetErrorHandler` handler. The hooks of (1) are called only from `_XReply` (for the errors that
  precede the reply and for the request's own), never from `_XEventsQueued` / `_XReadEvents`: errors read by
  `XPending` / `XCheckIfEvent` / `XNextEvent` / `XWindowEvent`, and by every `XFlush` (it polls), skip (1). Such a
  reader is not in the reply queue, so no 177 cycle; but an error that another thread expects is lost to it
  (gdi_display has these readers: every XFlush, and init.c blocks in XWindowEvent( gdi_display )).
- winex11 since 177: every display it opens gets a private extension record (`XAddExtension`) with an `XESetError`
  hook, `reply_error_handler` (x11drv_main.c), that runs the same expected / ignored check as the error handler. So a
  thread inside `_XReply` never reaches the user lock for an error that Wine expects or ignores, whoever holds it.
  The statics of `X11DRV_expect_error` are therefore written with the display mutex held (`LockDisplay` from
  Xlibint.h), since the hook reads them in other threads without the user lock.
  The hook must return 0 while the display has async handlers: `XGetWindowAttributes`, `XGetAtomNames`,
  `XInternAtoms`, `XLoadQueryFont`, `XReconfigureWMWindow` install one to see the errors of their own requests
  (consume the BadWindow of GetWindowAttributes in the hook and the BadDrawable of the following GetGeometry is no
  longer suppressed by Xlib: a fatal X error in get_host_window). The price: the hook is off while ANY thread has
  an async handler on the display, the lock holder included. A holder that makes one of these calls with the display
  locked (get_host_window: XGetWindowAttributes inside X11DRV_expect_error) and a reader on the same display still
  hang (plain Xlib: `inst/177-review/rv177 4 gwa sync`). winex11 is safe only through 173's rule: get_host_window
  ( create ) and every cross-thread request on a thread's display run with the window data locked, so the two
  exclude each other. Don't add a cross-thread round trip on a thread display outside the window data. On
  gdi_display async handlers exist only at start-up (XInternAtoms, libXrender 1 round trip, libGLX / glvnd 2).
- Not usable for this: an async handler of our own. While `dpy->async_handlers` is non-NULL `_XSend` keeps a record
  per request sent and the next `_XReply` asks xcb about each (plain Xlib: 80 -> 450 ns per request with a
  permanent handler, 11 ms bursts at a sync). Installed only inside X11DRV_expect_error it comes too late: added
  after XLockDisplay it misses a thread that reads its error in between (measured, `xerrlock hang 8 tlate`), added
  before it still misses one that already waits for the user lock of a third thread (by reading).
- Errors that still take the user lock in a thread that waits for a reply: the ones Wine neither expects nor
  ignores, i.e. those that end the process and those another library catches with a temporary XSetErrorHandler. If
  a lock holder waits for a reply at that moment the process hangs as before.
- The statics of X11DRV_expect_error are protected by the expecting display's mutex only; the check for an error on
  another display reads them unlocked (as upstream always did). `inst/177-review/v2-leaf-mutex.patch` (a leaf mutex
  instead of LockDisplay) is the upgrade if that ever shows.
- What the GL libraries do on winex11's displays (measured in 177's review, NVIDIA 595.91 on :101, Mesa on Xvfb):
  with EGL (the default) and with Vulkan the NVIDIA libraries make no Xlib call on them except XGetXCBConnection.
  With GLX (UseEGL=N) libGLX_nvidia takes XLockDisplay( gdi_display ) itself (also nested in our expect regions),
  does XSync, XESetError on its GLX extension, XSync, restore: it is a 177 holder and uses the same hook interface;
  its extension record is newer than ours, so its hook runs first. Mesa's GLX puts one temporary XSetErrorHandler
  around its XShmAttach check; with our hook a stray BadDrawable of a winex11 XShmPutImage no longer reaches it and
  switches XShm off. XESetWireToError is not called on any display.
- 182's rules stay: id-allocating requests inside `lock_xid_alloc`, a reply read before locking every 32768
  requests (window-surfaces.md).
