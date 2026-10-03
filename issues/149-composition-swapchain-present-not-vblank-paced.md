# 149 Present on a composition swapchain never waits for vblank
Status: draft · Found in: 131 review (fix/131 de75813d2f7), 2026-10-03

## Symptom
IDXGISwapChain::Present(1, 0) in a loop on a composition swapchain (D3D11 device or D3D12 queue) returns at once on
Wine; Windows paces it to the display. An app that relies on Present for its frame pacing spins a core.
WebView2/Chromium does not (it has its own vsync source: its two swapchains present ~49 times/s before and after
131), so nothing in Inventor shows it.

## Numbers
`tests/r131/review_parity.exe pace` (source tests/r131/review_parity.c; presents in 2 s per swap-chain flags / sync interval):
| | Win11 (WARP) | fix/131 GL | fix/131 Vulkan (lavapipe) | integ GL | integ Vulkan |
|---|---|---|---|---|---|
| d3d11, interval 1 | 128 | 225805 | 213971 | 2252 | 3891 |
| d3d11, interval 0 | 32479 | 224340 | 224573 | 2361 | 3943 |
| d3d11, waitable (0x40), interval 1 | 23081 | 457740 | 487976 | 235374 | 236938 |
| d3d12, interval 1 | 132 | 215696 | 220390 | 2411 | 4584 |
| d3d12, interval 0 | 56894 | 215779 | 227545 | 2516 | 4200 |
| d3d12, waitable, interval 1 | 67742 | 6884357 | 6807065 | 7281732 | 7313132 |
Raw files: inst/131-review/pace-*.txt. integ was not paced either: its 2-4 thousand came from the cost of blitting
to the hidden window (Xvfb, software rendering), and the waitable-object swapchains already ran free.

## Cause
A window-less swapchain presents nothing (wined3d only rotates the buffers; the d3d12 one only releases the frame
latency semaphore), so no driver vblank wait is left, and dxgi has no vblank wait of its own
(dxgi_output_WaitForVBlank is a stub as well).

## Task
Give dxgi a vblank wait primitive (per output: refresh period from the display mode, or the driver's vblank where
there is one) and use it for sync intervals >= 1 on window-less swapchains, D3D11 and D3D12; WaitForVBlank and
dcomp's composition thread could share it. Check on Windows how the waitable-object flag changes the pacing
(Win11 above: not paced by Present with the flag).
