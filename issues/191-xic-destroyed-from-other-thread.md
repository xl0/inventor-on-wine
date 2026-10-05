# 191 winex11: owner thread hangs in XCheckIfEvent after another thread recreated its X window (XIC destroyed under XFilterEvent, fault swallowed with the display locked)
Status: draft, root-caused by reading + one traced instance (found while running 182's GDI stress, not worked on) ·
upstream winex11 (cross-thread set_window_visual, same family as 173 / 184) + libX11 1.8.13 (`_XUnregisterFilter`
doesn't lock) + 172 (the fault is swallowed)

## Symptom
The thread that owns a window stops handling X events for good; every other thread that needs that thread's display or
the window data blocks behind it. gdb: the owner is in `X11DRV_ProcessEvents -> XCheckIfEvent -> _XLockDisplay ->
pthread_mutex_lock` on the mutex of its own display, and that mutex's owner is the same thread (non-recursive mutex
locked twice); the thread that recreates the window sits in `destroy_whole_window -> XFlush( data->display )` on the
same mutex, holding win_data.
`tests/r182/gdistress.exe 20 6 SEED` (workers call UpdateLayeredWindow / SetLayeredWindowAttributes on a window of the
main thread, which only pumps messages) under openbox on Xvfb, 20 s runs: integ b8f013d4fbb (wt/182-base-build) 1 of 30,
fix/182 4 of about 330 (`inst/182/out/ab-ob-base-1486-s17-hang.txt`, `ab-ob-fix-1480-s5-hang.txt`,
`ab-ob-fix-1486-s19-hang.txt`, `gs-seh-fix-1483-s22-hang.txt`, and `gs-ob-fix-1483-s6-hang.txt` without owner
information). Not seen without a window manager (the window must have been focused: it needs an XIC).

## Cause
- The run with WINEDEBUG=+seh (`gs-seh-fix-1483-s22.out`) has exactly one
  `handle_syscall_fault code=c0000005 ... ip=<libX11 XFilterEvent+0xe3>`, on the main thread's stack, a read through a
  wild pointer, r12 = an X window id.
- `XFilterEvent` (libX11 src/FilterEv.c) locks the display and walks `display->im_filters`. `_XUnregisterFilter`
  (src/RegstFlt.c) unlinks and frees nodes of that list without locking the display.
- `destroy_whole_window` (window.c) does `XUnsetICFocus( data->xic ); XDestroyIC( data->xic );`. Called from another
  thread through `set_window_visual` it destroys an input context of the owner's display and IM (without an IM
  server Xlib's local IM is used, which registers a KeyPress filter per focused IC): the unlocked
  `_XUnregisterFilter` runs while the owner is inside `XFilterEvent( &event, None )` in X11DRV_ProcessEvents.
- The owner reads the freed node and faults with the display mutex held. ntdll turns the fault into a return status of
  NtUserPeekMessage (172), nothing unlocks the display, and the next XCheckIfEvent blocks on it.

## Direction (not tried)
The XIC belongs to the owner thread: don't destroy it from another thread (keep it across the recreation and
re-point it at the new window with XSetICValues from the owner, or post the destruction to the owner). The other
things destroy_whole_window does on `data->display` from the wrong thread are requests and safe with XInitThreads.

## Repro
```
inst/182/x.sh start                                   # :1482 :1483 openbox
WINEDEBUG=err+all,+seh inst/182/gs.sh fix 1483 seh 60 20 6     # HANG lines; inst/182/classify.py out/*-hang.txt: "selfowner"
```
(the same runs also hit 177 and, when the threads start together, 190).

## With 177 fixed (177 worker, 2026-10-05; fix/177 b1e7b97cfed on integ cffd27540ee)
- A fast repro once 177 is out of the way: `WINEDEBUG=err+all,+seh,+synchronous tests/r182/gdistress.exe 20 6 SEED` under
  openbox hangs with this shape in 13 of 32 runs (integ: 0 of 12 get that far, all 177). Without synchronous mode:
  integ 4 of 80, fix/177 1 of 140. `BUILDS=fix WINEDEBUG=err+all,+seh,+synchronous inst/177/ab.sh 1775 TAG 10 20 6`
  (inst/177/x.sh start; :1774-:1777 openbox).
- 7 of 7 hung runs traced with `+seh` have exactly one `handle_syscall_fault` in libX11's XFilterEvent (+0xe3 five
  times, +0x88 twice; the distribution's 2:1.8.13-1, base from the XCheckIfEvent frame of the gdb dump).
- Only the distribution's libX11 binary faults: the same Wine build on an own build of the same source
  (inst/182/x11-build, `X11LIB=... inst/177/run.sh`) 10 of 10, on the build with 177's `_XError` patch 10 of 10. The
  use after free is there all the same (by reading); whether the stale node faults depends on the binary.
