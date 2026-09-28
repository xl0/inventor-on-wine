# 023 dxgi: Present1 ignores dirty rectangles on flip-model swapchains
Status: fixed (awaiting review) · Owner: 017 worker · Branch: fix/023-dxgi-present1-dirty-rects · Found in: inv-vm (AdskIdentityManager WebView2 dialog), wt/017-build

## Symptom
WebView2's software compositor copies only the damaged region into the back buffer
of its composition swapchain (FLIP_SEQUENTIAL, 2 buffers) and calls Present1() with
one dirty rect. On Wine only the damaged part showed; the rest was a stale/empty frame.

## Windows ground truth
tests/dxgi_comp_swapchain.c: after a dirty-rect Present1,
the presented buffer has the previous frame outside the dirty rects. Wine kept stale content.

## Fix
d3d11_swapchain_Present1(): for FLIP_SEQUENTIAL with dirty rects, copy the region
outside them from the last presented buffer (back buffer BufferCount-1) into buffer 0
before presenting. dxgi test test_swapchain_present_dirty_rects: VM + Wine pass.
Not done: scroll rects (FIXME), d3d12 swapchains.
