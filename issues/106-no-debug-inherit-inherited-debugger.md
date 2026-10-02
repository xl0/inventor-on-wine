# 106: PROCESS_CREATE_FLAGS_NO_DEBUG_INHERIT ignored when the debugger is inherited

Status: fixed on fix/106-no-debug-inherit (84386b7a641, on integ 7e8ed9554cb), not merged.
Low priority (no app known to need it). Found in 105.

Windows (Win11 VM, `tests/dbg_wow64_order.exe -ntinherit`): a process debugged with
DEBUG_PROCESS calls NtCreateUserProcess without a debug port.
- ProcessFlags 0: child debugged, its ProcessDebugFlags 1.
- ProcessFlags PROCESS_CREATE_FLAGS_NO_DEBUG_INHERIT (2): the child is **still debugged**
  (it inherits the debugger), but its ProcessDebugFlags is 0: its own children aren't
  (no conhost events for it).
- Non-debugged parent: the flag changes nothing, the child's ProcessDebugFlags is 1 either way
  (ntdll:info test).

Cause: server/process.c new_process only applied the flag in the `debug_obj` (port passed)
branch. Fix: in the inherited branch set `debug_children = !(flags & NO_DEBUG_INHERIT)`, and only
for a parent that actually has a debugger (`parent->debug_children` is 1 by default, so the
branch also ran for non-debugged parents). No protocol change.

Test: ntdll:info test_query_process_debug_flags: NtCreateUserProcess (suspended self) with/without
the flag from the non-debugged test process and from a DEBUG_PROCESS child
(`debuggee:nodebuginherit`). Passes on the VM (x64 + x86), fails on unfixed Wine, passes fixed.
ntdll:info / kernel32:process failure counts equal integ's (60/93, 112/176: host CPU-count and
other pre-existing failures); kernel32:debugger, ntdll:thread pass (3 runs each arch).
