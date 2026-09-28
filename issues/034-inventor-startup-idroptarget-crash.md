# 034 Intermittent Inventor startup crash in ole32 IDropTarget_Release
Status: open (draft, intermittent) · Owner: - · Branch: - · Found in: 032 timing runs (1 of 6 cold starts)

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
