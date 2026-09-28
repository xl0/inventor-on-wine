# 017 dcomp: DCompositionCreateDevice* are E_NOTIMPL stubs (WebView2 GPU process dies)
Status: fixed (awaiting review) · Owner: worker · Branch: fix/017-dcomp-staging (wt/017, on integ 77645e2b221) · Found in: inv-vm (build/ at integ 2d9c0d96550, WebView2 Runtime 154.0.4258.37)

## Symptom
Inventor start-up -> AdskLicensingAgent opens its sign-in window (WebView2,
windowed hosting, via Autodesk's browser_native.dll). The window stays blank white.
msedgewebview2.exe starts browser, GPU, network, storage and renderer processes,
then the GPU process dies with STATUS_BREAKPOINT (Chromium CHECK, int3 in
msedge.dll+0x4a201f5) every time. It dies again after the software fallback
(`--use-gl=disabled` on the relaunched GPU process). After 3 GPU crashes the
browser process also CHECK-fails (msedge.dll+0xb05f40d, crash key
`last_gpu_crash_exit_code = STATUS_BREAKPOINT`), and the agent is left with an
empty window. Crashpad dumps are in
`users/$USER/AppData/Local/Autodesk/AdskLicensingAgent/v1/native/EBWebView/Crashpad/reports`
(the crash keys are readable with `strings`).

## Evidence (WINEDEBUG=+pid,+seh,+loaddll,warn+all; local: inst/transplant/wv2-seh-trace.log)
GPU process, CrGpuMain thread, last calls before the int3:
```
fixme:d3d11:d3d11_create_device WARP driver not implemented, falling back to hardware.
warn:d3d:wined3d_swapchain_vk_create_vulkan_swapchain Image count 0 is not supported (2-8).
fixme:dcomp:DCompositionCreateDevice 0000000000000000, {c37ea93a-...-9746cb0407f3} (IDCompositionDevice)
trace:seh:dispatch_exception code=80000003 ... rax=0000000080004001   <- E_NOTIMPL, then CHECK
```
Earlier the GPU main thread also calls `DCompositionCreateDevice3(<device>,
IID_IDCompositionDesktopDevice)` and gets E_NOTIMPL. That failure is not fatal.
The fatal call is the NULL-device `DCompositionCreateDevice`, which is also made
on the software-compositing path.

## Windows ground truth
`tests/dcomp_create.c` calls DCompositionCreateDevice / 2 / 3 with a NULL
rendering device:
- VM (Win11): all three return S_OK.
- Wine (integ): all three return 0x80004001 (`dlls/dcomp/device.c` has only 47 lines of stubs).

## Notes
- Upstream context: Wine-Staging 11.6 has a large DirectComposition series from
  CodeWeavers, and later staging adds DCompositionCreateDevice2. Check it before
  writing our own. The minimum here is a device object whose methods the GPU
  process calls next. Unknown until the stub is gone; expect more (visuals,
  targets for HWND, Commit).
- Workaround switches could not be tested. AdskLicensingAgent's WebView2 loader
  (webview/webview-style, built into browser_native.dll) reads only
  WEBVIEW2_BROWSER_EXECUTABLE_FOLDER / _USER_DATA_FOLDER / _RELEASE_CHANNEL_PREFERENCE.
  It ignores WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS and the
  `Policies\Microsoft\Edge\WebView2\AdditionalBrowserArguments` registry policy
  (traced: +process/+reg). Crash-dump switch counts show no extra switches arrive.
  So `--disable-gpu` / `--disable-direct-composition` can't be injected for this
  app, and the GPU process already fails in software mode.
- After this is fixed: windowed hosting normally parents msedgewebview2's HWND
  into the host window across processes (not verified here). If so, it may hit
  the cross-process child-window issues noted under 006.

## Task
Implement enough of dcomp (or bring in the staging series) that
DCompositionCreateDevice*(NULL, ...) succeed and Chromium's GPU process
composites. Then re-run inv-vm to the Autodesk sign-in page.

## Outcome (worker)
- Imported Wine-Staging `patches/dcomp-DCompositionCreateDevice2` (staging cc193df7,
  66 of 67 patches, authorship kept) onto integ. Generated files folded in:
  configure entry for dlls/dcomp/tests, server protocol (make_requests).
- Dropped staging 0066 "dcomp: Always use the front buffer": the last presented frame
  of a flip-model swapchain is buffer BufferCount-1 (tests/dxgi_comp_swapchain.c,
  Win11 = Wine); with 0066 the compositor showed an all-zero buffer (black window).
- Own commit: dcomp presents targets inside another process's window through a DXGI
  swapchain. Chromium's software output targets its own child window, reparented into
  the browser's window; staging's GDI AlphaBlend onto it draws nothing in Wine (child
  windows use the foreign top-level's surface), so the page stayed white.
- Own commit: dropped staging's DCompositionWaitForCompositorClock absence test
  (Win11 exports it).
- dcomp tests: Wine 743 tests, 28 todo, 0 failures; VM (Win11) 1089 tests, 0 failures.
- Result: inv-vm (wt/017-build, wined3d-vk unchanged) shows AdskLicensingAgent's
  "Let's Get Started" page (Sign in with your Autodesk ID / serial number / network
  license). Not clicked.
- Left over: the hardware GPU path CHECK-fails 6 times (~30 s) before Chromium falls
  back to software compositing: it QIs the desktop device for an undocumented
  interface (Win11: S_OK). Filed as 022.
- Knowledge: notes/wine/dcomp.md.
