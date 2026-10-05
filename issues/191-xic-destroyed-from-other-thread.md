# 191 winex11: owner thread hangs in XCheckIfEvent after another thread recreated its X window (XIC destroyed under XFilterEvent, fault swallowed with the display locked)
Status: fixed, reviewed, on integ (12ef0899477 `winex11: Don't destroy the input context of a window from another
thread.` preceded by the reviewer's 48676c8af1c `Keep the window data locked while using the input context in
ToUnicodeEx().`, see "Review" at the end), verified · found while running 182's GDI stress · upstream winex11
(cross-thread set_window_visual, same family as 173 / 184) + a libX11 bug (1.8.13 and git master 6d4432b:
`_XUnregisterFilter` doesn't lock; report text + reproducer + patch ready, not filed:
[report](attachments/191-libx11-bug-report.md), [reproducer](attachments/191-libx11-reproducer.c),
[patch](attachments/191-libx11-filter-list-lock.patch)) + 172 (the fault is swallowed)

## Symptom
The thread that owns a window stops handling X events for good; every other thread that needs that thread's display or
the window data blocks behind it. gdb: the owner is in `X11DRV_ProcessEvents -> XCheckIfEvent -> _XLockDisplay ->
pthread_mutex_lock` on the mutex of its own display, and that mutex's owner is the same thread (non-recursive mutex
locked twice); the thread that recreates the window sits in `destroy_whole_window -> XFlush( data->display )` on the
same mutex, holding win_data.
`tests/r182/gdistress.exe 20 6 SEED` (workers call UpdateLayeredWindow / SetLayeredWindowAttributes on a window of the
main thread, which only pumps messages) under openbox on Xvfb: about 1 of 140 runs on fix/177, 20-40 % of the runs
with `WINEDEBUG=+synchronous`. Not seen without a window manager (the window must have had the focus: it needs a focused XIC).

## Cause (measured, except where marked)
- winex11 keeps one XIC per top-level X window, created with the XIM of the thread that owns the window, on that
  thread's display, at the window's first FocusIn or key event. `set_window_visual` destroys and recreates the X
  window in whatever thread calls it (UpdateLayeredWindow, SetLayeredWindowAttributes, WS_EX_LAYERED changes), and
  `destroy_whole_window` did `XUnsetICFocus( data->xic ); XDestroyIC( data->xic );` there.
- Xlib's local input method (no IM server) registers a KeyPress / KeyRelease filter per focused IC in
  `display->im_filters`. `XFilterEvent` (src/FilterEv.c), which the owner runs on every event it reads, walks that list
  inside the display lock; `_XUnregisterFilter` (src/RegstFlt.c), called by XUnsetICFocus and XDestroyIC, unlinks
  and frees the node with no lock. The owner reads the freed node's `next` (glibc's mangled tcache link) and faults:
  `XFilterEvent+0x88` / `+0xe3` of the distribution's libX11 are the `p->window` reads of its two loops.
- Wine side, counted with an instrumented libX11 (`inst/191/x11-build-instr`: reports every XIM call and filter list
  change made by a thread other than the one that filters the display's events): fix/177 makes about 500 cross-thread
  `XUnsetICFocus` + `XDestroyIC` (1000 unlocked `_XUnregisterFilter`) per 20 s gdistress run in synchronous mode, 2600
  per 15 s of `tests/r191/xicrace.exe race`; one of them was caught in the act (`X191: thread .. unregisters filter ..
  while thread .. is in XFilterEvent`, stack `NtUserUpdateLayeredWindow -> winex11 -> _XUnregisterFilter`). fix/191: 0.
- Why synchronous mode hits it so much more often (trace `inst/191/out/ab-syncxim-base-1803-s204.out`): the
  recreating thread's `XDestroyWindow` is then followed by a round trip; the UnmapNotify / FocusOut of the focused
  window wake the owner, which filters them exactly while the other thread goes on to the XIC. Without it the
  XDestroyWindow isn't even flushed before the XIC is gone.
- The fault happens in the owner's `NtUserPeekMessage` / `NtUserMsgWaitForMultipleObjectsEx` with the display mutex
  held; ntdll returns it as the syscall's status (172), nothing unlocks the display, and the owner's next
  `XCheckIfEvent` blocks on its own lock. 23 of 23 traced hangs here have exactly one `handle_syscall_fault`, at one
  of those two instructions (`inst/191/out/ab-sync*-base-*.out`, `par-xsync-base-*.out`), as had 7 of 7 of 177's.
- 177's note "only the distribution's libX11 binary faults" is explained: the own builds under inst/182 and inst/177
  were configured with a private prefix that has no locale data, so `XSupportsLocale()` fails and Wine runs without
  any input method on them (`warn:xim:xim_init X does not support locale`). A build with `--prefix=/usr`
  (`inst/191/x11-build-base`) creates XICs and faults like the package.
- libX11 (plain Xlib, `tests/r191/xfilter.c`): two threads on one display, one in XFilterEvent, the other only moving
  the focus between input contexts: SIGSEGV in XFilterEvent within a second, every time, distribution package and own
  build. With the proposed patch (lock in `_XUnregisterFilter`, `XFilterEvent` copies the filter before unlocking): ok.
  The patch does not make the cross-thread XDestroyIC safe: the filter runs unlocked with the IC as its argument
  (`xfilter 5 destroy match` still dies in `_XimLocalFilter`), so Wine has to keep the XIC in one thread whatever
  libX11 does.

## What 172 changes here
With 172 (today): no message at all; the process stops repainting and answering, every thread ends up behind the
owner's display mutex or win_data, and only a gdb dump shows a mutex that its owner is waiting for.
Without the swallowing (by reading, not tried): the same SIGSEGV would be raised as an access violation in the
window's thread with a Unix-side instruction pointer, i.e. the usual Wine crash (unhandled page fault, winedbg
backtrace ending in libX11's XFilterEvent) at the moment of the race instead of a hang some time later. The plain
Xlib reproducer shows the bare event: SIGSEGV. So 172 does not cause it, it turns a crash with a stack into a
silent hang.

## Fix (629120c8837)
Rule: a window's XIC belongs to the thread that owns the window (its XIM, its display); no other thread creates or
destroys it.
- `destroy_whole_window` called from another thread leaves the XIC alone and sets `data->xic_invalid`, next to the
  existing `parent_invalid` (same situation, same comment). In the owner thread it destroys the XIC as before.
- `X11DRV_get_ic` (the one place that hands out the XIC: FocusIn / FocusOut, KeyPress, ToUnicodeEx, NotifyIMEStatus)
  in the owner thread destroys a flagged XIC and creates the one for the current X window; so the owner replaces it
  at its next focus or key event for that window, which is when upstream recreated it too (the XIC was created
  lazily before as well). In any other thread it neither destroys nor creates: it returns what is there.
- `X11DRV_SetIMECompositionRect` treats a flagged XIC like no XIC.
- Window destruction and thread detach need nothing: `X11DRV_DestroyWindow` runs in the owner thread
  (`destroy_thread_windows` comes before `pThreadDetach`), which destroys the XIC flagged or not.
Considered and dropped:
- Keeping the XIC and pointing it at the new window: XNClientWindow can only be set once (libX11 imRm.c: mode
  `XIM_MODE_IC_ONCE`; XIM protocol: static attribute), so an XIC cannot follow a recreated window.
- Posting / sending the destruction to the owner: the owner may be blocked or exiting, the other thread would have
  to wait for it with win_data held, and nothing needs the XIC gone earlier than the owner's next use.
- A lock around XFilterEvent and the XIC calls: the filter runs input method callbacks (preedit, IM server round
  trips) for an unbounded time.
Until the owner replaces it the old XIC stays focused inside Xlib (local IM: its filter stays registered for the old
window id, which gets no more events; an IM server keeps the context of a window that no longer exists, until the
window is focused again or destroyed).

## Still reaching an XIC from a thread that doesn't own it (not changed)
- `X11DRV_ToUnicodeEx` in a thread whose focus window is a child of another thread's top-level calls
  `XmbLookupString` on that top-level's XIC (15 calls in the input script below, both builds). It only reads the IC
  and uses the IM's converters, but the owner can destroy the XIC under it (upstream race, as before). Returning no
  XIC there would lose every non-Latin-1 character in such windows (XLookupString only knows Latin-1).
- `X11DRV_SetIMECompositionRect` (XIMPreeditPosition styles only, i.e. `InputStyle=overthespot` with an IM server)
  calls XSetICValues from the caret owner's thread, which may differ from the top-level's. Not exercised.
- Wine's own statics in xim.c (`ime_comp_buf`, `input_style`) are process-wide and unlocked.

## Verification (base = wt/177-build fix/177 b1e7b97cfed, fix = wt/191-build fix/191 675c332fddc, alternating runs, Xvfb + openbox)
| what | base | fix |
|---|---|---|
| gdistress 20 6 SEED, `+synchronous` | 7 hangs of 40 (10 of 50 with base-only batches) | 0 of 40 |
| gdistress 20 6 SEED, normal | 0 hangs of 40 | 0 of 40 |
| `xicrace.exe race 30 gap=2`, `+synchronous` | 10 hangs of 16 (13 of 24 with a base-only batch) | 0 of 16 |
| `xicrace.exe race 30 gap=2`, normal | 7 hangs of 16 | 0 of 16 |
| cross-thread XIM calls per run (instrumented libX11): gdistress sync / xicrace sync | 488, 505 / 2607 XDestroyIC | 0, 0 / 0 |
All 30 base hangs classify as "selfowner" (inst/182/classify.py); the 23 that ran with `+seh` have exactly one
swallowed fault each, at XFilterEvent+0x88 or +0xe3. No hang dump of the fix build exists for these batches.
`xicrace race` (tests/r191/xicrace.c): the owner only pumps; another thread recreates its focused window every 2 ms
(170-270 recreations per second, each with a focused XIC again: openbox focuses the new window; the pause lets the
owner go idle, so that it is woken by the events of the next destruction). It hangs the base build in about half
of the 30 s runs also without synchronous mode, where gdistress needs about 140 runs. No probe of mine forces the
overlap (the owner has to be inside the few instructions of the list walk; a delay inside the walk makes it rarer,
because the walker then holds the display lock that the other thread needs just before); the count of cross-thread
XIM calls is the deterministic check.
Input method behaviour (`tests/r191/xicrace-ui.sh`: xdotool keys into edit controls of two top-levels and of a
top-level whose edit belongs to another thread; plain keys, dead keys and compose sequences on `us(intl)` with
compose:menu, Cyrillic layout; before / after recreation by another thread and by the owner, typing into B while A
is recreated, focus changes A <-> B):
- Xlib's local input method (`XMODIFIERS=@im=none`): 15 steps, base and fix identical, all characters right (é, ©,
  á, è, í, ü, ó, ú, Cyrillic), also right after a recreation.
- ibus 1.5.34 XIM server (`XMODIFIERS=@im=ibus`, ibus-daemon --xim on a private D-Bus session, no engine
  configured): base and fix identical; plain and Cyrillic text right, dead keys / compose produce nothing on either
  build in that setup (so composition through an IM server is not tested).
- ibus stopped and started again under the running probe (`inst/191/ui-ibus-restart.sh`): base and fix identical
  (4 xim_destroy, 2 xic_destroy, 4 xim_open callbacks, typing goes on; then a recreation by another thread).
Not tested: fcitx / other IM servers (not installed), preedit (CJK engines), the `overthespot` / `offthespot` /
`root` input styles, a recreation in the middle of a composition.
Conformance units (`tools/regress.sh unit`, both arches, fix build vs the integ baseline
`deps/regress/cffd27540ee*-h26`): user32:input pass (todo 143 / 138), user32:msg fail 1 / fail 1, user32:edit pass,
imm32:imm32 pass / fail 1 (x86_64), comctl32:edit pass: all equal to the baseline. user32:win differs from the
baseline (i386 fail 1, x86_64 pass) but not between the builds: 3 runs per arch on wt/177-build and on wt/191-build
give the same on both, i386 1 run of 3 and x86_64 3 of 3 with `fail 4` at `win.c:11003` / `11004` (transparent window
hit test). So that failure comes with fix/177 or with the host's state, not with this branch.

## Repro
```
inst/191/x.sh start                                   # Xvfb :1800-:1801 (no WM), :1802-:1809 (openbox)
BUILDS=base WINEDEBUG=err+all,+seh,+synchronous inst/191/ab.sh 1804 sync 10 20 6       # gdistress, base = wt/177-build
WINEDEBUG=err+all,+seh,+synchronous inst/191/par.sh xsync 8 80 "1804 1805" tests/r191/xicrace.exe race 30 gap=2
python3 inst/182/classify.py inst/191/out/*-hang.txt                                    # "selfowner"
X11LIB=$PWD/inst/191/x11-build-instr/src/.libs inst/191/run.sh base 1806 TAG 60 tests/r191/xicrace.exe race 15 gap=2
grep -c X191OWNER inst/191/out/TAG.out                # cross-thread XIM calls
DISPLAY=:1800 XMODIFIERS=@im=none tests/r191/xfilter 5                                  # plain Xlib
XMODIFIERS=@im=none tests/r191/xicrace-ui.sh $PWD/wt/191-build/wine 1806 $PWD/inst/191/pfx-fix-1806 inst/191/out/ui
```

## Review (2026-10-05, inst/191-review/) — merge after fixes, done
- The ToUnicodeEx path above is NOT "as before": with the fix alone the owner frees a flagged XIC within microseconds
  of taking the window data lock (base: the destroyer spends an XSync round trip under the lock first), so a non-owner
  thread still inside XmbLookupString hits the freed IC far more easily. It faults at libX11 `XmbLookupString+0xe`
  with kbd_mutex held, the fault is swallowed (172), and keyboard input is dead in the whole process.
  `inst/191-review/probe/xthr.exe 20 gap=2 tu=200` (thread B polls ToUnicode on a child of thread A's top-level, a
  third thread recreates every 2 ms, openbox): base 5 hangs of 16, fix alone 26 of 32, fix + follow-up 0 of 16.
  With realistic typing (xdotool, 25 s) the fix alone is already better than base: swallowed faults in 11 of 12 runs
  at a 2 ms recreation gap on base, 0 of 12 on fix and on fix + follow-up.
- Follow-up 48676c8af1c: X11DRV_ToUnicodeEx keeps the window data locked across its lookups and reads data->xic
  under the lock (new lock edge win_data → kbd_mutex; no kbd_mutex section takes win_data). Also covers IM-server
  death (the xic_destroy callback takes win_data before Xlib frees the IC).
- Low, by design: a non-owner no longer creates an XIC. If the owner's top-level has none (no XIM, or keys injected
  into a window that never had X focus) the non-owner translates with XLookupString: Latin-1, dead keys, Unicode
  keysyms and Euro still work, legacy non-Latin-1 keysyms (Cyrillic, Greek) don't. Base created an XIC on the wrong
  display there and left a dangling pointer.
- Pre-existing, unchanged: X11DRV_SetIMECompositionRect from a non-owner does a synchronous XSetICValues on the
  owner's display (over-the-spot with an IM server only, by reading); get_ic returns before updating last_xic_hwnd
  when the window is gone; owner and non-owner can run XmbLookupString on one IC concurrently.
- Checked sound: flag and data->xic always under win_data_mutex; destroyed-while-flagged, recreated twice, use_xim
  off, IM server killed and restarted with flagged XICs; cross-thread XDestroyIC count 1875 → 0; gdistress
  +synchronous 5 hangs of 40 → 0; xicrace 6 and 4 of 12 → 0; vstate 0 of 360 moved on both; typing (local IM, ibus)
  identical. Not verified: fcitx, CJK preedit, other preedit styles, WMs other than openbox.
- Upstream nits (not applied): keep the XFlush / NtUserRemoveProp lines of destroy_whole_window in place for a
  smaller diff; xim.c line over 100 columns; "never create from another thread" could be its own commit.
- libX11 report: correct, still in master; search for existing reports before filing.

