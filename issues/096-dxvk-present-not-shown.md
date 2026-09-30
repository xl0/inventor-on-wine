# 096 DXVK 3.1.1 presents never reach the screen in small D3D11 probes (NVIDIA :100)
Status: open (draft) · Owner: - · Branch: - · Found in: 078 (tests/expose_present.c, tests/present_lag.c)

## Symptom
Scratch prefix + app-local DXVK 3.1.1 x64 dlls (`WINEDLLOVERRIDES=d3d11,dxgi=n`), NVIDIA headless
Xorg :100 (DRI_PRIME=pci-0000_16_00_0, DXVK picks GPU0 = bus 22, IMMEDIATE, B8G8R8A8, 3 images):
tests/expose_present.exe reads 000000 everywhere; tests/present_lag.exe 60 1: none of 60 frames
show. Same with the swapchain on the toplevel instead of a child (so not the offscreen copy),
with `dxgi.syncInterval = 1`, and on wt/regress-master-build (upstream 4e819f054dd), build/ and
fix/078. wined3d vk/gl on the same setup show every frame. The child's redirected X window stays
black too (tools/pixgrab.py), i.e. NVIDIA never writes the presented image into it, although
win32u's vkQueuePresentKHR runs and copies (+x11drv, no Vulkan errors in +vulkan).
DXVK did render Inventor's viewport in 060 (:98), so something in these probes (D3D11CreateDevice-
AndSwapChain, R8G8B8A8 FLIP_DISCARD, 2 buffers, sync interval 0/1, DXVK's present_wait use)
or in the :100 setup matters. Only relevant if DXVK is recommended as a workaround (060).

## Repro
`cp deps/dxvk-3.1.1/x64/*.dll $WINEPREFIX/drive_c/windows/system32/`, then
`WINEDLLOVERRIDES=d3d11,dxgi=n DXVK_LOG_LEVEL=info wine tests/present_lag.exe 60 1`.
