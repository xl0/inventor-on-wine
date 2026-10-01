# 101 user32:input flaky: X_UnmapWindow BadWindow crash + SendInput returning 0 under load
Status: fixed (fix/101-user32-input, 3 commits on integ 4f92c92ace1) · Owner: worker-101 · Found in: 100 triage

Rates from 100: i386 10/30 bad in full regress runs (7 crash, 3 fail), x86_64 6/30; also on master.
Three independent causes, one commit each (wt/101, build wt/101-build):

1. `server: Don't count an exiting thread of the desktop owner as running.` (crash)
2. `win32u: Move the host cursor when ClipCursor moves the cursor.` (+ test) ("got pos (49,51)")
3. `user32/tests: Keep the main window over the other desktop's window in rawinput test 16.`
   (mouse_event / "SendInput returned 0")

## 1. Crash: X_UnmapWindow BadWindow on 0x400001
0x400001 = winex11's cursor clip window, created by the desktop thread of the first explorer on the
display (its X connection is client 2). Every thread caches it and unmaps it on WM_WINE_CLIPCURSOR
(sent on each foreground change); the test's main thread first does that right after input.c:5121.
Full regress run with debug prints (server close-timeout arm/fire, winex11 create/cache/unmap):
the default desktop's explorer (left over from the shard's previous unit) had 5 threads; the input
unit's main thread attached; one explorer thread exited -> `remove_desktop_user` saw users 5
(4 explorer + 1 test thread) == explorer running_threads 5 (the exiting thread is removed from its
process only after cleanup_thread) -> close timeout -> WM_CLOSE -> explorer exited mid-test ->
next unmap of the dead clip window -> Xlib default handler killed the test.
Needs exactly one foreign thread attached when an explorer thread exits: only at a unit's start,
right after the previous unit, so never seen in isolated runs.
Deterministic repro: `tests/desktop_owner_thread.c` (CreateRemoteThread(Sleep) into the desktop
owner, wait 2 s): unfixed explorer exits in round 0, fixed survives 3 rounds; prefix still shuts
down 4 s after the last process (1 s close + 3 s persist). Windows: csrss owns the desktop
window, OpenProcess fails (exit 2) - not testable there, no conformance test.

## 2. "got pos (49,51)" in test_SetCursorPos (input.c ~6400-6430), ~1/30 idle, ~1/30 loaded
SetCursorPos(49,51); ClipCursor(50,50,51,51): the server moves its cursor to (50,50) but the X
pointer stays at (49,51) (the child has no focused window, so winex11 doesn't grab/confine).
NtUserGetCursorPos asks the driver once the server position is >100 ms old -> (49,51).
`tests/clipcursor_warp.c`: Win11 VM reports (50,50) at 0/150/300 ms; unfixed Wine (49,51) from
150 ms on; fixed (50,50). Fix: NtUserClipCursor calls pSetCursorPos when set_cursor moved the
cursor (like NtUserSetCursorPos). Test: Sleep(150) after that ClipCursor in test_SetCursorPos
(fails 100 % without the fix, passes on Win11 VM i386+x86_64).

## 3. mouse_event / SendInput fail (input.c:1879-1901, 4004), ~25 % under 100 spinners
The input desktop is left on `rawinput_test_desktop` after test_rawinput. Case 16: the child's
TOPMOST window over the desk thread's window is destroyed; X sends EnterNotify to the desk
thread's window, winex11 sends it as hardware input with that hwnd and the server (496eed7aafd)
makes the window's desktop the input desktop. Under load the desk thread handles it before
destroying its window, so this switch is the last one; injected input of the main thread then
fails with ACCESS_DENIED. Tests 14/15 already recreate the main window TOPMOST for exactly this
(comment in the test); 16 now does too. Wine-side the real difference is that hidden desktops'
windows are visible on the X screen (design; not changed).

## Measurements (/tmp/u101 harness, own Xvfb + prefix per stream, 100 spinners = "load")
- Before (integ 4f92c92ace1): i386 load 7/30 bad, idle 1/30; x86_64 load 14/60.
  Interleaved with HEAD under the same load: base 17/80 bad, HEAD 0/80 (x86_64).
- After: i386 load 0/60 + idle 0/40; x86_64 load 0/80 (interleaved) and 10/180 in other batches,
  all of them input.c:4453 (below); idle 0/40.
- Full regress runs: before: crash in reg2 (my debug build) and in another worker's base run
  /tmp/claude-1000/rg/n1 (x86_64 input, same serial 734); after (fin2, fin3, new harness): input
  passes on both arches, no X error in any log, no unit worse than both base runs n1/n2.
- Remaining, not fixed: input.c:4452/4453 "button_down_hwnd missing" (click through a
  HTTRANSPARENT window of an attached thread, `wait_messages( 5 )`), ~2-5 % under heavy load,
  also on base (1/80 interleaved). Timing assumption in the test (5 ms for a cross-thread
  WM_NCHITTEST round trip).

## Tests
- VM (Win11): user32_test input with the new test lines, x86_64 21 / i386 20 failures, none near
  the changed lines (pre-existing: 754/755 LL hook rshift, 2612/2673, 5102, 5753/5769).
- Wine: input passes 0 failures idle and under load (see above).

## Harness (scratch, /tmp/u101; prefixes in /dev/shm/u101)
- `run.sh DISP ARCH N OUT [BUILD]` (TPL= for another template), `burn.sh start 100|stop`,
  `seq.sh` (all user32 units shuffled), `race.sh`, `probe.sh`; debug prints: /tmp/u101/debug.patch.
- A unit's log also gets wineserver's and explorer's stderr (whoever started them), so server
  debug lines land in some earlier unit's log in regress runs; match by pids + desktop pointer.
