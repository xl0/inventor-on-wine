# 166 winewayland + NVIDIA Vulkan: Present on a visible window stalls for seconds (vkAcquireNextImageKHR)
Status: draft · Found in: 132 M0 on the new host (mutter 50.1 headless, NVIDIA 595.91.07) · Component: unknown (win32u vulkan / winewayland / NVIDIA WSI / headless mutter)

## Symptom
`WINE_D3D_CONFIG=renderer=vulkan` under the host Wayland session: `tests/d3d11_present.exe` never finishes (300 presents),
`tests/r132/xp.exe visible interval=0 frames=60 sleep=0` reports "Present of frame 13 has not returned after 3014 ms",
single presents take 8 s and 19 s. A hidden window presents fine (60 presents, 0.5 ms average). renderer=gl is fine (NVIDIA EGL).

## Evidence
gdb (`inst/132/m0b/vk-stall-gdb.txt`): wined3d_cs in `win32u_vkAcquireNextImageKHR` -> NVIDIA driver -> `drmSyncobjTimelineWait`
(the driver waits for the compositor to release a swapchain image; `wp_linux_drm_syncobj_manager_v1` is advertised).

## Open
Not investigated. Does the compositor (headless, virtual monitor) not signal the release points, or does Wine's surface
setup (client subsurface, viewport, no frame callbacks) keep images busy? Try a native Vulkan Wayland client (vkcube --wsi wayland)
in the same session, and `VK_PRESENT_MODE` / `__NV_DISABLE_EXPLICIT_SYNC=1`. Until fixed, Inventor on Wayland runs renderer=gl.
