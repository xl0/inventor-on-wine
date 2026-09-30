# 093 Absolute wait timeouts expire up to ~1 ms early (coarse client clock)
Status: fixed (awaiting review) · Owner: 093 worker · Branch: fix/093-abs-timeout (wt/093 on integ 1d006ebdafb, build wt/093-build) · Found in: 089 worker (wt/089, integ 481a8f5f6ab)

## Symptom
NtWaitForSingleObject with an absolute timeout 1 ms in the future returns after ~0.05 ms on Wine.
The client converts absolute NT times using a coarse clock (CLOCK_REALTIME_COARSE, ~1-4 ms
granularity) while the server compares against a precise clock. Same root cause as the MsgWait
early return fixed in 088 (281e8530a1e → relative remaining time).

## Windows ground truth (Win11 VM, tests/abs_timeout.c; global resolution 1 ms from another process)
Deadline = NtQuerySystemTime()/GetSystemTimeAsFileTime ("system") or GetSystemTimePreciseAsFileTime
("precise") + offset; NtWaitForSingleObject, NtDelayExecution, waitable timer, HIGH_RES timer.
- Never returns before the precise time or NtQuerySystemTime() has reached the deadline (0 of 1920).
- Default resolution: +1/+5 ms -> next 15.6 ms tick (mean ~15.5), +20 -> ~31. period 1: +1 -> 1.5
  (system) / 1.9 (precise), +5 -> 5.5 / 5.9, +20 -> 20.4 / 20.7. HIGH_RES timers exact.
- Due already passed (system +0, -1 ms, -1 s): immediate for waits and timers, any resolution.
- precise +0 waits for the next tick (15.3 default, ~1 ms period 1): Windows compares with its
  tick-updated system time, which lags the precise one.
- Wall-clock step during a 1 s absolute wait (SetSystemTime, restored): +2 s -> returns at once
  (~210 ms), -2 s -> 3.0 s. All four kinds; absolute deadlines follow the wall clock.

## Cause
- NtQuerySystemTime read CLOCK_REALTIME_COARSE, measured ~1 ms behind CLOCK_REALTIME here (HZ=1000);
  the server expires absolute timeouts on gettimeofday. With WINE_TIMER_RESOLUTION=0 or a raised
  resolution: system +1 ms returned after 0.26 ms, +5 after 3.96, and NtQuerySystemTime() after the
  timeout was still before the deadline in up to 20/20 waits (NtWait, timers). NtDelayExecution's own
  loop was consistent with itself but precise-based deadlines ended early by the coarse clock.
  With 089 rounding (default) the futures were masked by the tick.
- 089 rounding sent deadlines due since the last tick to the next one: absolute timers in the past
  took 15.5 ms (Windows 0), NtDelayExecution(system +0) 15.6, NtWait(-1 ms) sometimes 14.
- Not early on base: relative futex waits (SleepConditionVariableSRW/CS 1 ms under period 1, min 1.02).
  (Windows returns those early: min 0.68 ms, 30/200 under 1 ms.)

## Fix (fix/093-abs-timeout, 2 commits on integ 1d006ebdafb)
- `ntdll: Use the precise clock in NtQuerySystemTime().` (calls system_time_precise). Cost:
  GetSystemTimeAsFileTime 65 -> 82 ns x86_64 (i386 ~170 ns, dominated by the thunk); Windows 2.4 ns
  (reads USD). Also fixes the intermittent ntdll:time "USD SystemTime / NtQuerySystemTime are out
  of order" (t2 <= t3) failure.
- `ntdll: Don't round absolute timeouts that are already due up to the next tick.` round_timeout
  and server get_timer_timeout: `when <= now` instead of `<= now - res`.
- No protocol change. 089 semantics kept (absolute stays absolute, tick phase monotonic).
- Left: precise +0 (due between Windows' last tick and now) is immediate on Wine, next tick on
  Windows. Wall-clock steps not tested on Wine (can't step the host clock).

## After (Wine, tests/abs_timeout.c x86_64 + i386, default, period 1, WINE_TIMER_RESOLUTION=0)
0 early by either clock in all 3x96 rows; past due immediate everywhere; futures: default tick
(15.3-15.6 / 31.2), period 1 +N -> N + 0.05-0.1 ms.

## Tests
ntdll:time test_absolute_timeout: 60 absolute waits/delays/timers 0.5-2 ms ahead (raised, then
default resolution), NtQuerySystemTime() after >= due; 8x already-due wait+delay+timer mean < 5 ms.
Base Wine: "returned 500..1500 us early" x20+, "due timeouts took 15.62 ms". Fixed: 0 failures x86_64
+ i386 (3 runs each). VM x86_64 + i386: new checks pass; only the known intermittent 305/310/334
(global resolution state) fail.
Regress (ntdll kernel32 kernelbase user32 winmm ws2_32 rpcrt4, 218 units) vs deps/regress/1d006ebdafb:
0 REAL, 2 FLAKY (x86_64 kernel32:debugger, user32:msg; same on base re-runs).
