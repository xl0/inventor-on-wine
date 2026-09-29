# 057 Opening/reopening multi-file assemblies 2.5–7x slower than Windows
Status: fixed (main cause; rest in 081) · Owner: 057 worker · Branch: fix/057-assembly-open-slow (0c22edefcdc, 7f1853f3464, 09c61c9904a) · Found in: samples campaign (inv3, integ 38e4c1c00ce)

## Observed (Wine / VM seconds, 2022 sample set)
Buffer Prep Skid.iam (1313 occurrences, 453 files): open 62.2 / 20.9, reopen
74.9 / 10.7; Test Station.iam open 15.4 / 5.9, reopen 15.4 / 3.9; Personal
Computer.iam open 13.0 / 5.6. Saves ~1.2x, single parts ~VM speed. So the cost is
per referenced file (file resolution / open / project search / OLE storage).
Note the VM runs WARP; open is mostly CPU+I/O, not graphics.

## Task
Profile a reopen of Buffer Prep Skid (perf/+relay counts on file APIs:
NtCreateFile/NtQueryAttributesFile/FindFirstFile counts, path lookups through
case-insensitive dir scans (Wine's biggest file-open cost on Linux), StgOpenStorage,
registry), compare API call counts with a VM ProcMon-free estimate if needed.
Candidates: case-insensitive lookup cost (casefold ext4 dir or Wine's dir cache),
repeated project/library search paths, storage (ole32 structured storage) reads.

## Cause
Not file I/O. Most of the gap is the harness' own API calls: samples' Counts() walks every
file reference (Missing(): FullFileName, ReferencedFileDescriptors, ReferenceMissing,
ReferencedFile; ~700 calls for Test Station, ~3000 for Buffer Prep Skid) and each
cross-process call into Inventor cost ~25-45 ms on Wine vs ~2 ms on the VM
(`tools/invscen/openbench.cs` "walk refs": Test Station 16-32 s vs 1.2-1.7 s). In a fresh
session a plain open without the walk is at VM speed (3.4-3.6 s vs 3.0-3.1 s warm).
Per call Inventor's main thread and wineserver ping-pong (~50 % CPU each):
- ~60 %: GetWindow(). Every COM call is a window message; Inventor's MFC loop
  (FwUI.dll message pump -> CWinApp::OnIdle) then runs the idle command-UI update, which
  walks the whole window tree (thousands of windows) with GetWindow(GW_CHILD/GW_HWNDNEXT).
  Each GetWindow was a server round trip: 20 us vs 10 ns on Windows
  (`tests/getwindow_perf.c`, 3500 windows). Stacks: winedbg bt samples (mfc140u recursive
  descendant walk under fwui idle), perf dwarf (NtUserCallHwndParam -> get_window_relative).
- rest: ~500 server requests per call (hooks, HKCR COM registry lookups, GetProp,
  FreeLibrary module-list walks) -> draft 081.
Also: after save-as Inventor saves migrated dependents; saves were 3-5x slower for the
same reason (walks/idle per call), now ~1.1-1.3x.

## Fix (wt/057, branch fix/057-assembly-open-slow)
- server: window_shm_t gets next/prev/first_child/last_child, maintained by
  `update_shared_children()` on every Z-order list change (link_window, set_parent_window,
  update_window_zorder). O(siblings) per change, writes only changed values.
- win32u: get_window_relative() reads GW_HWNDNEXT/GW_HWNDPREV/GW_CHILD from shared memory.
  GetWindow walk 19.9 us -> 0.1 us per call (VM 10 ns).
- user32/tests: test_GetWindow_zorder_changes (reorder, destroy, reparent both ways):
  VM 0 failures (user32 win, 70857 tests); Wine x86_64/i386 pass; regress user32|win32u vs
  master baseline: no REAL (i386 win fail/4 is FLAKY, same on base re-runs).

## Outcome (inv3, full samples run, s: before / after / VM)
Sum of all open+save+reopen steps: 1294 / 714 / 498.
Test Station open 22.5 / 9.2 / 5.9, reopen 22.1 / 9.4 / 3.9, save 48.7 / 11.8 / 10.9.
Personal Computer open 18.4 / 8.4 / 5.6, reopen 13.5 / 8.4 / 4.5.
Blower open 16.8 / 7.1 / 5.1, save 51.6 / 12.8 / 10.8.
Buffer Prep Skid open 83.9 / 60.1 / 20.9, reopen 86.6 / 33.7 / 10.7, save 181.8 / 47.4 / 37.7.
openbench walk refs (Test Station, 692 calls): 16-32 s / 6-8 s / 1.2-1.7 s.
Remaining gap = per-call overhead of 081 (Buffer Prep Skid open: ~17 s loading at full CPU,
~50 s ref walk).
Side observations: inv3's wineserver sits at 30-50 % CPU while Inventor is idle (other
prefix processes; not investigated). A samples restore right after an Inventor restart
hit `~Rim.dwg.NNNN.tmp` in use (DWGCleanServer cleaning up); rerun passed.
