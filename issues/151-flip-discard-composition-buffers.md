# 151 FLIP_DISCARD composition swapchains read back black after Present
Status: draft · Found in: 131 review, integ d18a5dcd1ef (predates fix/131), 2026-10-03

## Symptom
A composition swapchain created with DXGI_SWAP_EFFECT_FLIP_DISCARD: after Present the frame just presented is
not in buffer BufferCount-1; reading that buffer gives opaque black (ff000000) for every frame. dcomp composes
exactly that buffer (device.c, "the front buffer is the last back buffer"), so such a swapchain would show black.
FLIP_SEQUENTIAL is fine (reviewer's probe2: 0 mismatches with the Vulkan renderer), and Chromium / WebView2 use
FLIP_SEQUENTIAL for their root surface, so Inventor is not affected.

## Repro
`PROBE_DISCARD=1 wine tests/r131/review_rotation.exe` (source tests/r131/review_rotation.c, next to it): 54 mismatches with the Vulkan
renderer, 58 with GL, on integ and on fix/131 alike (inst/131-review/probe2dis-*.txt), e.g.
`MISMATCH discard 2 bufs frame 1 buffer 1: got ff000000, expected ff306038`.
No Windows comparison is possible through the API: there GetBuffer(n > 0) fails with DXGI_ERROR_INVALID_CALL on a
FLIP_DISCARD swapchain (inst/131-review/probe2-win.txt), only DirectComposition sees the presented frame.

## Cause (suspected)
wined3d marks the last back buffer WINED3D_LOCATION_DISCARDED after a present for the discard swap effects
(wined3d_cs_exec_present), which is the buffer that holds the presented frame after the rotation.

## Task
Keep the presented frame readable for the compositor on FLIP_DISCARD composition swapchains (do not discard the
buffer dcomp reads, or give dcomp its own reference to the presented image), and make GetBuffer(n > 0) fail as on
Windows. dcomp test with a FLIP_DISCARD swapchain as visual content.
