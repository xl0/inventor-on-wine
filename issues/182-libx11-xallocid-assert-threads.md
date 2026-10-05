# 182 Process aborts with `_XAllocID: Assertion 'ret != inval_id' failed` when several threads create DCs (libX11 race on the shared gdi_display)
Status: fixed on fix/182 (wine-src, on integ b8f013d4fbb; commit 5c2152cd4d4 `winex11: Lock the display around the requests
that allocate X resource ids.`), verified except on Inventor (licence seat on the laptop) · Found in: review of 173
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
- Whether libX11's git master differs was not checked (no source outside the distro archive).

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

## Fix: commit 5c2152cd4d4
`lock_xid_alloc( display )` / `unlock_xid_alloc( display )` (x11drv_main.c) = `XLockDisplay` / `XUnlockDisplay`, taken
around the request that allocates the id: in `create_gc()` and `create_pixmap()` (x11drv_main.c, 30 call sites),
`create_picture()` (xrender.c, 12), and directly around the remaining ones (table above). 165 lines added, 54 removed,
11 files; no change of what is sent to the X server.
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
- No reply is waited for while holding it by our code: every locked call is a request without reply. Two cases
  remain where Xlib itself waits: the sequence sync when it falls into the locked `LockDisplay` calls (about
  3 requests in 65000, or the first Xlib request after 65000 xcb requests of a Vulkan presenter), and synchronous
  mode. In synchronous mode (`WINEDEBUG=+synchronous`, a reply after every request) the lock is therefore not taken
  at all: Xlib then never needs the sequence sync for its own requests. See 177 below.
- Not done: creating the per-DC GC lazily. It would remove most of the 2 million allocations above but is an
  optimisation, not needed for correctness.

## Cost (base = integ b8f013d4fbb, wt/182-base-build)
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
- Otherwise: the two Xlib-internal waits named above. For a hang one of them must coincide with an X error that a
  thread ahead in the reply queue is reading. Not seen in any run.
177 itself turned out to be reachable without synchronous mode: see Verification (gdistress under openbox) and the
note added to [177](177-xlib-error-vs-xlockdisplay-deadlock.md).

## Verification (wt/182-build at 4c181523f06 = this commit + 184's two; Xvfb displays of inst/182/x.sh)
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
- The Xlib-internal sequence sync under the lock is a 177 window, small but not zero; I have no measurement of it
  other than "0 of 34 dumps".
- The Xcursor calls are made inside the lock. The first one on a display queries extensions (round trips, once per
  process), and XcursorLibraryLoadCursor reads theme files there: other threads' requests on gdi_display wait that
  long, once per system cursor.
- Thread displays: only create_whole_window is locked. XOpenIM run again by the owner (IM server restart) while
  another thread recreates one of its windows is not covered; it needs 65000 requests without a single event or reply
  read on that display at that moment.
- The first (mutex) version ran the same matrix once; its results are only in inst/182/out/v1-mutex/.
- Whether libX11 master has the bug was not checked.

## Tools
`tests/r182/`: `xallocid.c` (plain Xlib reproducer with the lock modes), `gdistress.c` (multi-thread GDI / USER stress,
flags `x` = also cross-thread window drawing (188), `n` = no X window recreation), `debug-build.patch` (fix/182 +
tests/r173/lockorder-debug.patch + the XID allocation audit), `xidsites.py` (folds the audit). Harness in inst/182/:
x.sh (Xvfb :1480-:1486), run.sh (watchdog, gdb stacks + mutex owners + Xlib lock state via mowner.py), dcchurn.sh,
gs.sh, ab.sh, p173.sh, dbg.sh, audit.sh, gpu.sh, cost.sh, vs.sh, vm-vs.sh. libX11 builds: inst/182/x11-build (1.8.13 as
is, with debug info: `X11LIB=inst/182/x11-build/src/.libs inst/182/run.sh ...`), x11-build-instr, x11-build-fixed.
