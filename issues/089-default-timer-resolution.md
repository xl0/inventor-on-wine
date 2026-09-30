# 089 Short waits take 1 ms on Wine, ~15.6 ms on Windows 11 (polling loops spin 15x more)
Status: open (draft) · Owner: - · Branch: - · Found in: 088 (inv3, integ 9c1eea5beac)

## Symptom
AdskIdentityManager (idle, Inventor at Home) takes 11-25 % of a core and ~4400 wineserver
requests/s on Wine (VM: ~4 %). Five "Request: HeartBeat/IsLoggedIn" threads each loop
~950 times/s: non-blocking recv, then Boost.Thread interruptible_wait (IdIPCServer.dll:
CreateWaitableTimer + SetWaitableTimerEx(-1 ms) + WaitForMultipleObjects + CloseHandle).

## Windows ground truth (tests/wait_granularity.c, Win11 VM)
A process that didn't call timeBeginPeriod: Sleep(1), WaitForSingleObject(ev, 1),
MsgWaitForMultipleObjects(1), SleepConditionVariableSRW(1), a 1 ms waitable timer: 15.5-15.7 ms,
even though NtQueryTimerResolution reports cur = 1 ms (another process raised it; since
Win10 2004 that no longer applies to other processes). CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
timer: 1.6 ms. After timeBeginPeriod(1): all ~1.5 ms.
Wine: everything ~1.05 ms regardless; NtSetTimerResolution is a semi-stub that claims 1 ms.

## Task / design sketch
Per-process timer resolution: default 15.625 ms, lowered by NtSetTimerResolution
(timeBeginPeriod) of that process; HIGH_RESOLUTION timers exempt. Timeouts of waits,
NtDelayExecution and waitable timers expire on the next tick of that resolution.
Needs a decision first: long-standing Wine behaviour (1 ms), games/apps that Sleep(1)
without timeBeginPeriod would slow down exactly as on Windows. Touches ntdll (delay, waits,
inproc sync), server timers (protocol change for the per-process resolution).
Expected win here: IdentityManager ~15x fewer wakeups (~20 % of a core + wineserver share).
