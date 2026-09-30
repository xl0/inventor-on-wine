# 089 Short waits take 1 ms on Wine, ~15.6 ms on Windows 11 (polling loops spin 15x more)
Status: fixed (awaiting review) · Owner: 089 worker · Branch: fix/089-timer-resolution (wt/089 on integ 481a8f5f6ab, build wt/089-build) · Found in: 088 (inv3, integ 9c1eea5beac)

## Symptom
AdskIdentityManager (idle, Inventor at Home) takes 11-25 % of a core and ~4400 wineserver
requests/s on Wine (VM: ~4 %). Five "Request: HeartBeat/IsLoggedIn" threads each loop
~950 times/s: non-blocking recv, then Boost.Thread interruptible_wait (IdIPCServer.dll:
CreateWaitableTimer + SetWaitableTimerEx(-1 ms) + WaitForMultipleObjects + CloseHandle).

## Windows ground truth (Win11 VM, tests/wait_granularity.c [mode])
- Process without its own request: every timed wait and timer expires on the 15.625 ms tick:
  Sleep, SleepEx alertable, WaitForSingle/MultipleObjects, MsgWait, SleepConditionVariableSRW,
  WaitOnAddress, NtWaitForSingleObject and NtDelayExecution with absolute timeouts, waitable
  timers (relative and absolute). NtQueryTimerResolution meanwhile says cur 1 ms (global value).
- Rounding = first tick at or after start + timeout (not a minimum duration): from a random phase
  (`phase`) 1 ms waits take 0.4..16 ms (mean ~9), 5 ms waits 4..21 (mean ~12.5); wake-ups cluster
  on one tick phase. Back-to-back loops (`loops`): 1..15 ms -> 15.6, 16/20 -> 31.25, 32 -> 46.9,
  50 -> 62.5. Zero timeouts return at once; an absolute timer due "now" waits for the next tick.
- CREATE_WAITABLE_TIMER_HIGH_RESOLUTION timers are exempt (1.3 ms, uniform phase).
- Any request of the process (NtSetTimerResolution(x, TRUE) with any x, even 20 ms, or
  timeBeginPeriod(1..15)) -> ~1 ms (the global resolution). timeBeginPeriod(16+) returns 0, no effect.
- NtSetTimerResolution: one flag per process (set twice, clear once; clear with any value; clear when
  not set -> STATUS_TIMER_RESOLUTION_NOT_SET). timeBeginPeriod/timeEndPeriod count per period and
  set/clear that same flag on the first/last request: Nt clear after timeBeginPeriod drops the raise;
  the next timeEndPeriod still returns 0, an unbalanced one (or a period never begun) TIMERR_NOCANDO.
- A resolution change doesn't affect running waits (50 ms wait + timeBeginPeriod after 20 ms: 62.5;
  45 ms wait under period 1 + timeEndPeriod: 45.2); a waitable timer keeps the resolution of the
  SetWaitableTimer call (`running`).
- Periodic timers (`periodic`): 1 and 5 ms periods fire once per tick (33/500 ms), 20 ms keeps its
  average (25/500 ms, intervals 15.6/15.6/31.25); HIGH_RES and period 1 exact.
- Not bound to the process resolution (`pool`, `mmtimer`, qclock probe): timeSetEvent callbacks
  (precise; timeSetEvent also raises the process resolution for (1,1), (5,0), (10,10) but not
  (1,10) -- not modelled), quartz system clock AdvisePeriodic. Always tick-rounded even with
  timeBeginPeriod(1): threadpool timers, timer queue timers, RegisterWaitForSingleObject timeouts,
  WM_TIMER (10 ms -> 15.6, 20 -> 31.25).
- NtCreateTimer2(handle, NULL, attr, attributes, access): EX_TIMER_HIGH_RESOLUTION 0x4,
  EX_TIMER_NO_WAKE 0x8 accepted, EX_TIMER_NOTIFICATION 0x80000000 = manual reset; 0x2 ->
  STATUS_INVALID_PARAMETER, other bits STATUS_INVALID_PARAMETER_4.
- timeGetDevCaps max 1000000 on Win11 (Wine 65535); timeBeginPeriod(65536) = 0 (Wine 97). Left alone.

## Design (fix/089-timer-resolution)
- ntdll: `get_timer_resolution()` = 156250 unless the process holds an NtSetTimerResolution
  request or the thread is exempt; `round_timeout()` turns a timeout into a relative one ending on
  the next multiple of the tick on the monotonic clock (CLOCK_BOOTTIME, same as the server), 0 if
  already due. Applied at the Nt entry points only (not idempotent): NtWaitForSingleObject,
  NtWaitForMultipleObjects, NtSignalAndWaitForSingleObject, NtDelayExecution, NtWait/ReleaseKeyedEvent,
  NtWaitForAlertByThreadId (futex/kqueue paths). Covers win32u MsgWait (re-waits round to the same
  tick), SRW/CS/condvars/WaitOnAddress, IOCP (NtRemoveIoCompletion waits via NtWaitForSingleObject),
  ntsync and server waits.
