# 106: PROCESS_CREATE_FLAGS_NO_DEBUG_INHERIT ignored when the debugger is inherited (draft)

Status: draft, low priority (no app known to need it). Found in 105.

Windows (Win11 VM, `tests/dbg_wow64_order.exe -ntinherit`): a process debugged with
DEBUG_PROCESS calls NtCreateUserProcess without a debug port.
- ProcessFlags 0: child debugged, its ProcessDebugFlags 1.
- ProcessFlags PROCESS_CREATE_FLAGS_NO_DEBUG_INHERIT (2): the child is **still debugged**
  (it inherits the debugger), but its ProcessDebugFlags is 0: its own children aren't
  (no conhost events for it).

Wine: the flag only applies when a debug port is passed (server/process.c create_process:
`debug_children = !(flags & NO_DEBUG_INHERIT)` in the `debug_obj` branch only); with an inherited
debugger the child's ProcessDebugFlags stays 1. Fix is likely one line in the `parent->debug_children`
branch, no protocol change; not checked for non-debugged parents.
