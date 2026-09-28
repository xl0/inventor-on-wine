# DirectComposition (dcomp.dll) — checked at integ 77645e2b221 + fix/017

- Upstream Wine: DCompositionCreateDevice/2/3 are E_NOTIMPL stubs. Wine-Staging
  `patches/dcomp-DCompositionCreateDevice2` (Zhiyi Zhang, CodeWeavers; ~67 patches,
  upstream MR 9839) implements devices, targets, visuals, surfaces and a compositor
  thread; fix/017 imports it. Needs `tools/make_requests` (adds server requests for
  shared visual handles) and a configure entry for dlls/dcomp/tests.
- Staging's compositor: per device a thread, every refresh period, reads each
  target's root visual content (composition swapchain / IDCompositionSurface), draws
  it via D2D into a GDI-compatible bitmap and AlphaBlends it onto
  GetDCEx(target hwnd). Composition swapchains come from a dxgi HACK:
  CreateSwapChainForComposition = CreateSwapChainForHwnd on a hidden popup.
- GDI can't reach a window whose top-level window belongs to another process
  (child windows paint on the top-level's surface, see window-surfaces.md).
  fix/017 presents such targets through its own DXGI swapchain instead (root content only).
- Flip-model buffer order (tests/dxgi_comp_swapchain.c, Win11 = Wine): after
  Present1 the presented frame is buffer BufferCount-1, buffer 0 is the next back
  buffer. Staging's "Always use the front buffer" (GetBuffer(0)) reads a stale
  frame; fix/017 drops it.
- WebView2/Edge (msedgewebview2 154) GPU process:
  - hardware path: DCompositionCreateDevice3(dxgi dev, IDCompositionDesktopDevice),
    QI IDCompositionDevice3, then QI undocumented {4ca97a18-cbfd-4b0d-89e1-f7fa86d8d63e}
    (Win11 S_OK, tests/dcomp_qi.c); E_NOINTERFACE -> CHECK. 6 crashes (~30 s), then
    software compositing.
  - software path (see also 023: it presents with a dirty rect): DCompositionCreateDevice(NULL), CreateTargetForHwnd(its own child
    window, reparented into the browser window), CreateVisual, composition swapchain
    (B8G8R8A8, 2 buffers, FLIP_SEQUENTIAL, premultiplied), SetContent, SetRoot, one
    Commit; per frame GetDC on buffer 0 + Present1.
- Chromium's GPU output window is WS_EX_TRANSPARENT|WS_EX_LAYERED|WS_EX_NOREDIRECTIONBITMAP,
  owned by the GPU process, and parented into the browser's window, which sits in the
  host app's toplevel (three processes). 027: the server ignored transparent children
  when clipping the parent surface.
- Edge (the browser) needs --no-sandbox under Wine for now (026). It launches from
  AdskIdentityManager through HKCR https.
