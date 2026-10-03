# 131 Composition swapchains leak a hidden top-level window each (WebView2 GPU process)
Status: fixed (awaiting review) · Owner: worker-131 · Branch: fix/131 (8915c5c5274, wt/131 on integ d18a5dcd1ef) · Found in: 130 (soak #4 rubber fps), inv3/inv2 2026-10-02/03

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

## More Windows ground truth
`tests/r131/comp_probe.exe` (D3D11 device and D3D12 queue, Win11 VM; identical for both):
| call on a composition swapchain | Win11 | fix/131 |
|---|---|---|
| thread windows around create / release | 0 -> 0 -> 0 | unchanged (the count includes Wine's per-factory "DXGI device window") |
| GetHwnd | DXGI_ERROR_INVALID_CALL, NULL | same |
| GetDesc | S_OK, OutputWindow NULL, Windowed TRUE | same |
| GetContainingOutput | DXGI_ERROR_UNSUPPORTED, NULL | same |
| GetFullscreenState | S_OK, FALSE, NULL | same |
| SetFullscreenState(TRUE / FALSE) | DXGI_ERROR_INVALID_CALL | same |
| Present, ResizeBuffers(320x240) | S_OK | same |
| create with 0x0, DXGI_SWAP_EFFECT_DISCARD, DXGI_SCALING_NONE, NULL desc / device | DXGI_ERROR_INVALID_CALL | same (old hack: accepted; NULL desc crashed) |
| ResizeTarget | DXGI_ERROR_INVALID_CALL | S_OK (no-op), left |
| GetFullscreenDesc | DXGI_ERROR_INVALID_CALL | S_OK, Windowed TRUE, left |
| ResizeBuffers(0x0) | E_INVALIDARG | d3d11 0x8876086c + wined3d ERR "Failed to get client rect", d3d12 DXGI_ERROR_INVALID_CALL, left |
| GetDesc1 AlphaMode | PREMULTIPLIED | d3d11 IGNORE (old FIXME), d3d12 same as Windows |
`tests/r131/comp_threads.exe` (create on a thread that exits, use + release on main; create on main, release on
another thread; back buffer held past the swapchain and released on another thread): Win11 no process window at
any point, all calls S_OK, exit 0 (IDXGISwapChain::Release with a buffer held returns 1 there).

## Who creates and releases (WebView2 154 GPU process)
Traced with the thread id + GetThreadDescription in the old hack and in the d3d11 swapchain release/destroy paths
(inst/131/instr.patch, inventor-base.log), one `run.sh all` on inv3: Home GPU process 74 creates, 73 releases,
licensing popup 1 / 0, Assistant 3 / 2; every create and every final Release on `CrGpuMain`, and the wined3d
swapchain is destroyed in that same Release (nobody holds a buffer longer). So Chromium itself only needs the
same-thread case; the other cases matter for the software path, other apps and the API contract.

## Fix (fix/131)
No window at all, as on Windows; the lifetime problem and the thread question disappear with it.
- `db0185a25ca wined3d: Support swapchains without a window.` A swapchain whose window is NULL gets no DC, no
  Vulkan surface/swapchain and no state registration (the Alt+Enter WH_GETMESSAGE hook the hidden window put on
  CrGpuMain); Present loads back buffer 0 into the draw binding and rotates the buffers, nothing else (GL and
  Vulkan). A GL context that switches to such a swapchain keeps the drawable it has (context_gl.c, one condition):
  it renders to FBOs anyway, so no backup window and no wglMakeCurrent per swapchain switch.
- `e7e60511eaf dxgi: Don't create a window for composition swapchains.` CreateSwapChainForHwnd's body became
  `dxgi_factory_create_swapchain()` (window may be NULL); CreateSwapChainForComposition validates like Windows
  and passes NULL. d3d11 and d3d12 swapchains: GetHwnd / SetFullscreenState / GetContainingOutput fail as on
  Windows without a window. d3d12: no Vulkan surface/swapchain, the present op only releases the frame latency
  semaphore.
- `8915c5c5274 dxgi/tests: Test composition swapchains.` (d3d10 + d3d12: thread window count, the calls in the
  table that match, invalid descriptions). Passes on the VM and on Wine, both arches.

What needed the HWND before: wined3d for the DC (GL context drawable, GDI fallback), the Vulkan surface, the
default destination rectangle and the fullscreen/Alt+Enter state; dxgi's d3d12 swapchain for its Vulkan surface;
winex11 only as a consequence (whole window + client window per Vulkan surface). dcomp never used it: it reads
buffer BufferCount-1 after each present (GetBuffer, GetLastPresentCount) and never calls GetHwnd. Nothing in the
stack needs an internal way to a window.

Why not keep the window and destroy it with the swapchain: only the creating thread can destroy a window, so
a release on another thread needs a posted WM_CLOSE (lost if that thread never pumps), a creating thread that
exits takes the window away under a live swapchain, and the destroy has to hang off the wined3d swapchain
(buffer references keep it alive past the DXGI refcount). Window-less handles all of that by construction, and
drops an X window pair, a Vulkan surface and one blit + vkQueuePresent per frame as well.

Thread / lifetime cases (comp_threads.exe, exit 0 on Win11 and on the fix with GL and with the Vulkan
renderer; integ: 2 -> 7 -> 12 process windows and "Failed to blit" errors once the creating thread is gone): release on the creating thread; release on another thread; creating
thread gone before use and release; last DXGI reference dropped while a buffer is held, buffer released on
another thread (wined3d swapchain destroyed there); creation failure (nothing was created). No window in any.

## Verification
- comp_windows.exe: `before 1, 10 composition swapchains 1, released 1; GetHwnd 0x887a0001 hwnd 0`, exit 0 (GL on
  Xvfb/llvmpipe and Vulkan on lavapipe). The 1 is the calling thread's "DXGI device window" (upstream: one hidden
  static per DXGI factory with a d3d10/11 device, destroyed with the factory; Windows has none).
- inv3, `run.sh all` (13/13 PASS on both builds), counts before -> after the suite:
  | build | "Static" windows of the 3 GPU processes | X windows of the GPU processes | all X windows |
  |---|---|---|---|
  | integ d18a5dcd1ef (leak) | 10 -> 81 | 41 -> 183 | 393 -> 539 |
  | fix/131 | 3 -> 3 | 27 -> 27 | 381 -> 385 |
  (3 = one DXGI device window per GPU process.) Then `samples` on the fix (464 PASS, the 2 known Speedometer.ipt
  FAILs): 3 -> 3, 27 -> 27; was ~+230 Static per samples run.
- Screens (inst/131/*.png): Home page live after the suite and after samples (recent list shows the new files),
  trial popup rendered at a harness-free start (startup-40.png), Home redrawn after restore + five resizes and
  after maximizing again.
- Frame pacing unchanged (`+fps`, Inventor idle at Home, Assistant pane animating, 60 s): both swapchains of the
  Assistant GPU process (Chromium's composition swapchain, dcomp's target swapchain) present 49.3 / 48.5 per s on
  integ and 49.5 / 49.5 on the fix; vkQueuePresentKHR 97.7 -> 49.5 per s; that GPU process 22.6 -> 15.3 % of a
  core, renderer 15.0 -> 12.6, wineserver 9.3 -> 7.2 (one sample each, host shared).
- Teardown: Inventor closed with its close button (all msedgewebview2 gone), err lines of the exit identical in
  kind on both builds (EndDialog / RevokeDragDrop from Inventor); no Crashpad dump in any fix session outside
  the 144 repro; err-line classes of the whole suite identical to integ (38 classes).
- Regress vs build/ (same integ), final commits: dxgi d3d11 d3d12 dcomp d2d1 d3d10core d3d8 d3d9 ddraw d3d10
  d3d10_1 (wined3d swapchain code is shared), 51 units on both arches: 0 real, 1 FLAKY (x86_64 dxgi fail/2 ->
  fail/3 once: dxgi.c:8752 "Expected event fired" in the video memory budget test; re-runs new 2, 2, base 3, 2).
  dxgi on Wine 16318 tests (+59), the same 2
  refresh-rate failures; VM dxgi: 58 failures, all display-mode lines (2814..3723, 7648, 7726), none in the new
  test. dcomp 753/0 on both builds and arches. tests/dxgi_comp_swapchain.exe (GetDC + Present1 dirty rects, the
  software path's calls) prints the same on both.
- asmbig 3x: fix 28.8 / 28.7 / 19.5 s, integ 28.0 / 25.8 / 21.8 s (the bimodal COM step of 088, not this).

## Limits / left
- GL: a context created first for a window-less swapchain (not the case for d3d10/11, whose device has the
  implicit swapchain on the DXGI device window) still takes wined3d's backup window, as for the desktop window.
- The new dxgi test counts the calling thread's windows; with the wined3d command stream disabled (csmt=0) and GL
  that backup window, if ever needed, would be the caller's.
- wined3d treats every swapchain without a window this way, so a d3d8/d3d9 windowed device created with neither
  a focus nor a device window (invalid on Windows) now presents nowhere instead of GDI-blitting onto the desktop
  DC (GL) or failing to create (Vulkan).
- ResizeTarget, GetFullscreenDesc, ResizeBuffers(0x0) and d3d11 GetDesc1().AlphaMode differ from Windows (table).
- d3d12 composition swapchains are created and rotate, but dcomp cannot show them (it needs IDXGISurface buffers).
- The live software path (browser-side DCompositionCreateDevice(NULL) + GetDC swapchain) was not driven in
  Inventor, only its API sequence in the probe and the dcomp tests.
- i386 dxgi tests with the Vulkan renderer on lavapipe run out of address space at dxgi.c:5613 on both builds
  (not in regress.sh's GL setup).
- Found on the way: [144](144-webview-gpu-crash-on-window-resize.md), GPU process CHECK on repeated window
  resizes, same on integ.
