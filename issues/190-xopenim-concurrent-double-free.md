# 190 winex11 + libX11: threads that create their first window at the same time corrupt the heap in XOpenIM ("double free or corruption"), the process hangs
Status: fixed on fix/191 (wine-src; 675c332fddc `winex11: Serialize the creation and destruction of input methods.`,
second commit of 191's branch, independent of 191's and of 177), verified; **read "Side effect" before merging** ·
found while running 182's GDI stress · upstream winex11 (XOpenIM per thread, unserialized) + a libX11 bug (1.8.13 and
git master 6d4432b: process-wide list of input methods without a lock; report text + reproducer + patch ready, not
filed: [report](attachments/190-libx11-bug-report.md), [reproducer](attachments/190-libx11-reproducer.c),
[patch](attachments/190-libx11-im-list-lock.patch)) · any process that starts several UI threads at once

## Symptom
A process whose threads create their first top-level windows at the same moment sometimes never gets going: glibc
prints `double free or corruption (!prev)` / `realloc(): invalid old size`, the aborting thread ends up in an endless
loop of `handle_syscall_fault code=c0000005 addr=(nil) ip=0` (WINEDEBUG=+seh writes gigabytes), and the other threads
wait for glibc's malloc lock or for win32u's user lock. `tests/r182/gdistress.exe 20 6 SEED`: 2 of about 22 runs on
fix/173, 2 of about 300 on integ + fix/182. `tests/r191/thrwin.exe` (main window, then 16 threads creating a window
each, one process per run on a fresh wineserver): 2 of 70 on fix/177.
A second, silent form: XOpenIM just fails in one of the threads (`warn:xim:xim_create Could not open input method`);
that thread then has no input method (no compose sequences / dead keys through XIM, no IM server) until it ends.

## Cause (measured)
- winex11 opens one input method per thread, on the thread's own display, in `x11drv_init_thread_data`
  (`xim_thread_attach`: XCreateFontSet, then `xim_create` -> XOpenIM), and closes it in `X11DRV_ThreadDetach`.
- libX11 `modules/im/ximcp/imInt.c`: `_XimOpenIM` adds the new IM to a process-wide list (`_XimCurrentIMlist`,
  `_XimCurrentIMcount`) with `Xrealloc( list, (count + 1) * sizeof(Xim) )`, no lock; `XCloseIM` and
  `_XimServerDestroy` (IM server gone, from XFilterEvent) scan and clear it, no lock. Two threads realloc the same
  block. Caught in Wine at the call (`tests/r191/guardmalloc.so`, fix/177): `realloc <- _XimOpenIM <- xim_create` on
  a block that the same call in another thread had just freed; gdb dump of a hung run: `realloc(oldmem, 80)` waiters
  in `_XimOpenIM` behind the aborted thread.
- Plain Xlib, no Display shared (`tests/r191/ximopen.c`: 16 threads x 64 XOpenIM at once, `XCOMPOSEFILE=/dev/null`
  so that the calls are short): 27 of 30 processes abort on the distribution's libX11, 0 of 30 with all XOpenIM /
  XCloseIM under a mutex, 0 of 30 with the proposed libX11 patch.
- The failing XOpenIM is a second race: `open_indirect_converter` (src/xlibi18n/lcConv.c) initializes three static
  quarks lazily and tests the one it sets first (2-4 of 100 processes with 8 threads x 1 XOpenIM; 0 of 200 with the
  patch, which sets that quark last).
- More unlocked process-wide state on the same paths (ThreadSanitizer on a libX11 built with -fsanitize=thread,
  `tests/r191/xthreads.c`; list in the report): the cached compose table (imLcIm.c), the instantiate callback list
  (imInsClbk.c), lazily compiled resource tables (imRm.c, lcWrap.c via XCreateFontSet). With XCreateFontSet / XOpenIM
  / XCloseIM / XFreeFontSet under one mutex and everything else concurrent, the only reports left are function
  statics of `_XimLocalFilter` (scratch buffer, previous key) and `_XErrorFunction` in XOpenDisplay.
