Text for a new issue at https://gitlab.freedesktop.org/xorg/lib/libx11/-/issues. Not filed.
Reproducer: [177-libx11-reproducer.c](177-libx11-reproducer.c) (the longer `tests/r177/xerrlock.c` has the workaround
modes and the cost benchmark). Patch: [177-libx11-xerror-user-lock.patch](177-libx11-xerror-user-lock.patch).
The tracker could not be searched from here (gitlab.freedesktop.org / bugs.freedesktop.org deny access; only a web
search, which found #121, the self-deadlock through the sequence sync that commit 30ccef3a fixed, a different one):
an existing issue may exist, look before filing.

---

**Deadlock: `_XError()` waits for the `XLockDisplay()` lock of a thread that waits for a reply behind it**

**Version:** libX11 1.8.13 (Ubuntu 26.04, 2:1.8.13-1), libxcb 1.17.0, x86_64; any X server. Git master (6d4432b1) has
the same `_XError()`, `_XReply()`, `handle_error()`, locking.c and LockDis.c; the patch applies to it.

**Summary**

With `XInitThreads()`, two threads on one `Display`:

- thread A calls anything that waits for a reply (`XSync()`), and an X error for an earlier request arrives before
  that reply (a drawing request on a window that another client or thread has just destroyed, say);
- thread B calls `XLockDisplay()` and then anything that waits for a reply (`XSync()`, `XGetImage()`, ...), as a
  program does that wants a request and its error to itself.

If B locks the display while A is waiting for its reply, both threads hang forever, and so does every other thread
that uses the display afterwards. The attached program does only this and hangs within milliseconds.

**Cause**

`_XReply()` returns replies in the order of the requests: B waits on `dpy->xcb->reply_notify` until A, whose request
is first in `dpy->xcb->pending_requests`, is done. A has its reply, but before it returns it processes the responses
that precede it:

```c
		/* Any user locks on another thread must have been taken
		 * while we slept in xcb_wait_for_reply64. Classic Xlib
		 * ignored those user locks in this case, so we do too. */
		InternalLockDisplay(dpy, /* ignore user locks */ 1);
		...
				while((event = poll_for_response(dpy)))
					handle_response(dpy, event, True);
```

For the error that is `handle_response()` -> `handle_error()` -> `_XError()`, and `_XError()` takes the user lock
around the call of the error handler:

```c
	if (dpy->lock)
	    (*dpy->lock->user_lock_display)(dpy);    /* _XUserLockDisplay -> _XDisplayLockWait */
	UnlockDisplay(dpy);
	rtn_val = (*_XErrorFunction)(dpy, (XErrorEvent *)&event); /* upcall */
```

So the thread that `_XReply()` deliberately lets finish under a foreign user lock waits for that lock after all, and
its holder waits for this thread.

```
Thread A (reader)                                  Thread B (holder)
#7  _XDisplayLockWait (locking.c:453)              #7  _XReply (xcb_io.c:705)   ConditionWait(dpy, dpy->xcb->reply_notify)
#8  _XUserLockDisplay (locking.c:513)              #8  XSync (Sync.c:44)
#9  _XError (XlibInt.c:1491)                       #9  holder_thread            after XLockDisplay()
#10 handle_error (xcb_io.c:220)
#11 handle_response (xcb_io.c:412)
#12 _XReply (xcb_io.c:731)
#13 XSync (Sync.c:44)
```

`XLockDisplay()` (src/LockDis.c) still has the code that was meant to prevent this in the pre-XCB transport:

```c
    /*
     * We want the threads in the reply queue to all get out before
     * XLockDisplay returns, in case they have any side effects the
     * caller of XLockDisplay was trying to protect against.
     * XLockDisplay puts itself at the head of the event waiters queue
     * to wait for all the replies to come in.
     */
    if (dpy->lock && dpy->lock->reply_awaiters) {
```

but `dpy->lock->reply_awaiters` is never set since xcb_io.c replaced the old transport (nothing calls `push_reader`
any more), so `XLockDisplay()` returns while other threads are still inside `_XReply()`.

History: commit fd85aca7 "Ignore user locks after sleeping in _XReply and _XReadEvents." (2011-03-14) fixed exactly
this pair of threads for the plain lock wait after the sleep ("thread 2 will wait for thread 1 to process its
reply ..., but thread 1 will wait for thread 2 to drop its user lock"). Commit 83e1ba59 "Call _XErrorFunction
without holding the Display lock." (2011-03-15, a day later) added the user lock around the handler call in
`_XError()` and so reopened it for the case that thread 1 has an error to process.

The same happens without an explicit `XLockDisplay()` round trip when Xlib's own sequence synchronisation runs in the
`LockDisplay()` of `XUnlockDisplay()` (GetInputFocus + `_XReply()` with the user lock held).

**Reproducer**

```
gcc -O2 -o xerrlock 177-libx11-reproducer.c -lX11 -lpthread && ./xerrlock
```

libX11 1.8.13 (distribution package, and an own build of the same source): `HANG after N round trips` with N between
0 and 7 in 8 of 8 runs. With the patch below: `ok` in 5 of 5 runs (about 280000 round trips in 5 seconds each).

**Proposed fix**

`_XError()` only takes the user lock when no other thread holds it; when another thread does, that thread locked the
display while this one was waiting with the display unlocked, the case in which `_XReply()` already ignores user
locks. `_XLockDisplay()` checks `dpy->error_threads` before it waits for the user lock instead of after, so that the
`LockDisplay()` after the handler (and any non-protocol Xlib call that the handler makes) does not wait for that
lock either. The handler is then called without the user lock in that case; the lock holder may run at the same
time, as it already does with everything else that such a thread does on its way out of `_XReply()`.

Not changed: `XLockDisplay()` still returns while threads are in `_XReply()`. Waiting for them there (the intent of
the comment above) would need this change as well, or they could never get out.

Where this was found: Wine's X11 driver expects errors with `XLockDisplay(); request; XSync(); XUnlockDisplay()`
(XShmAttach, XGetImage), while other threads flush window contents into windows that may be gone. Programs can
avoid the hang by consuming such errors in an `XESetError()` hook, which `_XReply()` calls before `_XError()`.
