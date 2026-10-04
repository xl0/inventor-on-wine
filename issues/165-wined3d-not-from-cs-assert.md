# 165 Inventor: "Assertion failed … wined3d_private.h … cs->thread_id != GetCurrentThreadId()" box (seen once)
Status: draft · Owner: - · Branch: - · Found in: new-host verification round (inv3 :100, build-s = integ e00a74f6590, 2026-10-04)

## Symptom
A Wine assertion box in Inventor: `Assertion failed! … dlls/wined3d/wined3d_private.h Line 4792 Expression:
cs->thread_id != GetCurrentThreadId()` (`wined3d_not_from_cs()`: a wined3d function that must not run on
the command-stream thread was called from it). The box is modal: the next harness connect failed
(`sheetmetal` FAIL at connect, 28.8 s) until that Inventor was replaced.
Screenshot: inst/round2/assert-s-1.png (not in the repo), log: inst/round2/sheetmetal-s-fail.txt.

## Frequency
1 time: the first `run.sh all` of a session that had run `hello`, `tests/r131/winlist.exe` and `uilat` before.
Not reproduced in three more `all` runs on build-s (the exact hello -> uilat -> all sequence once more)
nor in one `all` on the old build/ 04293594c50. The asserting Inventor's log was not kept (WINEDEBUG=-all),
so the calling function is unknown. A/B inconclusive: could be old (upstream assertion, a race) or new with
131 (window-less composition swapchains: `WINED3D_SWAPCHAIN_WINDOWLESS`, present/rotate and destroy paths),
NVIDIA 595 / new host, or gcc 15.

## Next
Which process asserted (Inventor.exe's own D3D11 viewport, or was it a WebView2 helper's box)? Get the stack:
run the sequence in a loop with `WINEDEBUG=+seh` (or err only) and, when the box is up, gdb the process
(tools/gdb/winesyms.py, sehbt.py): the frame above `_wassert` names the wined3d function. Then decide.
