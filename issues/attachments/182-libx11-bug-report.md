Text for a libX11 bug report (https://gitlab.freedesktop.org/xorg/lib/libx11/-/issues). Not filed.
Reproducer: tests/r182/xallocid.c. Patch: issues/attachments/182-libx11-lockdisplay-order.patch.

---

**Title:** `_XAllocID: Assertion 'ret != inval_id' failed` when two threads create resources on one Display (race between the sequence sync and the XID refill in `_XLockDisplay`)

**Version:** libX11 1.8.13 (Ubuntu 26.04, 2:1.8.13-1), libxcb 1.17.0, x86_64. Any X server (seen with Xvfb and Xorg).

**Summary**

A program that calls `XInitThreads()` and creates X resources (GCs, pixmaps, windows, pictures, ...) from two or more
threads on the same `Display` aborts with

```
xcb_io.c:635: _XAllocID: Assertion `ret != inval_id' failed.
```

once enough requests have been sent without reading a reply. With the attached program (N threads calling
`XCreateGC` / `XFreeGC` in a loop) that takes a second or two.

**Cause**

`_XLockDisplay()` (src/locking.c) prepares the display for the next request with

```c
    _XIDHandler(dpy);         /* make sure dpy->xcb->next_xid is valid */
    _XSeqSyncFunction(dpy);   /* GetInputFocus + _XReply() every ~65000 requests without a reply */
```

`_XSeqSyncFunction()` calls `_XReply()`, which unlocks the display while it waits in `xcb_wait_for_reply64()`, and
the user-level display lock is not held during that wait (`sync_while_locked()` only takes it afterwards). So:

1. Thread A enters `LockDisplay()` from `XCreateGC()`. `_XIDHandler()` finds `next_xid` valid. The sequence sync is
   due, A sends GetInputFocus and waits for the reply with the display unlocked.
2. Thread B runs `XCreateGC()` to completion in the meantime (or was itself queued in a sequence sync ahead of A and
   returns first): `_XAllocID()` hands out `next_xid` and sets it to `inval_id`.
3. A gets its reply, returns from `LockDisplay()` into `XCreateGC()` and calls `XAllocID()`: `next_xid` is `inval_id`,
   the assertion fails. Nothing refills the XID between the sync and the caller's allocation.

An instrumented build (a thread-local flag set when `_XSeqSyncFunction()` waited for its reply, and a counter of
`_XAllocID()` calls) shows that in every failing run the asserting thread had waited for a sequence sync reply inside
the same `LockDisplay()`, and that other threads allocated between 1 and 35 XIDs during that wait (9 of 9 runs with 2, 4
and 8 threads).

This looks like the remaining part of what commits cc19618d ("Fix XAllocID race: hold the user display lock until we
have a new XID") and a6d974dc ("Move XID and sync handling from SyncHandle to LockDisplay to fix races") addressed.

**Reproducer**

```
gcc -O2 -o xallocid xallocid.c -lX11 -lpthread
./xallocid 4 10        # 4 threads, 10 seconds
```

Results on libX11 1.8.13, Xvfb, 8 second runs: 2, 4 and 8 threads abort in every run (9 of 9). One thread: no abort.
`./xallocid 4 10 lock` (every thread takes `XLockDisplay()` around its `XCreateGC()`) and `./xallocid 4 10 mutex`
(a mutex of the application around every `XCreateGC()`) do not abort, `./xallocid 4 10 mixmutex` (one thread without
the mutex) does.

```c
#include <X11/Xlib.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static Display *dpy;
static volatile int stop;

static void *thread( void *arg )
{
    while (!stop)
    {
        GC gc = XCreateGC( dpy, DefaultRootWindow( dpy ), 0, NULL );
        XFreeGC( dpy, gc );
    }
    return NULL;
}

int main( int argc, char **argv )
{
    int i, n = argc > 1 ? atoi( argv[1] ) : 4, secs = argc > 2 ? atoi( argv[2] ) : 10;
    pthread_t t[64];

    XInitThreads();
    if (!(dpy = XOpenDisplay( NULL ))) return 2;
    for (i = 0; i < n; i++) pthread_create( &t[i], NULL, thread, NULL );
    sleep( secs );
    stop = 1;
    for (i = 0; i < n; i++) pthread_join( t[i], NULL );
    printf( "DONE\n" );
    return 0;
}
```

**Suggested fix**

Run the sequence sync first and the XID refill after it, so that nothing unlocks the display between the refill and the
caller's request:

```diff
--- a/src/locking.c
+++ b/src/locking.c
@@ static void _XLockDisplay(
-    _XIDHandler(dpy);
     _XSeqSyncFunction(dpy);
+    _XIDHandler(dpy);
 }
```

`_XIDHandler()` itself unlocks the display around `xcb_generate_id()`, but it holds the user-level lock while it does
(`_XAllocIDs()`), so other threads wait in `_XDisplayLockWait()` and the thread comes back with the display locked
and a valid XID. With this change the reproducer ran 12 times for 15 seconds with 2, 4, 8 and 16 threads without an
abort (36 million GCs in the 2-thread runs); the unmodified build of the same source aborted in every run.

Not checked: `_XPrivSyncFunction()` (the non-threaded path) calls the two functions in the same order; it cannot race
without threads.

**Where it was found**

Wine's X11 driver allocates a GC on one shared Display for every GDI device context, from whichever thread creates
it; a Windows program that creates memory DCs in two threads aborts within seconds.
