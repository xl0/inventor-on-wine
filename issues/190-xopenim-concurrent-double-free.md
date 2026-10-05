# 190 winex11 + libX11: threads that create their first window at the same time corrupt the heap in XOpenIM ("double free or corruption"), the process hangs
Status: draft (found while running 182's GDI stress, not worked on) · upstream winex11 (XOpenIM per thread, unserialized) +
libX11 1.8.13 (global IM list without a lock) · any process that starts several UI threads at once

## Symptom
`tests/r182/gdistress.exe 20 6 SEED` (6 threads that each create a top-level window right after start) sometimes never
gets going: glibc prints `double free or corruption (!prev)`, the aborting thread ends up in an endless loop of
`handle_syscall_fault code=c0000005 addr=(nil) ip=0` (WINEDEBUG=+seh writes gigabytes), and the other threads wait for
glibc's malloc lock or for win32u's user lock. Seen on fix/173 (wt/173-build) in 2 of about 22 runs and on integ +
fix/182 in 2 of about 300 (`inst/182/out/gs-own-b173-1486-s9*`, `gs-sync-b173-1486-s4*`, `gs-seh-fix-1483-s11*`,
`gs-seh-fix-1483-s19*`; the two +seh logs were cut to their first 20 kB).

## Where
Every occurrence has a thread in
```
__libc_realloc <- _XimOpenIM <- xim_create <- xim_thread_attach <- x11drv_init_thread_data <- thread_init_display
  <- X11DRV_create_win_data <- X11DRV_WindowPosChanging <- NtUserCreateWindowEx
```
waiting for the malloc lock, and the abort message comes right at thread start.
libX11 `modules/im/ximcp/imInt.c`: `_XimOpenIM` registers the new IM in a process-wide list
(`static Xim *_XimCurrentIMlist; static int _XimCurrentIMcount;`, `_XimSetIMStructureList`): it scans the list and
`Xrealloc`s it by one element with no lock at all. Two threads in `XOpenIM` at once realloc the same pointer.
winex11 opens one input method per thread, on the thread's own display, in `x11drv_init_thread_data`
(`xim_thread_attach` -> `xim_create` -> `XOpenIM`), with nothing serializing the calls.
An undetected variant of the same race would leave two owners of one heap block; whether any of the unexplained hangs
of that stress come from it is not known.

## Direction (not tried)
Serialize `XOpenIM` / `XCloseIM` in winex11 (a mutex in xim.c around xim_create and the XCloseIM in
X11DRV_ThreadDetach; `xim_destroy` callbacks come from Xlib). The list is also walked by `_XimServerDestroy`
callbacks. And report it to libX11.

## Repro
```
inst/182/x.sh start
inst/182/gs.sh fix 1485 own 100 20 6        # look for "exited without DONE" / HANG with malloc waiters; rare
```
A probe that only starts N threads creating one window each, in a loop of processes, would hit it faster.
