# 133 WaitForInputIdle returns too early: Wine-internal message waits make the process input idle (winewayland clipboard thread, WaitForInputIdle itself); Inventor DWG/DXF export fails on Wayland
Status: fixed on fix/133 (3 commits, not merged; Wayland run not done) · Branch: fix/133 (wt/133) · Found in: wayland test pass (notes/wine/wayland.md)

## Symptom
invscen on Wayland: `drawing2` "export DWG" fails with REGDB_E_CLASSNOTREG (0x80040154) and `sheetmetal` "export flat
pattern DXF" with E_FAIL; both pass on X with the same build/prefix. DwgTrans.dll starts
`DBXBridge.exe /ParentProcessIdentity=...`, calls WaitForInputIdle, then `CoGetClassObject({b472e45c-...},
CLSCTX_LOCAL_SERVER)`; on Wayland the wait returned after ~60 ms, before the bridge had registered its class
(~240 ms), and Inventor terminated the bridge (timeline: inst/wayland/dbxbridge-timeline-*.txt).

## Windows ground truth (Win11 VM, `tests/r133/idle.c`; 78 scenarios; 32-bit and a 64-bit parent with a 32-bit
child agree, except two borderline single-peek cases)
The original guess ("Windows only counts the initial thread, Wine counts any thread") is WRONG: Windows counts any
thread, like Wine. user32:msg `wait_idle` case 13 already said so (and Wine commit 7d4e28480d0, 2009).
`i@T=RET(+DT)` = called T ms after CreateProcess, returned after DT ms. Child script: threads separated by `/`
(first = main); w window, m message-only window, sN sleep, g GetMessage loop, MN MsgWait, WN WaitMessage, x ExitThread,
e ExitProcess, k one PeekMessage(PM_NOREMOVE), IN WaitForInputIdle on a grandchild that never idles.

| question | child | Windows 11 | Wine fix/133 |
|---|---|---|---|
| main thread busy 1.5 s, then GetMessage | `w,s1500,g` | 0 after +1570 | +1578 |
| same + helper thread pumping from the start (message-only / visible / no window) | `w,s1500,g/m,g`, `/w,g`, `/g` | 0 after +25 / +52 / +12 | +55 / +77 / +35 |
| helper only waits in MsgWaitForMultipleObjects | `w,s1500,g/M4000` | +13 | +37 |
| helper idles later than the main thread became GUI | `w,s1500,g/s500,m,g` | +526 | +551 |
| helper is the first GUI thread and busy, main idles first | `s500,w,g/w,s2500,g` | +530 | +556 |
| main thread never a GUI thread, helper pumps | `s4000/s500,w,g` | +573 | +596 |
| main thread exits, helper idles later | `s500,x/s1500,w,g` | +1568 | +1586 |
| two helpers, the second idles first | `s4000/s500,w,s1500,g/s1000,w,g` | +1039 | +1075 |
| GUI process, no thread ever waits (window or not) | `s500,w,s4000` | WAIT_TIMEOUT | WAIT_TIMEOUT |
| exits before idle | `w,s1000,e` | 0 at exit (+1063) | +1082 |
| after exit | | WAIT_FAILED | WAIT_FAILED |
| console subsystem child (no GUI / window + GetMessage / GUI helper) | | WAIT_FAILED at once | same |
| wait kinds: GetMessage, MsgWaitForMultipleObjects, WaitMessage (with and without a window) | | idle | idle |
| SendMessage to another process (waiting for the reply) | `w,s1000,S500,s2000` | not idle (0 at exit) | same |
| waiting in WaitForInputIdle on another process | `s1000,I1000,s2000` | not idle (0 at exit, +4550) | +4107 (was idle, +1060) |
| PeekMessage polling loop, single empty peeks | | idle (time dependent) | never: issue 143 |
| second call when the app is busy again | | waits again (not once-only) | returns at once: issue 143 |
| timeout N >= 1000 | | returns after N + ~500 ms | N: issue 143 |

