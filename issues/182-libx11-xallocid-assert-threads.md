# 182 Process aborts with `_XAllocID: Assertion 'ret != inval_id' failed` when several threads create DCs (libX11 race on the shared gdi_display)
Status: fixed on fix/182, reworked after the review and rebased onto integ d8e4d0f72d2 (commits 9d2803e04c5 helpers,
52e60e38bc2 `winex11: Lock the display around the requests that allocate X resource ids.`, 43a63cdae4b dummy parent; the
first version is fix/182-v1 = 4c181523f06 on b8f013d4fbb), verified except on Inventor (licence seat on the laptop) · Found in: review of 173
(`inst/173-review/iconrace.exe dcchurn 10 4`) · upstream winex11 + upstream libX11 1.8.13 · libX11 report text ready, not filed:
[attachments/182-libx11-bug-report.md](attachments/182-libx11-bug-report.md)

## Symptom
```
iconrace.exe: ../../src/xcb_io.c:635: _XAllocID: Assertion `ret != inval_id' failed.
```
then SIGABRT. `iconrace.exe dcchurn 10 N` = N threads looping `CreateCompatibleDC( 0 ); DeleteDC()`, 20 runs of 10 s each on
Xvfb: integ (build/, b5d75449ffe) aborts in 6 of 20 with 2 threads, 20 of 20 with 4, 20 of 20 with 8.

## Cause: libX11, verified
Reproduced with plain Xlib, `tests/r182/xallocid.c` (N threads `XCreateGC` / `XFreeGC` on one Display after `XInitThreads`),
on the distro's libX11 1.8.13 and on an own build of the same source (inst/182/x11-build).
- `_XLockDisplay` (src/locking.c) prepares the display for the next request with `_XIDHandler( dpy )` (makes
  `dpy->xcb->next_xid` valid) and then `_XSeqSyncFunction( dpy )`. Once 65535 - 512 requests were sent since the last reply
  or event was read, the latter sends GetInputFocus and calls `_XReply`, which unlocks the display around
  `xcb_wait_for_reply64`. The user-level lock is not held during that wait (`sync_while_locked` takes it only afterwards).
- A thread that locks the display meanwhile (or was queued in its own sequence sync ahead and returns first) creates a
  resource: `_XAllocID` hands out `next_xid` and sets it to `inval_id`. The first thread comes back from `LockDisplay`
  into its `XCreateGC`, calls `XAllocID`: assertion.
- Instrumented build (inst/182/src/libX11-instr: a thread-local flag set when the sync waited, a counter of
  `_XAllocID` calls): 9 of 9 aborts (2, 4, 8 threads) are in a thread that had waited for a sequence sync reply inside
  the same `LockDisplay`, while other threads took 1 to 35 ids.
- Swapping the two calls in `_XLockDisplay` (sync first, id after;
  [attachments/182-libx11-lockdisplay-order.patch](attachments/182-libx11-lockdisplay-order.patch)): 12 of 12 runs of
  15 s with 2, 4, 8, 16 threads finish (37 million GCs per 2-thread run). That is the proposed libX11 fix.
- Requests that Vulkan / EGL drivers send through xcb on the same connection count too (`dpy->request` jumps when Xlib
  takes the socket back), so a process that presents with Vulkan syncs more often than its own Xlib traffic suggests.
- libX11's git master still has the two calls in that order (checked in the review). xorg/lib/libx11 issue #10 is
  the same assertion, open since 2010 without a cause; the report text is written as a comment for it.

What protects a thread (reproducer modes, `xallocid THREADS SECS MODE [NOISE]`, 8 s runs):
| who allocates how | result |
|---|---|
| all plain | abort 3 of 3 (also with other threads reading replies now and then, `NOISE_PERIOD=30000`: 3 of 3) |
| all inside `XLockDisplay` / `XUnlockDisplay` | 0 of 8 |
| all inside one application mutex | 0 of 6 |
| one plain, the others inside the mutex (`mixmutex`) | abort 3 of 3 |
| one plain, the others inside `XLockDisplay` (`mixlock`) | 0 of 16 (4 with reply-reading noise) |
| two plain, the others inside `XLockDisplay` (`mix2lock`) | abort 3 of 3 |
Why the user lock protects against threads that don't take it: other threads entering `LockDisplay` wait in
`_XDisplayLockWait` before their `_XIDHandler`; a thread that returns from a sequence sync takes the user lock for a
moment in `sync_while_locked` before it goes on to allocate, so it waits for the holder; and the holder's
`XUnlockDisplay` runs `LockDisplay` first, which refills the id before the lock is released. So a holder neither
loses its id nor takes one away from a waiting thread. A mutex of our own has none of this: it only helps if every
allocator on the display takes it.

## Who allocates X resource ids on a display that several threads use
Found by reading (grep of every creating call) and by a debug build that replaces the display's `resource_alloc`
hook (the `XAllocID` macro of Xlib.h) with a logger: call chain, display kind, lock held or not
(`tests/r182/debug-build.patch`, folded with `tests/r182/xidsites.py`). Counts are from one pass over gdi32 dc bitmap
font clipping brush pen palette dib gdiobj, user32 win msg input sysparams monitor clipboard cursoricon dce class,
comctl32 imagelist, opengl32 opengl (Xvfb / llvmpipe, and the NVIDIA Xorg :101 with d3d11_present vulkan / gl), and
the stress probes below (`inst/182/out/sites-all.txt`).
| display | call | where | allocations seen | concurrent? | now |
|---|---|---|---|---|---|
| gdi_display | XCreateGC | one per DC (create_x11_physdev: every CreateCompatibleDC / CreateDC / window DC), SET_DRAWABLE escape (every GetDC on a window without surface, the screen DC), StretchBlt, PutImage with a ROP, GetImage fallback, pixmap from image, window surface, xrender put_image / StretchBlt | 1970746 | any thread, the common one | locked (create_gc) |
| gdi_display | XCreatePixmap | execute_rop temporaries, StretchBlt / PutImage / GetImage, bitmap brushes and cursor bitmaps (create_pixmap_from_image), pattern brush recolouring, xrender tiles / masks / temporaries, cursors | 38278 | any thread that draws through the X11 driver (screen DC, windows without surface) | locked (create_pixmap) |
| gdi_display | XRenderCreatePicture | per DC on first xrender use, sources, tiles, masks, temporaries | 26889 | same | locked (create_picture) |
| gdi_display | XRenderCreateGlyphSet | per font / antialiasing format (under xrender_mutex) | 8257 | any thread that draws text on such a DC | locked |
| gdi_display | XRenderCreateLinearGradient | GradientFill | 2782 | same | locked |
| gdi_display | XShmAttach | every window surface image (create_shm_image) | 34943 | any thread (cross-thread UpdateLayeredWindow too) | already inside X11DRV_expect_error = display locked |
| gdi_display | XCreateBitmapFromData (pixmap + GC) | hatch brushes, mono dither brush, empty cursor | 564 | any thread | locked |
| gdi_display | XCreatePixmap + XCreateGC | BRUSH_DitherColor | 0 (needs a palette display) | any thread | already inside XLockDisplay upstream |
| gdi_display | Xcursor (XcursorImagesLoadCursor: pixmap, GC, picture, cursor; XcursorLibraryLoadCursor), XCreatePixmapCursor, XCreateFontCursor | SetCursor in the thread of the window under the pointer | 2775 x 4 + 69 | every UI thread | locked |
| gdi_display | XCreateWindow | client windows (GL / Vulkan surfaces), dummy parent | 67 | any thread that creates such a surface | locked |
| gdi_display | XCreateColormap | client surfaces with another visual, private palette colormap, GLX init window | 5 | same | locked |
| gdi_display | XCreateColormap in process init | x11drv init | 209 (once per process) | no, single thread | not locked |
| gdi_display | ids inside the GL library | GLX: glXCreateContext / Window / Pbuffer; NVIDIA's EGL (595.91): eglCreatePbufferSurface and eglMakeCurrent, called by win32u (pbuffer_create, create_memory_pbuffer, make_internal_context_current); Mesa's EGL / GLX and both Vulkan drivers allocate through xcb, which does not use Xlib's id | 154 (NVIDIA, opengl32:opengl + d3d11_present gl) | GL threads | not locked, see below (GLX context creation is inside X11DRV_expect_error) |
| a thread's display | XCreateWindow, XCreateColormap in create_whole_window | every top-level window; from ANOTHER thread in set_window_visual (UpdateLayeredWindow, SetLayeredWindowAttributes, WS_EX_LAYERED on a window of another thread) | own 20347 + 5464, other thread 10419 + 5082 | owner vs. other thread, two other threads | locked |
| a thread's display | clip window, XIM (XOpenIM's window, XCreateFontSet fonts), desktop / systray, clipboard windows (30233 in user32:clipboard) | owner thread only | | no second allocator of that kind | not locked |
SetWindowText (173) and the other cross-thread uses of a thread's display allocate nothing.
Inventor could not be measured (no licence seat). What it would add is known from 173's session: several threads with
windows, GDI through window surfaces (GCs per DC and per surface, XShm segments).

## Fix (second version, see "Review round")
9d2803e04c5 adds `create_gc()` and `create_pixmap()` (x11drv_main.c, 30 call sites) and `create_picture()` (xrender.c,
12) without a functional change. 52e60e38bc2: `lock_xid_alloc( display )` / `unlock_xid_alloc( display )`
(x11drv_main.c) = `XLockDisplay` / `XUnlockDisplay`, taken in the helpers and directly around the remaining
allocating requests (table above), with a pre-sync before the lock (below). No change of what is sent to the X
server apart from that GetInputFocus round trip every 32768 requests.
- Why the display's user lock and not a driver mutex. The first version of this fix (82ad7612f45, kept in the reflog;
  results in inst/182/out/v1-mutex/) used a driver mutex. It passed everything below, but the allocation audit on the
  NVIDIA display showed allocators it cannot reach: NVIDIA's EGL library takes ids from the same Display inside
  `eglCreatePbufferSurface` and `eglMakeCurrent`, which win32u calls directly. With a mutex one such call and one GDI
  thread can still abort each other (`mixmutex`); with the user lock a locked request is safe from unlocked ones and
  the other way round (`mixlock`), only two unlocked ones can still race. Left unlocked on purpose: the calls into
  the GL libraries (they do round trips of their own, and their id use is not ours to see), i.e. two threads that
  create GL drawables / make EGL contexts current at the same moment on NVIDIA are the remaining pair.
- Nesting: the user lock is recursive, so the sites that already hold it (create_shm_image and GLX context creation
  inside X11DRV_expect_error, BRUSH_DitherColor) need nothing, and the helpers may be called inside each other.
- Lock order (173): a thread inside lock_xid_alloc only makes Xlib calls, it takes no win32u or driver lock and
  calls nothing that can wait for one; it is entered with win_data, the user lock, a surface mutex or xrender_mutex
  held, like X11DRV_expect_error. The lock-order debug build shows no pthread mutex taken inside.
- No reply is waited for while holding it by our code: every locked call is a request without reply. But Xlib's own
  sequence sync can run inside the locked `LockDisplay` calls, in particular in the one XUnlockDisplay makes, i.e.
  with the user lock held. My first estimate ("about 3 requests in 65000") was wrong: the sync lands on locked
  requests in proportion to their share of all requests (review: 17 of 61 syncs in dcchurn 10 4, 119 of 672 in a
  one-thread ROP blit loop). Then every thread that uses the display waits for that round trip, and it is 177's
  shape with a new holder (reproduced by the reviewer in plain Xlib 5 of 5, `inst/182-review/xlat.c`). So
  lock_xid_alloc reads a reply itself before locking once half of the sequence numbers are used up:
  `if (NextRequest( display ) - LastKnownRequestProcessed( display ) > 0x8000) XSync( display, False );`
  With it Xlib's own sync does not happen at all in these workloads (instrumented libX11: base 56 syncs in dcchurn 10
  4 and 485 in the ROP loop; fix 0 and 0, none under the lock).
- In synchronous mode (`WINEDEBUG=+synchronous`, a reply after every request) the lock is not taken: Xlib then never
  needs the sequence sync for its own requests, and a round trip per request under the lock would be 177 every time.
  The mode is read once at process start (`synchronous_mode`) and used for lock, unlock and the XSynchronize of new
  thread displays: debug channels can change at run time, and a flip between lock and unlock would have left a
  display locked.
- Not done: creating the per-DC GC lazily. It would remove most of the 2 million allocations above but is an
  optimisation, not needed for correctness.
- 43a63cdae4b (review F4, an older race): get_dummy_parent() checks its static again once it holds the lock, two
  threads no longer create two dummy parents (not in synchronous mode, where nothing is locked).
- Considered in the review and not taken: an XSetAfterFunction hook on gdi_display that syncs early, without any
  locking (`inst/182-review/v2-after-function.patch`): it does not cover a burst of 65000 xcb-only requests followed
  by two threads that allocate. A pool of pre-generated ids behind the Display's `resource_alloc` hook (my idea, not
  built): ids that sit unused when xcb asks the server for a new id range (XC-MISC) can be handed out twice.

## Cost (first version; base = integ b8f013d4fbb. For the second version see "Review round")
- CreateCompatibleDC + DeleteDC, one thread on one core, 8 alternating 5 s runs (inst/182/cost.sh): base 5.67-6.60 us,
  mean 5.95; fix 5.70-6.64 us, mean 5.94. Not measurable.
- The lock itself, plain Xlib, one thread (`xallocid 1 4 none|lock|mutex`): 0.190 / 0.190 / 0.191 us per
  XCreateGC + XFreeGC. With 4 threads contending the user lock is slower than a mutex (5.0 vs 12 million GCs in 8 s).
- gdi32:bitmap 7.0-7.5 s on both, gdi32:dc 5.7-6.3 s (base) / 6.2-6.3 s (fix), 3 runs each.
- `tests/d3d11_present.exe` on the NVIDIA Xorg :101 (own prefixes, inv4 leased for the display only): exit 0 on both
  builds with renderer=vulkan (fix/173 3491 / 3876 fps, fix 4030 / 4172 fps) and renderer=gl (8412 / 7742 vs 11489 /
  10418 fps); opengl32:opengl there: 11784 tests, 121 failures, 46 todo, 19 skipped on both.

## 177 (hang between `_XError` and a holder of the display's user lock that waits for a reply)
Not closed by this fix and not meant to be. What the fix adds to it:
- Synchronous mode: nothing (no lock taken).
- Otherwise: nothing as long as the pre-sync keeps Xlib's own sequence sync from happening (0 syncs in the measured
  workloads). It does not see requests that a GL / Vulkan driver sends through xcb until Xlib takes the socket back,
  so the first locked request after a burst of more than 65000 of those can still sync under the lock (the
  reviewer's `presentchurn` on the NVIDIA display showed none with the pre-sync).
177 itself turned out to be reachable without synchronous mode: see Verification (gdistress under openbox) and the
note added to [177](177-xlib-error-vs-xlockdisplay-deadlock.md).

## Review round (2026-10-05, inst/182-review/; wt/182-build at cffd27540ee on integ d8e4d0f72d2, base = build-next)
Taken: F1 the pre-sync (the reviewer's tested variant, comment reworded), F3 the synchronous mode latched once (a
static set where XSynchronize is called; also used for the thread displays), F4 the dummy parent re-check (own
commit), the split into helpers + lock, the report text as a comment for libx11 issue #10. Not taken: F2 (warm up
Xcursor with XcursorSupportsARGB at init): measured with a plain Xlib program, it moves the round trips out of the
first XcursorImagesLoadCursor (11 -> 8 requests, no reply read inside), but the first XcursorLibraryLoadCursor still
reads a reply (XFixes, from XFixesSetCursorName) and reads its theme files inside the lock; half a cure, left as a
weak spot.
- `dcchurn 10 N`, N = 2, 4, 8, 20 runs each: 60 of 60, 0 assertions (+ 18 of 18 as background load later).
- Xlib's own sequence syncs, libX11 with a counter (inst/182/x11-build-instr): dcchurn 10 4: base 56 (then the
  assertion), fix 0; `rv182.exe bench 6 1 rop`: base 485, fix 0. None under the user lock.
- `vstate.exe loop 60` x 6 under openbox: 0 of 720 moved.
- Units, both arches, vs deps/regress/d8e4d0f72d2...-h26: all 20 lines equal (gdi32:dc fail 3 / 3, user32:win fail
  4 / 4, user32:msg fail 1 / 1, the rest pass, same todo and skip counts).
- gdistress: openbox `n` 10 of 10, `+synchronous` `n` 4 of 4, visual_race text 3 of 3; no WM with recreation: see
  the last lines of this section.
- The reviewer's bystander bench, `inst/182-review/rv182.exe bench 6 1 rop` (one thread of ROP blits on the screen
  DC, a second thread draws a rubber band every ms and measures its own calls), pinned to CPUs 20-39, alternating:
  | | base p99 (us) | fix p99 (us) |
  |---|---|---|
  | quiet host | 286, 285, 304, 311, 276 | 307, 351, 534, 290, 278 |
  | 14 dcchurn threads of other Wine processes on overlapping CPUs | 350, 349, 380 (earlier batch 385, 378, 503, 449, 407, 345) | 2756, 2417, 3247 (earlier 4259, 5083, 6537, 3843, 2610, 4445) |
  So on a quiet host the pre-sync brings p99 back to base, as the review found. Under CPU contention it does not:
  2.4-6.5 ms against 0.35-0.5 ms, and an experiment build without the pre-sync is no different there (317, 4745, 6719
  us). It is not a round trip: with a libX11 that records where each thread waits (inst/182/x11-build-wait), the
  allocating thread never waits longer than 0.6 ms for anything (its 1500 pre-syncs take 10 us on average), while
  the rubber band thread waits up to 8.3 ms for the display's user lock (33 and 95 waits over 1 ms in two runs) and up
  to 8.8 ms for the display mutex (34 and 124 over 1 ms; base: 2 and 14, user lock 0 and 2). Xlib's locks are not
  fair: a thread that has to sleep on them loses the race against a thread that takes them again within
  nanoseconds, the more so the later the scheduler lets it run; and with the fix an allocating thread takes the
  display mutex about four times per allocation instead of once and holds the user lock in between. This is the cost
  of the user lock for threads that allocate nothing while another thread allocates at a few 100000 per second on a
  busy host; a driver mutex would not make them wait (not measured), but leaves the GL library hole. Not solved.
- gdistress without a WM, with recreation, on the new base (alternating, `ab.sh`): base 75 of 75, fix 83 of 85; the
  two hangs are 177 with the usual pair (reader destroy_whole_window's XSync, holder X11DRV_GetImage), no thread
  inside lock_xid_alloc or a pre-sync. The first version had 70 of 70 there on the old base; two of 85 against
  none of 75 does not show whether the rate changed.

## Verification of the first version (wt/182-build at 4c181523f06 on b8f013d4fbb; Xvfb displays of inst/182/x.sh)
- `iconrace.exe dcchurn 10 N`, 20 runs each: N = 2, 4, 8: 60 of 60 finish, 0 assertions (integ: 6 / 20 / 20 of 20 abort).
- `tests/r182/gdistress.exe 20 6 SEED` (6 threads: memory DC churn, pens / solid / hatched / pattern brushes, BitBlt
  with ROPs, StretchBlt, AlphaBlend, GradientFill, GetPixel, text in 8 fonts with all antialiasing kinds and region
  clipping on the screen DC (= drawn by the X11 driver) and on window DCs, icons, colour / mono / system cursors,
  window regions, and cross-thread X window recreation of a window of the main thread):
  | mode | fix/182 | base |
  |---|---|---|
  | no WM, with recreation | 70 of 70 done (20 s; + 5 of 5 with 12 threads, 30 s) | fix/173: 9 of 10, 1 = 190 |
  | no WM, flag `n` (no recreation) | 20 of 20 | |
  | openbox, `n` | 60 of 60 | integ 60 of 60 |
  | awesome, `n` | 20 of 20 | |
  | `+synchronous`, no WM, `n` | 6 of 6 | |
  | openbox, with recreation, alternating runs (`ab.sh`) | 54 of 60; hangs: 177 x 3, 191 x 2, 190 x 1 | integ 53 of 60; hangs: 177 x 4, 190 x 2, 191 x 1 |
  | `+synchronous`, no WM, with recreation | 0 of 6: all 177 (holder X11DRV_GetImage x 3, create_shm_image x 3) | fix/173 0 of 6: 177 x 5, 190 x 1 |
  0 `_XAllocID` assertions and 0 X errors in all of them. The hangs are three older bugs that the recreation part
  of the stress provokes on any build: [177](177-xlib-error-vs-xlockdisplay-deadlock.md) (also without synchronous
  mode, as it turned out), [190](190-xopenim-concurrent-double-free.md) (threads starting together; the probe now
  starts them one by one) and [191](191-xic-destroyed-from-other-thread.md). All 34 hang dumps of fix/182 in normal
  mode under a WM (about 390 runs incl. reruns with +seh and with a libX11 that has debug info) were sorted with
  `inst/182/classify.py`: 27 x 177 with the holder in X11DRV_GetImage, 4 x 191, 3 x 190; none has a thread
  waiting for a reply inside lock_xid_alloc. With flag `x` (threads draw on each other's windows) the stress runs into
  [188](188-window-dc-blit-while-surface-replaced.md) on every build.
- 173's probes, without a WM / with openbox: iconlock icon 5 / 5, visual_race 5 / 5, visual_race text 5 / 5, flushpost
  2 / 2, uistress 3 / 3, iconrace draw 2 / 2, wdraw 2 / 2, cursor 2 / 2 (0 bad images), lockstress 15000 nogl seeds 1-3 /
  4-6: all done, no X error.
- Lock-order debug build (fix/182 + tests/r173/lockorder-debug.patch + the allocation audit, wt/182-dbg-build): dcchurn,
  gdistress (also `x`), iconlock, visual_race (+ text), flushpost, uistress, iconrace wdraw / cursor, vstate loop,
  lockstress 3000 seeds 1-2 and 600 with GL, without a WM and with openbox: 29 of 30 done (iconrace cursor once did
  not finish under openbox: a thread inside the debug build's backtrace() with the user lock, no X lock involved; fine
  on the other display and on the plain build). The fix adds no pthread mutex, so the lock graph is 173's: 49 edges,
  0 cycles, no edge that 173's runs didn't have (`inst/182/out/lock2.folded`; the gdi32:bitmap unit must be left out of
  the fold: something in it leaves the debug build's bookkeeping with gdi_lock "held", with the first version of the
  fix as well). 0 X errors. Allocation audit over these runs and the units: every id
  allocated on gdi_display or on another thread's display is allocated with the display locked, except the ones
  listed as "not locked" in the table above.
- `tools/regress.sh unit`, both arches, vs deps/regress/b5d75449...-h26: gdi32:dc fail 3 / 3 (todo 5, skip 4),
  gdi32:bitmap pass (todo 94), gdi32:font pass (116, skip 106), gdi32:clipping pass (3), user32:cursoricon pass (27),
  user32:dce pass (9), comctl32:imagelist pass (64), user32:input pass (143 / 138, skip 1), user32:win fail 4 / 4 (160 /
  158), user32:msg fail 1 / 1 (253, skip 3): status, failure, todo and skip counts equal the baseline in all 20
  runs, 0 worse (`inst/182/out/ut.txt`).
- Inventor: not run (licence seat on the laptop).

## Weak spots
- Completeness rests on the grep + the audit of what the tests run. A new or missed allocation in winex11 is
  "unlocked": it can only collide with another unlocked one, not with the locked ones.
- GL library allocations are not serialized (NVIDIA EGL through win32u; GLX window / pbuffer creation in winex11
  left alone as well): two GL threads can still hit the libX11 bug against each other. Wrapping the GLX calls would
  hold the user lock across round trips of the GL library.
- Synchronous mode is not protected (by design, see Fix). With a Vulkan / EGL presenter in the process (xcb requests
  that Xlib doesn't see) the sequence sync can still happen there.
- Threads that only draw wait longer for Xlib's locks while another thread allocates at a high rate, by milliseconds
  at p99 on a busy host (Review round). Not solved.
- The pre-sync does not see xcb-only requests of GL / Vulkan drivers: after a burst of 65000 of them Xlib's sync can
  still run under the lock once (177 window, and a stall of one round trip).
- The Xcursor calls are made inside the lock. The first one on a display queries extensions (round trips, once per
  process), and XcursorLibraryLoadCursor reads theme files there: other threads' requests on gdi_display wait that
  long, once per system cursor.
- Thread displays: only create_whole_window is locked. XOpenIM run again by the owner (IM server restart) while
  another thread recreates one of its windows is not covered; it needs 65000 requests without a single event or reply
  read on that display at that moment.
- The first (mutex) version ran the same matrix once; its results are only in inst/182/out/v1-mutex/.
- wt/182-dbg (debug build, audit) is still the first version: tests/r182/debug-build.patch applies to fix/182-v1.

## Tools
`tests/r182/`: `xallocid.c` (plain Xlib reproducer with the lock modes), `gdistress.c` (multi-thread GDI / USER stress,
flags `x` = also cross-thread window drawing (188), `n` = no X window recreation), `debug-build.patch` (fix/182 +
tests/r173/lockorder-debug.patch + the XID allocation audit), `xidsites.py` (folds the audit). Harness in inst/182/:
x.sh (Xvfb :1480-:1486), run.sh (watchdog, gdb stacks + mutex owners + Xlib lock state via mowner.py), dcchurn.sh,
gs.sh, ab.sh, p173.sh, dbg.sh, audit.sh, gpu.sh, cost.sh, vs.sh, vm-vs.sh. libX11 builds: inst/182/x11-build (1.8.13 as
is, with debug info: `X11LIB=inst/182/x11-build/src/.libs inst/182/run.sh ...`), x11-build-instr, x11-build-fixed.
