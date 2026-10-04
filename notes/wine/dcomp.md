# DirectComposition (dcomp.dll) — checked at integ 77645e2b221 + fix/017

- Upstream Wine: DCompositionCreateDevice/2/3 are E_NOTIMPL stubs. Wine-Staging
  `patches/dcomp-DCompositionCreateDevice2` (Zhiyi Zhang, CodeWeavers; ~67 patches,
  upstream MR 9839) implements devices, targets, visuals, surfaces and a compositor
  thread; fix/017 imports it. Needs `tools/make_requests` (adds server requests for
  shared visual handles) and a configure entry for dlls/dcomp/tests.
- Staging's compositor: per device a thread, every refresh period, reads each
  target's root visual content (composition swapchain / IDCompositionSurface), draws
  it via D2D into a GDI-compatible bitmap and AlphaBlends it onto
  GetDCEx(target hwnd). Composition swapchains came from a dxgi HACK:
  CreateSwapChainForComposition = CreateSwapChainForHwnd on a hidden popup that was
  never destroyed (131). fix/131: they have no window, as on Windows (GetHwnd fails;
  Win11 table in issue 131): wined3d swapchain with WINED3D_SWAPCHAIN_WINDOWLESS = no DC,
  no Vulkan surface, Present only rotates the buffers and never waits for vblank (149). The compositor needs just GetBuffer +
  GetLastPresentCount. WebView2's GPU process creates and releases them on CrGpuMain,
  a new one per root-surface resize/hide/show.
- GDI can't reach a window whose top-level window belongs to another process
  (child windows paint on the top-level's surface, see window-surfaces.md).
  fix/017 presents such targets through its own DXGI swapchain instead (root content only).
- Flip-model buffer order (tests/dxgi_comp_swapchain.c, Win11 = Wine): after
  Present1 the presented frame is buffer BufferCount-1, buffer 0 is the next back
  buffer. Staging's "Always use the front buffer" (GetBuffer(0)) reads a stale
  frame; fix/017 drops it.
- WebView2/Edge (msedgewebview2 154) GPU process:
  - hardware path: DCompositionCreateDevice3(dxgi dev, IDCompositionDesktopDevice),
    QI IDCompositionDevice3, QI undocumented {4ca97a18-cbfd-4b0d-89e1-f7fa86d8d63e}
    (Win11: == the Device3 pointer, tests/dcomp_qi.c), context QI ID3D11VideoContext1,
    visual SetClipObject/SetTransformObject(NULL), SetOffsetY, SetClip(rect): any failure
    is a CHECK (int3); 6 GPU crashes (~30 s) then software compositing. All succeed since
    fix/085. IDXGIDevice2::EnqueueSetEvent failing at the first present only makes a
    >100 MB DumpWithoutCrashing. The compositor still ignores offsets/clips/transforms.
  - No usable D3D11 device at all (164: wined3d on a Vulkan device that cannot present to the display,
    D3D11CreateDevice 0x8007000e): hardware-path GPU processes exit quietly (7 per browser), then three software-mode ones
    (`--use-gl=disabled`) die at `output_device_backing.cc:150 D3D11CreateDevice failed` (the software presenter wants a
    WARP device), then the BROWSER process dies: `gpu_data_manager_impl_private.cc:436 GPU process isn't usable. Goodbye.`
    No msedgewebview2 process is left; the host app's web panes stay blank/black.
  - Crashpad dumps (EBWebView/Crashpad/reports) name the site: `strings -a` shows
    `ptype`, `DumpWithoutCrashing-file/-line`; Chromium source (github.com/chromium mirror).
  - software path (see also 023: it presents with a dirty rect): DCompositionCreateDevice(NULL), CreateTargetForHwnd(its own child
    window, reparented into the browser window), CreateVisual, composition swapchain
    (B8G8R8A8, 2 buffers, FLIP_SEQUENTIAL, premultiplied), SetContent, SetRoot, one
    Commit; per frame GetDC on buffer 0 + Present1.
- Chromium's GPU output window is WS_EX_TRANSPARENT|WS_EX_LAYERED|WS_EX_NOREDIRECTIONBITMAP,
  owned by the GPU process, and parented into the browser's window, which sits in the
  host app's toplevel (three processes). 027: the server ignored transparent children
  when clipping the parent surface.
- Edge (the browser) sandboxes its renderers in lowbox app containers
  (kernelbase CreateAppContainerToken, 026; before it, only --no-sandbox rendered). It launches from
  AdskIdentityManager through HKCR https.
- Composition thread (device.c composite_thread_proc, fix/088): composes after Commit() (global
  commit serial, so shared visuals of other devices count), when a content swapchain's
  GetLastPresentCount() changed, when the target window was damaged (WH_GETMESSAGE WM_PAINT and
  WH_CALLWNDPROCRET erase/ncpaint/windowposchanged/showwindow hooks on the window's thread, target.c;
  composes that pass and the next) and once a second as a safety net (X exposes of offscreen client
  surfaces reach nobody). No DWM: the composed image lives in the window surface / client surface.
  The same-process path draws with GDI into the window surface; GetQueueStatus(0) flushes it without
  the message wait's side effect of signalling the process idle event (WaitForInputIdle).