- Each XOpenIM parses the locale's Compose file again (12 ms here) unless `~/.compose-cache/` exists (Xlib doesn't
  create it) and is writable.

## Fix (675c332fddc)
A recursive `xim_mutex` in xim.c around every call of winex11 that enters that code: `xim_thread_attach`
(XCreateFontSet, XOpenIM, XRegisterIMInstantiateCallback), the new `xim_thread_detach` (XCloseIM, XFreeFontSet; was
inline in X11DRV_ThreadDetach), and the two callbacks that open or register again when an IM server comes or goes
(`xim_open`, `xim_destroy`). Recursive because XRegisterIMInstantiateCallback calls `xim_open` back at once when an
input method is available. It is a leaf lock for Wine (taken with win_data possibly held: `thread_init_display()`
under `destroy_whole_window`; nothing of Wine's is taken inside; the XIC destroy callback, which takes win_data,
runs after the IM's and outside the mutex).
Not closed by it (inside libX11, reachable only with an IM server appearing or dying while threads start):
`_XimFilterPropertyNotify` opens input methods for every registered callback from each thread's XFilterEvent, and
`_XimServerDestroy` clears list slots, both without Wine being able to serialize them.
Cost: threads that start together open their input methods one after the other (12 ms each without a compose cache).

## Side effect: more exposure to 193 for processes whose main thread has no window
[193](193-libxext-xge-record-freed-on-last-display-close.md) is another, older race at thread start / exit (libXext
frees a global record when the last XInput2 display is closed while another thread registers its own). It needs a
moment in which the only thread display of the process is being closed. A process with a main window never has that.
A process whose only windows belong to short-lived threads does, and there the mutex changes the timing: without it
all threads are in the same phase (all initialize, all exit), with it they are staggered by the serialized
XOpenIM, so the first thread exits while later ones initialize XInput2.
`thrwin.exe 16 1 0 nomain` (no main window), fresh wineserver per run, `WINEDEBUG=+seh,+wgl,+pid`: fix/177 0 died
of 6, fix/191 5 of 6 (heap corruption / hang on Xlib's global lock); with a 12 ms stagger in the probe itself
(`thrwin.exe 16 1 12 nomain`) fix/177 dies too (1 of 5). With a main window: both 0 (table below).
So 190's commit should go in together with a fix for 193 (an experimental one-liner that closes it is in the 193 file).

## Verification (base = wt/177-build fix/177, fix = wt/191-build 675c332fddc, alternating)
| what | base | fix |
|---|---|---|
| `thrwin.exe 16` (main window + 16 threads), fresh wineserver per run, default Compose file | 2 hangs of 70 (190: `realloc(): invalid old size`, realloc waiters in `_XimOpenIM`) | 0 of 70 |
| same with `XCOMPOSEFILE=/dev/null` | 0 of 30 | 0 of 30 |
| plain Xlib `ximopen` (16 x 64), distribution libX11 | 27 abort + 3 XOpenIM failures of 30 | with `lock`: 30 ok |
| ibus XIM server stopped / started under a running 4-thread probe (`inst/191/ui-ibus-restart.sh`: xim_destroy x4, xim_open x4 through the mutex) | typing goes on | same |
The base rate in Wine is low (2 of 70), so "0 of 70" alone is weak (about 13 % chance without a fix); the plain Xlib
numbers and the allocator trace carry the conclusion.
gdistress / xicrace / input-method / conformance numbers of the branch: see 191.

## Repro
```
inst/191/x.sh start
for b in base fix; do WINEDEBUG=err+all,+seh inst/191/fresh.sh $b 1802 TAG 25; done     # thrwin 16, fresh wineserver per run
ARGS="1 0 nomain" WINEDEBUG=err+all,+seh,+wgl,+pid inst/191/fresh.sh fix 1802 TAG 6     # 193 through the mutex
WINEDEBUG=err+all,+seh inst/191/gm.sh base 1802 TAG 6 16 1 12 nomain                     # guard allocator: realloc <- _XimOpenIM
for i in $(seq 30); do DISPLAY=:1800 XMODIFIERS=@im=none XCOMPOSEFILE=/dev/null tests/r191/ximopen; done | sort | uniq -c
```
