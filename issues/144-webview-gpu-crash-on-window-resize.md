# 144 WebView2 GPU process of the Home page crashes when Inventor's window is resized repeatedly
Status: draft · Found in: 131 (inv3, integ d18a5dcd1ef, also with fix/131), 2026-10-03

## Symptom
Inventor at Home, main window restored (not maximized), then resized a few times in a row from outside
(`xdotool windowsize WID W H` every 2-3 s on :100/openbox: 1500x950, 1200x800, 1700x1000, 1300x700, 1600x900):
the GPU process of the Home page's WebView2 (`--user-data-dir=...Inventor 2027\WebBrowser`) dies at the third or
fourth step (5 of 5 runs at the 1700x1000 -> 1300x700 shrink; a first resize straight to 1300x700 survived).
Chromium starts a new GPU process and the page is redrawn 3-5 s later; until then the web area keeps stale or
blank content (and Inventor's own chrome was caught half repainted once). After several such crashes Chromium
would fall back to software compositing (notes/wine/dcomp.md).

## Evidence
- Crashpad dump per crash in `AppData/Local/Autodesk/Inventor 2027/WebBrowser/Cache/EBWebView/Crashpad/reports/`:
  `ptype gpu-process`, `exception_code 0x80000003` in msedge.dll (int3 = a Chromium CHECK), no
  DumpWithoutCrashing file/line.
- Nothing at err level in the Wine log around it. With `warn+dxgi,fixme+dxgi,warn+dcomp`: no failing dxgi call
  before the crash; each surviving step logs one new composition swapchain (GetDesc1, SetColorSpace1), the
  crashing step logs none.
- Same on build/ (integ d18a5dcd1ef, hidden-window composition swapchains) and on fix/131 (window-less ones):
  not caused by 131.
- Not seen during `run.sh all` / `samples` (document open/close resizes the web view too, without crashes).

## Task
Find the failing call (d3d11/dcomp/user32 traces of the GPU process around the shrink; Chromium source for the
CHECKs on the root-surface resize path: `SkiaOutputDeviceDComp`, `DCLayerTree`, `DXGISwapChainImageBacking`),
check whether a real WM-driven resize (mouse drag of the frame) hits it too, fix the Wine side.