- Timers: server rounds each expiration to the tick passed with set_timer (ntdll sends its current
  resolution), unless the timer was created high resolution; periodic timers that fell behind fire
  once per tick, `when` stays unrounded so the average period is kept. NtCreateTimer2 added
  (kernelbase uses it for CREATE_WAITABLE_TIMER_HIGH_RESOLUTION).
- Protocol change: create_timer.high_res, set_timer.resolution (version 967 -> 968).
- kernel32 timeBeginPeriod/timeEndPeriod: per-period counts (1..15), NtSetTimerResolution on the
  first/last request.
- Exempt threads (keep Wine's precise waits): system threads (PsCreateSystemThread: the unix audio
  timer loops of winealsa/winepulse/wineoss/winecoreaudio), and via the Wine-private
  `ThreadWineHighResolutionTimers` class: winmm timer thread, dsound mixer + capture, quartz clock,
  evr presenter, dcomp composition thread, wined3d CS thread, DwmFlush (on the caller's thread,
  set/cleared around the delay). ntoskrnl (winedevice) holds a process request (kernel timers, e.g.
  hidclass polling). winex11 selection polling uses usleep (was 500 x NtDelayExecution(1 ms)).
- Knob: env var `WINE_TIMER_RESOLUTION` = tick in 100 ns units (default 156250); 0 = off (old behaviour).
- Known differences left: threadpool/timer-queue timers and RegisterWait timeouts follow the process
  resolution (Windows: always tick); WM_TIMER not rounded (Windows: tick); timeSetEvent doesn't raise
  the process resolution; NtQueryTimerResolution still reports cur 1 ms.
- Pre-existing, not fixed: NtWaitForSingleObject with an absolute timeout 1 ms ahead returns at once
  under period 1 (client computes it from CLOCK_REALTIME_COARSE, server compares with gettimeofday).

## Tests
- ntdll:time test_timer_rounding (NtDelayExecution, NtCreateTimer, NtCreateTimer2 attributes /
  high res / manual, NtSetTimerResolution raise); kernel32:sync test_timer_resolution (Sleep,
  HIGH_RES timer, timeBeginPeriod counting and link to NtSetTimerResolution). Bounds: >= 10 ms mean of
  8 waits without a request (broken() before Win10 2004), <= 8 ms with one.
- VM: both pass x86_64 + i386 (ntdll:time lines 304/309/334 fail intermittently on the VM on base too:
  global resolution state). Wine: both pass.

## Commits (fix/089-timer-resolution, 14 on integ 481a8f5f6ab)
NtCreateTimer2; kernelbase HIGH_RES via NtCreateTimer2; ThreadWineHighResolutionTimers class (+system
threads); exemptions winmm, dsound, quartz, evr, dcomp, wined3d, dwmapi, ntoskrnl.exe, winex11.drv;
kernel32 timeBeginPeriod; ntdll+server rounding (protocol 967 -> 968) with the tests and the man page.

## Results (inv4, Inventor idle at Home, trial popup + Assistant open, % of one core)
| | integ 481a8f5f6ab (2 samples, quiet host 2nd) | fix/089 (2 samples) | VM |
|---|---|---|---|
| AdskIdentityManager | 13.7 / 21.0 | 3.6 / 3.4 | 4.0 |
| its server writes (strace, 10 s) | 61.5k | 11.0k | - |
| wineserver | 12.7 / 18.0 | 7.8 / 7.7 | - |
| Inventor.exe | 1.6 / 2.5 | 1.8 / 1.8 | 2.8 |
Assistant WebView2 GPU+renderer unchanged (~33 %, 091).
- Suite `run.sh all` x2 per build: all PASS (hello's 2x2 '#32770' dialog on the first run after a
  fresh Inventor start happens on both builds). Sum of per-step minima: base 96.8 s, fix 87.8 s; no
  step slower (> 0.3 s and 1.3x); feat steps 1.6-2.1 -> 1.1-1.2 s (less idle load). Inventor.exe and
  its DLLs don't import timeBeginPeriod (only libcef/Qt WebEngine, rti.dll, gBaseC13.dll mention it).
- regress (52 modules, 624 units) vs deps/regress/481a8f5f6ab: 0 REAL, 2 FLAKY (i386 quartz:filtergraph,
  user32:win; same on base re-runs). Slower units: kernel32:sync +16 s (8 -> 24), ws2_32:sock +28 s
  (42 -> 70), user32:win +8, quartz:filtergraph +8 (tests' timed waits now tick-rounded, as on Windows).
