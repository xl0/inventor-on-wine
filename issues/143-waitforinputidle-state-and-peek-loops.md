# 143 WaitForInputIdle: idle is sticky on Wine (Windows tracks one thread), PeekMessage polling loops never count
Status: draft · Found in: issue 133 ground truth (tests/r133/idle.c) · no app known to need it

## Symptom
Black-box differences between Windows 11 and Wine (fix/133) left after 133. Probe: `tests/r133/idle.exe [NAME..]`
(VM: `WINRUN_ID=x vm/winrun.sh tests/r133/idle.exe once state_ pl_ kind_peek timeouts`; outputs in inst/133/vm-*.txt,
Wine: inst/133/wine-fix.txt; inst/ is local, not in git). `i@T=RET(+DT)`: WaitForInputIdle called T ms after CreateProcess, returned RET after DT ms.
Child script tokens: w window, sN sleep, g GetMessage loop (gN: leaves it after N ms on a posted message), pN
PeekMessage(PM_REMOVE)+Sleep(1) loop, MN MsgWaitForMultipleObjects timeout N, k/K one PeekMessage NOREMOVE/REMOVE,
m message-only window, x ExitThread; `/` separates threads (first = main).

1. Not once-only. After the first idle Windows keeps tracking ONE thread, the one that went idle first
   (next idle thread once that one exits): WaitForInputIdle returns at once while that thread waits for messages (or
   after its wait ended without a message: MsgWait timeout, polling loop left), and waits when it has retrieved a
   message and is busy. Wine: the event is set once and never reset.

   | scenario (parent / child) | Windows 11 | Wine |
   |---|---|---|
   | once: `i5000,s500,i5000,s700,i5000` / `w,s500,g300,s2000,g` | +535, +1799 (until idle again), +0 | +608, +1, +1 |
   | once_called_late: `s1500,i5000` / `w,s500,g300,s3000,g` | +2338 | +1 |
   | state_msgwait_tmo: `i5000,s500,i5000` / `w,s500,M300,s2000,g` | +549, +0 | same |
   | state_other_thread: `s1500,i5000` / `w,s500,g300,s2500,g` + `s200,m,g` (helper idle first, stays idle) | +0 | +0 |
   | state_other_woke: `s1500,i5000` / `w,s500,g` + `s200,m,g300,s2500,g` (helper idle first, busy; main idle) | +1506..1528 (6/6 runs) | +0 |
   | state_main_first2: `s1500,i5000` / `w,g300,s2500,g` + `s500,m,g` (main idle first, busy; helper idle) | +1334..1370 (5 of 6 runs; one +0, unexplained) | +0 |
   | state_first_exits: first idle thread exits, second one busy | +1510 (until the second idles) | +0 |

   The documentation ("waits only once ... subsequent calls return immediately") is wrong for Windows 11.
   Who would notice (inferred, no app seen): callers that use it to wait for a busy app, e.g. UI automation.
2. PeekMessage polling loops. Windows: a thread that polls an empty queue with PeekMessage (no PM_NOYIELD) becomes
   idle: `w,p6000` +39 ms, helper thread polling +7 ms, main thread without a window ~1 s after process start
   (`p6000`: +1017). Wine: never (the probe returns when the process exits or the caller times out; only a retrieved
   WM_TIMER or a `hwnd == -1` peek counts), so an INFINITE wait on a pure PeekMessage loop would not return (inferred). Single peeks are time dependent on Windows
   (one empty PeekMessage on the main thread in the first ~second: no; later: yes; on a helper thread: yes at once;
   32- and 64-bit runs flip the borderline cases at 1000 ms), which is what user32:msg wait_idle cases 1, 2, 5, 6
   (peek 200 ms after start -> WAIT_TIMEOUT, still true on Windows 11) encode. Wine's `hwnd == -1` and WM_TIMER
   special cases come from the same tests. Not modelled further.
3. Timeouts are rounded up on Windows: 100 -> +108, 1000 -> +1528, 1500 -> +2016, 2000 -> +2511, 3000 -> +3520
   (timeout + ~500 ms from 1000 on). Wine: exact. Harmless.
4. Wine only: `peek_message` and `wait_objects` read `thread_info->idle_event`, which is fetched with the queue handle
   at the thread's first message wait, so the `hwnd == -1` / WM_TIMER idle paths do nothing in a thread that has
   never waited.

## Task (if an app needs it)
1 needs the idle state per thread on the server (who set it, reset when that thread gets a message): the client sets
the event today (f4fd7a20698, needed for in-process sync waits), so a reset per retrieved message would cost a server
call unless it rides on get_message. Probably a protocol change. 2 needs a rule for "polling" that keeps the
user32:msg cases passing.
