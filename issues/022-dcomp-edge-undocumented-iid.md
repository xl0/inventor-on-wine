# 022 dcomp: Edge WebView2 GPU hardware path needs undocumented desktop-device interface
Status: fixed by 085 (awaiting review) · Owner: 085 worker · Branch: fix/085 · Found in: inv-vm, wt/017-build (integ 77645e2b221 + fix/017)

## Symptom
With the staging dcomp series (017), msedgewebview2's GPU process (hardware mode)
calls DCompositionCreateDevice3(<dxgi device>, IDCompositionDesktopDevice) -> S_OK,
QIs IDCompositionDevice3 -> S_OK, then QIs {4ca97a18-cbfd-4b0d-89e1-f7fa86d8d63e}
-> E_NOINTERFACE and hits int3 (Chromium CHECK). The browser relaunches the GPU
process 6 times (~30 s), then falls back to software compositing, which works.
Trace: WINEDEBUG=+pid,+dcomp,+seh.

## Windows ground truth
tests/dcomp_qi.c on Win11: create3 S_OK, QI IDCompositionDevice3 S_OK, QI 4ca97a18 S_OK.

## Notes
The IID is not in any public header (not IDCompositionDevice4 = 85fc5cca-...).
Its methods can't be learned clean-room without blind black-box probing, and
Edge presumably calls them next. Only cost today is the ~30 s delay before the
licensing page shows. Not upstream-worthy to fake a failure from
DCompositionCreateDevice3 (Windows succeeds).

## Outcome (085)
Edge calls no methods on it; Win11 returns the IDCompositionDevice3 pointer (only for
DCompositionCreateDevice3 devices). fix/085 returns the device for it, then fixes the next
hardware-path CHECKs (ID3D11VideoContext1, visual SetOffsetY/SetClip/NULL clip+transform)
and EnqueueSetEvent. No GPU crashes left; see 085.
