# 105: kernel32:debugger wow64 "ntdll after breakpoint" (x86_64 build)

Status: fixed on fix/105 (wt/105: cab481e280e fix, e57f8e74d0c optional, on integ c3bcda7c5e4). Awaiting merge.

## Symptom
From 100 row 9: test_debug_loop_wow64 (64-bit debugger, 32-bit msinfo32 child,
DEBUG_PROCESS) failed debugger.c:1188 "loaddll on ntdll should appear before exception"
and :1257 "Expecting two ntdll instances" always in isolation, ~5/30 in full regress runs.

## Windows order (Win11 VM, tests/dbg_wow64_order.exe)
CREATE_PROCESS msinfo32, LOAD_DLL ntdll (64), ntdll (32), wow64, wow64base, wow64win,
wow64con, EXCEPTION 80000003 (64-bit initial bp), LOAD_DLL wow64cpu, kernel32, ...,
EXCEPTION 4000001f (WX86 bp, ~30 events later). No other process.
Wine: CREATE_PROCESS, ntdll 64, ntdll 32, wow64, wow64cpu, wow64win, win32u, kernel32 ...,
bp; no WX86 bp (already todo_wine). The WoW64 startup order was never the problem.

## Cause
Not ordering of the 32-bit ntdll at all. When no desktop exists yet, msinfo32's first
window makes win32u `get_desktop_window` start `explorer.exe /desktop` with NtCreateUserProcess
from msinfo32. Being msinfo32's child, explorer inherited the DEBUG_PROCESS debugger: the loop
got explorer's CREATE_PROCESS + LOAD_DLL ntdll (64-bit ntdll is at the same base in both
processes, so `GetMappedFileNameW(msinfo32, base)` names it ntdll.dll) after the first bp
-> 1188, and num_ntdll == 3 -> 1257. Also: the debugger exiting killed the desktop.
Environment dependence: explorer is only spawned when the desktop isn't running. The
desktop closes 1 s after its last user (notes/wine/desktops-input.md); in full runs another
unit on the shard usually keeps it alive. Check: unfixed build, fresh wineserver: 2 failures;
`wine explorer.exe /desktop` started first: 0 failures.
On Windows the desktop is never the app's child; a debugged process can stop inheritance
with NtSetInformationProcess(ProcessDebugFlags, 0) (VM: child not debugged; value 2 ->
STATUS_INVALID_PARAMETER, size 8 -> STATUS_INFO_LENGTH_MISMATCH). Wine only had the query.

## Fix (fix/105)
- `cab481e280e win32u: Don't let the desktop process inherit the app's debugger.` When the app
  has a debug port, explorer is created with a private debug object (PS_ATTRIBUTE_DEBUG_PORT, which
  replaces inheritance) and NtRemoveProcessDebug detaches it before resuming: the queued startup
  events are auto-continued, nobody sees them. Race-free, no protocol change.
- `e57f8e74d0c ntdll: Implement NtSetInformationProcess(ProcessDebugFlags).` Optional, independent
  (not needed by the fix): server set_process_info mask SET_PROCESS_INFO_DEBUG_CHILDREN
  (**protocol 969**), wow64 pass-through, info.c tests (pass on the VM, both arches).
- Rejected: (1) toggling ProcessDebugFlags around the launch (first version): another thread's
  child created meanwhile loses its debugger. (2) PROCESS_CREATE_FLAGS_NO_DEBUG_INHERIT: on Windows
  the child still inherits the debugger, the flag only clears the child's own ProcessDebugFlags
  (VM, `-ntinherit`; Wine doesn't do even that -> draft 106). (3) NtRemoveProcessDebug of the
  inherited debugger after creation: Wine queues the startup events at init_process_done, before
  NtCreateUserProcess returns, so the app's debugger had already seen CREATE_PROCESS.

## Results (cab481e280e alone, protocol 968)
`tools/regress.sh unit kernel32:debugger -a x86_64 -n 30`: before (build/, integ) 30/30 fail
(2 failures each); after 30/30 pass idle, 30/30 pass with 100 spinners (same for the first,
toggle-based version).
`regress.sh run -j 8 -m '^(ntdll|kernel32|win32u)$'` vs build/ (integ): 124 units, only difference
x86_64 kernel32:debugger fail 2 -> pass (0 worse).