Full outputs: inst/133/vm-all64.txt, vm-all32.txt, wine-base.txt (before), wine-fix.txt (after) (local, not in git).
Of the 77 scenarios with a Windows result, Wine fix/133 matches 35; 20 differ only by the +500 ms timeout rounding,
18 are PeekMessage cases and 4 are the state tracking of later calls (143). One Windows result is not reproducible
(state_main_first2, see 143); `helper_nowin_getmsg` once returned WAIT_FAILED on a slow child start (1 of 4 runs,
not understood).

The X number in the first report (child sleeps 1.5 s, Wine/X returned after 4611 ms): the old `tests/wl_idle.c`
child pumped with PeekMessage, which never makes a Wine process idle; the wait ended on the process handle when the
child exited (1.5 s + 3 s). Windows returns after +1558 there (polling counts, 143). wl_idle.c now uses GetMessage.

## Cause
Wine has one idle event per process, set by any thread that waits for messages (correct). Two Wine-internal waits
set it too:
1. winewayland starts a clipboard manager thread (GetMessage loop) in every process when the compositor lacks
   zwlr_data_control (mutter: always). Windows has no such thread in the application.
2. `NtUserWaitForInputIdle` waits with a message wait, so a process waiting for another process was itself idle.
   `get_desktop_window` does that when it starts explorer: the first window of the first GUI process of a session
   made that process idle (repro: `tests/wl_idle.exe` right after `wineserver -k`: 59 ms instead of ~1500+).

## Fix (fix/133, on integ d18a5dcd1ef; no server protocol change)
- f6849b150a1 user32/tests: the WaitForInputIdle tests never tested anything: the test exe is a console program and
  WAIT_FAILED was accepted for console children. They now run from a GUI-subsystem copy of the exe, plus a console
  child check. All 21 old cases pass on Win11 and Wine unchanged (16 and 20 return the `broken` value on Win11).
- 344a83186de win32u: a thread that dispatches a clipboard manager window message (`NtUserClipboardWindowProc`:
  winewayland's per-process thread, explorer's thread for the other drivers) drops its idle event handle
  (`disable_thread_input_idle`). Not testable on Windows.
- 49300e6a97f win32u: the idle event is set in `wait_objects` (GetMessage, MsgWait, WaitMessage) instead of
  `wait_message`; `NtUserWaitForInputIdle` calls `wait_message` directly. Test: wait_idle case 21 (fails without
  the fix: `21: WaitForInputIdle error 00000000 expected 00000102`).

## Verification
- X stand-in for the Wayland thread: `tests/r133/idle.exe wine_` (helper thread whose window proc goes through
  `NtUserMessageCall(NtUserClipboardWindowProc)`): `w,s1500,g/c,g` +61 ms before, +1598..1621 after.
  NOT run on Wayland (the session belongs to the 134/135 worker). To check there: `tests/r133/idle.exe main_getmsg
  helper_win_getmsg` (expect ~+1600 and < +200) or `tests/wl_idle.exe` (~1500; cold server ~2100) and invscen
  drawing2 / sheetmetal.
- VM: user32_test msg, 64-bit and 32-bit: no failure in test_WaitForInputIdle (the runs have 11 and 7 unrelated
  failures elsewhere in msg.c: mouse/paint/hotkey tests).
- `tools/regress.sh unit`, 2 runs per arch: user32:msg 1 failure (baseline 1), user32:win 4 (4), user32:input 0,
  win32u:win32u 0, user32:clipboard 0, both arches.
- Inventor on inv4 (X): suite 12/13, `export` fails only the known IGES 80-column step (user name left as is);
  drawing2 14/14, sheetmetal 9/9. Start to connect, prefix restarted each time: build/ 20.5 s, 20.6 s; fix 20.5 s, 21.0 s.

## Limits
- The Wayland symptom itself is unverified; the argument is that the clipboard thread was the only thread of
  DBXBridge idling early and the X stand-in shows it no longer counts.
- Keying on `NtUserClipboardWindowProc` is implicit; it also changes explorer (its WaitForInputIdle in
  get_desktop_window now ends on another explorer thread). Other Wine-internal pumping threads (dinput, winmm devices,
  combase apartment host) still count; they only start when the app uses those APIs.
- Not matched: 143 (state after the first idle, PeekMessage polling, timeout rounding).
