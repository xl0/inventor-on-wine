# 129 winewayland: vkGetPhysicalDevicePresentRectanglesKHR returns (0,0)-(-1,-1)
Status: draft · Found in: wayland test pass (notes/wine/wayland.md)

## Symptom
vulkan-1:vulkan test (vulkan.c:767 "Got unexpected rect (0,0)-(-1,-1)", for created/resized/hidden
windows) on winewayland.drv with the llvmpipe (lvp) device. The rect is the host's Wayland answer
(extent 0xFFFFFFFF = "defined by the swapchain"); the test expects the client rect. The
currentExtent checks around it pass. 3 failures, no other lvp failures in vulkan-1:vulkan (not compared
against X11: the Xvfb run here had no usable presentation).

## Task
Make the win32u/winewayland Vulkan surface report the client rect for present rectangles
(and device-group present rectangles if affected).
