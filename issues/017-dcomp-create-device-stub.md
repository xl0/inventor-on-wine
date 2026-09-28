# 017 dcomp: DCompositionCreateDevice* are E_NOTIMPL stubs (WebView2 GPU process dies)
Status: open (draft) · Owner: - · Branch: - · Found in: inv-vm (build/ at integ 2d9c0d96550, WebView2 Runtime 154.0.4258.37)

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
