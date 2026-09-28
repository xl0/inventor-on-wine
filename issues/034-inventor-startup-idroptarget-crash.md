# 034 Intermittent Inventor startup crash in ole32 IDropTarget_Release
Status: fixed (pending confirmation in Inventor) · Owner: 034 worker · Branch: fix/034-revokedragdrop · Found in: 032 timing runs (1 of 6 cold starts)

Inventor crashed once at cold start (integ + 032) in ole32's IDropTarget
Release path, reading a garbage pointer. Not reproduced in 5 further starts.
Suspect: drop-target registration/revocation race (RegisterDragDrop /
RevokeDragDrop, OLE drop target stored in window property) during startup
window creation/destruction. Next time it happens: keep the crash dump / CER
report and the backtrace (winedbg), note which window was being destroyed.

## Test campaign (2026-09-28, integ 91495f487ad)
Not startup-only: during `run.sh all`, Inventor (started 10:45:39 by the suite)
crashed ~1.5 min later in the `part` scenario (the first Documents.Add of a
visible part after start; ActiveView was then null): page fault in
ole32+0x347a0 = `IDropTarget_Release` inlined in `RevokeDragDrop`
(dlls/ole32/ole2.c:648, addr2line on build/). inventor.log has two more such
faults today (ole32 offset …4750). The CER dialog + `winedbg --auto` then keep
the dead Inventor.exe around, so the next scenarios hang in connect.
winedbg --auto's stdout was /dev/null here: to get a backtrace, point AeDebug at
a winedbg that logs to a file, or run Inventor under `winedbg` directly.

## Findings (034 worker)
Fault is `mov (%rax),%rax` at ole32 RevokeDragDrop → IDropTarget_Release. rax is the
`OleDropTargetInterface` prop value itself, so the pointer is unmapped in Inventor
rather than being a freed object with a bad vtable. All six faulting values
(0x342C008DAB20, 0x13BC02AB8570, 0x1784008BC450, 0x36BC008DCA80, ...) are a 16 GB-aligned
base plus a small offset. That looks like Chromium PartitionAlloc pools, i.e. objects of
another process (msedgewebview2; Inventor runs two WebView2 instances).
Inventor side (FwUI.dll, decomp): `FWxDockableWindow` AddChildWindow(hwnd)/OnCreate call
a helper that does `RevokeDragDrop(hwnd); RegisterDragDrop(hwnd, own)` to take over
drops on hosted child windows. OnDestroy revokes them again. Wine's RevokeDragDrop
released the prop pointer without checking who registered it.
Synthetic repro: `tests/dragdrop_revoke.c` (a cross-process revoke crashed master Wine at
the same instruction).

Windows ground truth (Win11 VM, `tests/dragdrop_revoke.exe`):
- Props: `OleDropTargetInterface` (target) + `OleEndPointID` (per-apartment id).
  RegisterDragDrop AddRefs once and never calls QI.
- A revoke from any thread other than the registering apartment (another STA, the MTA,
  a no-COM thread, another process, or the same thread after
  OleUninitialize/OleInitialize) returns RPC_E_WRONG_THREAD. There is no Release, but
  the props are removed (the owner's later revoke returns DRAGDROP_E_NOTREGISTERED, the
  target leaks). Cross-process RegisterDragDrop succeeds (Wine: FIXME, INVALIDHWND).
- OleUninitialize (and thread exit) releases the targets of still-alive registered
  windows. DestroyWindow without revoke releases nothing, not even later.
- Revoke Releases Windows' own stored pointer: an overwritten `OleDropTargetInterface`
  prop is ignored. With the prop removed, revoke returns NOTREGISTERED.

Fix (6637ab2cf67): RegisterDragDrop stores the registering thread id
(`WineDropTargetThread` prop). When RevokeDragDrop is called from any other thread it
returns RPC_E_WRONG_THREAD without Release. For same-process callers it still removes
the registration. For other-process windows it leaves everything alone, because the
map handle belongs to that process. The ole32 dragdrop test is extended.

Remaining deviations (no crash, not fixed):
- No release at OleUninitialize.
- Release uses the prop value (Wine crashes if the app overwrites the prop).
- Cross-process registration is unsupported.
- After a cross-process revoke the props stay.

Crash capture: see notes/wine/debugging.md (AeDebug via cmd redirect + ShowCrashDialog=0).

Capture change for Inventor prefixes (apply with the prefix's build while Inventor is stopped,
`WINEPREFIX=... build/wine regedit /S file.reg`):
```
REGEDIT4
[HKEY_LOCAL_MACHINE\Software\Microsoft\Windows NT\CurrentVersion\AeDebug]
"Debugger"="cmd /c winedbg --auto %ld %ld >>C:\\winedbg-crash.log 2>&1"
[HKEY_CURRENT_USER\Software\Wine\WineDbg]
"ShowCrashDialog"=dword:00000000
```
Revert: same with `"Debugger"="winedbg --auto %ld %ld"` and `"ShowCrashDialog"=dword:00000001`.
Log: `drive_c/winedbg-crash.log`. Tested on wt/034-prefix: backtrace written, process exits.

Tests: the ole32 dragdrop test passes on the VM and on Wine (x86_64 and i386), and the
new checks fail on unfixed Wine. regress (ole32|combase|user32|shell32|comctl32) against
4e819f054dd: 0 REAL, 3 FLAKY (user32 input/win/winstation).
inv2 probe (`tests/droptargets.exe`, read-only, one run while Inventor ran there): no
window had a drop-target registration at that moment, so the WebView2 link is not
confirmed. Next time it crashes, the AeDebug log above should name the hwnd/caller.
