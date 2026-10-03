# 131 Composition swapchains leak a hidden top-level window each (WebView2 GPU process)
Status: draft · Found in: 130 (soak #4 rubber fps), inv3/inv2 2026-10-02/03

## Symptom
The GPU process of Inventor's WebView2 (Home page / in-app browser, `--user-data-dir=...Inventor 2027\WebBrowser`)
accumulates hidden top-level windows of class "Static", style 0x84000000 (WS_POPUP|WS_CLIPSIBLINGS), no owner, no
children, all on the GPU main thread, sized like the web view (1678x884, 1920x884, 2304x884, ...):
about 3 per document opened/closed (the Home view is resized/hidden/shown around it), ~80 per `run.sh all`,
~230 per `samples` run; 311 after one suite + samples, i.e. thousands in a 4 h soak. Only one
"Intermediate D3D Window" (Chromium's real output child) exists next to them.
`tests/r131/winlist.exe msedgewebview2` (all windows per process/class/parent; `raw` = one line each).

Each one keeps a winex11 whole window (unmapped root child, WM hints, XI2 touch selection) plus the client window
of its Vulkan surface: X windows of the GPU process 31 -> 655 over suite + samples (tests/r130/xres.py). Costs:
X server work that scales with the window count (130: root XISelectEvents 0.5 -> 1.5 ms after one suite + samples),
USER handles, and whatever wined3d/Vulkan keeps per window.

## Cause
Wine-side, in our patch stack: `002b1a8ab01 HACK: dxgi: Add a dxgi_factory_CreateSwapChainForComposition() stub.`
(from Wine-Staging's dcomp set, dlls/dxgi/factory.c):

    hwnd = CreateWindowA("static", NULL, WS_POPUP, 0, 0, desc->Width, desc->Height, 0, 0, 0, NULL);
    return dxgi_factory_CreateSwapChainForHwnd(iface, device, hwnd, desc, NULL, output, swapchain);

Nothing destroys that window when the swapchain is released (nor when CreateSwapChainForHwnd fails). Chromium's
DirectComposition presenter makes a new composition swapchain whenever its root surface is recreated (resize,
hide/show), so every one leaves a window behind.

## Windows ground truth
`tests/r131/comp_windows.exe` (10 composition swapchains, windows of the calling thread before / after creating /
after releasing them, GetHwnd):
- Win11 VM: `before 0, 10 composition swapchains 0, released 0; GetHwnd 0x887a0001 hwnd 0` (no window at all,
  GetHwnd = DXGI_ERROR_INVALID_CALL), exit 0.
- Wine integ 04293594c50: `before 1, 10 composition swapchains 11, released 11; GetHwnd 0 hwnd 0x20058`, exit 1.

## Task
Destroy the backing window with the swapchain (the swapchain owns it: e.g. a flag/field on the d3d11 and d3d12
swapchain objects set by CreateSwapChainForComposition, DestroyWindow in their destroy paths, on the creating
thread or via a message if released elsewhere), and on creation failure. GetHwnd should fail with
DXGI_ERROR_INVALID_CALL for such swapchains (check that dcomp's compositor doesn't rely on it: notes/wine/dcomp.md).
Verify with comp_windows.exe (exit 0) and that the GPU process' "Static" count stays flat over `run.sh all`
(winlist.exe). Longer term the composition swapchain shouldn't need a window at all.
Not the 130 regression (that was winex11 selecting XI2 events on the root per WM config change, fixed there), but
it is what made 130's cost grow with session age, and it grows the X window tree without bound.
