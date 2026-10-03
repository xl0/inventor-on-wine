# 150 Vulkan renderer: swapchain of a destroyed window fails to recreate, then crashes when destroyed
Status: draft · Found in: 131 review, integ d18a5dcd1ef (predates fix/131), 2026-10-03

## Symptom
D3D11 swapchain on an HWND, the window is destroyed, then Present and Release of the swapchain, with
`WINE_D3D_CONFIG=renderer=vulkan`: Present returns S_OK, the log shows
`err:d3d:wined3d_swapchain_vk_create_vulkan_swapchain Failed to get surface capabilities, vr VK_ERROR_SURFACE_LOST_KHR.`
`err:d3d:swapchain_vk_present Failed to recreate swapchain, hr 0x80004005.`
and the process dies in the swapchain's destruction:
`err:vulkan:vkDestroySwapchainKHR Exception 0xc0000005 in Unix call.` (or vkDestroySemaphore), exit code 3.
Windows: Present returns S_OK, no crash (inst/131-review/probe2-win.txt). The GL renderer only logs
"swapchain_blit_gdi Failed to blit".

## Repro
`WINE_D3D_CONFIG=renderer=vulkan wine tests/r131/review_rotation.exe deadwin` (source tests/r131/review_rotation.c, next to it; on Xvfb with
`VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.x86_64.json`). Outputs: inst/131-review/probe2dw-base-renderer_vulkan.txt
(integ), same on fix/131 de75813d2f7.

## Cause (from reading, not fixed)
wined3d_swapchain_vk_recreate() destroys the Vulkan swapchain first (frees vk_images and vk_semaphores, destroys
the VkSwapchainKHR and the surface) and then fails to create the new one; the swapchain keeps the freed
vk_images / vk_semaphores pointers, the stale image_count and the dead handles, and
wined3d_swapchain_vk_destroy_vulkan_swapchain() runs on them again at destruction.

## Why it matters here
dcomp presents targets inside another process's window through its own swapchain on that window (Chromium's
GPU-process child of the browser window, notes/wine/dcomp.md). When the host closes the window before the GPU
process drops the target, this path can take the GPU process down.

## Task
Leave the swapchain in a clean "no Vulkan swapchain" state when recreation fails (NULL the pointers, zero
image_count, VK_NULL_HANDLE for the handles) so later presents skip or retry and destruction is safe; decide what
Present should do meanwhile (Windows: S_OK). A test with a destroyed window in d3d11 or dxgi tests.
