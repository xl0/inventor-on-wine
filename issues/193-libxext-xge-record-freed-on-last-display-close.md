# 193 winex11 + libXext: heap corruption / hang on Xlib's global lock when the last thread display is closed while another thread opens its own (XGE record freed without a lock)
Status: draft, root-caused, not fixed (an experimental Wine-side workaround works, see Direction) · found by 191's
worker while testing 190 (`tests/r191/thrwin.exe ... nomain`) · upstream winex11 (XInput2 per thread display) + a libXext
bug (1.3.4 and git master 508f8ad, src/Xge.c) · processes whose only windows belong to short-lived threads; made much
more likely by 190's fix

## Symptom
A process without a long-lived window thread, in which one thread's last window goes away (the thread exits) while
another thread creates its first one, dies with a glibc heap check (`corrupted size vs. prev_size while consolidating`,
`free(): chunks in smallbin corrupted`, `corrupted double-linked list`, then `err:seh:call_seh_handlers invalid frame`
and a segfault) or hangs with every thread waiting for Xlib's global lock (`_Xglobal_lock`: in XOpenDisplay,
`_XrmInternalStringToQuark`, `XextRemoveDisplay`), whose owner is a thread that is itself waiting for it or for
something else.
`tests/r191/thrwin.exe 16 1 STAGGER nomain` (16 threads create and destroy one window each, the main thread has
none; one process per run on a fresh wineserver, `WINEDEBUG=+seh,+wgl,+pid`):
| | fix/177 (wt/177-build) | fix/191 with 190's XIM mutex |
|---|---|---|
| all threads at once (STAGGER 0) | 0 died of 6 | 5 died of 6 (earlier batches: 6 of 6; 3 died + 1 hung of 4) |
| thread i starts 12 ms after thread i-1 | 1 died of 5 | |
| STAGGER 5 / 25 | 0 of 5 / 0 of 5 | |
| with a main window (no `nomain`) | 0 of 8 | 0 of 8 |
| fix/191 + the workaround below, STAGGER 0 / 12 | | 0 of 8 / 0 of 6 |
Without tracing and with lingering wineservers it is rarer: fix/177 5 hangs of 50 (`XCOMPOSEFILE=/dev/null`; the one
looked at has this shape, the others were not told apart from 190), fix/191 1 death of 50.

## Cause
- winex11 initializes XInput2 on every thread display (`x11drv_xinput2_init`: XIQueryVersion, XISelectEvents), never on
  gdi_display. libXi registers each such display with libXext's generic event code (`xgeExtRegister`).
- libXext src/Xge.c keeps those displays in a static `XExtensionInfo *xge_info`. `_xgeFindDisplay()` creates it on
  demand and passes it to `XextFindDisplay()` / `XextAddDisplay()`; the close hook `_xgeDpyClose()` (from
  XCloseDisplay) does `if (xge_info->ndisplays == 0) { XextDestroyExtension( xge_info ); xge_info = NULL; }`.
  Nothing locks the pointer. When the last registered display is closed while another thread is in
  `_xgeFindDisplay()`, that thread uses the freed record (XextAddDisplay links the new display into freed memory:
  heap corruption) or a NULL one.
- Measured in Wine: a swallowed fault (172) at libXext.so.6+0x6b1e = `XextAddDisplay`, the `dpyinfo->next =
  extinfo->head` after `_XLockMutex( _Xglobal_lock )`, with `extinfo` (rcx) = NULL: the thread goes on with Xlib's
  global lock held, every later taker hangs (`inst/191/out/fresh-c-fix-1802-r4*`, `sehwgl-1*`; the first base hang
  `tw-null-base-1802-r8-hang.txt` shows the owner itself in XCloseDisplay -> XextRemoveDisplay).
- Plain Xlib (`tests/r191/xgeclose.c`: 4 threads, each XOpenDisplay + XIQueryVersion + XCloseDisplay in a loop): SIGSEGV
  or `malloc(): unaligned tcache chunk detected` within a second, 4 of 4; with one registered display kept open by the
  main thread: ok 4 of 4 (35000-50000 cycles in 5 s).
- Why 190's mutex makes it likely: with all threads starting together they used to be in the same phase (all
  open, all register, all close); serialized XOpenIM calls (12 ms each) stagger them, so the first thread closes its
  display, alone in the list, while the next ones are registering.
A process with a main window (Inventor, anything with a UI thread that lives as long as the process) always has one
registered display and never frees the record: not reachable there.

## Direction
- Wine side: keep one display registered for the lifetime of the process. Tried (not committed,
  `tests/r191/xge-keep-display-try.patch`): `pXIQueryVersion( gdi_display, &major, &minor )` in `x11drv_xinput2_load()`
  when the extension is there. `thrwin nomain` on fix/191 + that: 0 died of 8, and 0 of 6 with a 12 ms stagger.
  Not checked: what else declaring XI 2.2 on gdi_display changes (it selects no input events; 062 once saw an
  XFixes query on gdi_display change X timing), conformance tests.
- libXext: don't free `xge_info` (or lock it); worth a report with xgeclose.c.
- Should go in together with 190's commit.

## Repro
```
inst/191/x.sh start
ARGS="1 0 nomain" WINEDEBUG=err+all,+seh,+wgl,+pid inst/191/fresh.sh fix 1802 TAG 6      # fix = wt/191-build
ARGS="1 12 nomain" WINEDEBUG=err+all,+seh,+wgl,+pid inst/191/fresh.sh base 1802 TAG 6    # base = wt/177-build
DISPLAY=:1800 tests/r191/xgeclose 4 5 ; DISPLAY=:1800 tests/r191/xgeclose 4 5 keep
```
