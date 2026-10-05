# 181 GPU-presented child windows go stale on screen when their toplevel is resized (offscreen client surfaces)
Status: draft · Found in: 174 (probes `tests/r174/`) · integ b5d75449ffe, same without 077 / 130 · X11, NVIDIA, wined3d Vulkan

## Symptom
A child window that a GPU API presents into is an offscreen client surface on winex11 (window-surfaces.md): the driver
presents into an X window under the dummy parent and copies it onto the toplevel X window. That copy is only refreshed
by the next full present (and by fix/061 on Expose). After the toplevel is resized (by the WM or not) such children
show old or foreign pixels until the application presents the whole window again. Applications that present once per
change (WPF, anything that renders on WM_SIZE / WM_PAINT only) stay wrong; hovering repairs the rectangles WPF redraws.
![grown view](attachments/181-grown-view-strip.png) ![moved pane](attachments/181-moved-pane-not-shown.png)
Left: D3D11 view (yellow) after a growth, 50 ms layout. Right: WPF panes, the status bar (blue) only moved.

## Defects (each reproduced; awesome + picom on :98, also without picom and with `XSIZE=1` = no WM grab)
Common: `export WINEPREFIX=... WINEBUILD=$PWD/build DRI_PRIME=pci-0000_ca_00_0 WINE_D3D_CONFIG=renderer=vulkan SCALE=1.5`
(prefix with LogPixels 144; `SCALE` unset at 96).
1. **First present after a growth leaves the new strip undefined.**
   `tests/r174/drive.sh 98 OUT 20 super 15 10 0 -- 600 100 100 900 700 dpi plain` -> `view` bad in 10 of 10 growths.
   Timeline (`WINEDEBUG=+x11drv,warn+vulkan,warn+d3d`): child resized -> `client_surface_update_geometry` resizes the
   offscreen X window (new area = what the server copied from the screen) -> the application's Present still goes to
   the old-size Vulkan swapchain: `win32u_vkQueuePresentKHR Swapchain size 607x473 does not match client rect
   (0,0)-(684,512), returning VK_SUBOPTIMAL_KHR` -> `X11DRV_client_surface_present` copies 684x512 from the offscreen
   window, of which only 607x473 was presented -> wined3d (`swapchain_vk_present`) recreates the swapchain on
   VK_SUBOPTIMAL_KHR but presents again only on VK_ERROR_OUT_OF_DATE_KHR. When the host driver itself answers
   OUT_OF_DATE (it sometimes does) the frame is right. GL renderer: 2 of 10.
2. **A moved child is not shown at its new place.**
   `XSIZE=1 DXMUL=0 PROBE=wpf.exe tests/r174/drive.sh 98 OUT 6 super 3 50 0 -- 600 100 100 600 400 nogpu hosted partial`
   (WPF 4.8 prefix, hardware rendering) -> `status` bad in 2 of 3 growths: the pane keeps its size, so WPF doesn't
   present; win32u's `move_window_bits()` copies inside the window surface (DCX_WINDOW DC), which has no pixels of a
   client surface child and is clipped out of its area anyway. Windows moves the child's bits.
3. **Partial presents never reach the offscreen X window, and are not flushed.** wined3d presents a D3D9 COPY-effect
   swapchain with a partial destination rectangle (WPF's dirty rectangles) with `swapchain_blit_gdi()`: a BitBlt on the
   window DC from the command stream thread. For a window with a client surface that DC draws on the toplevel X window
   directly (`X11DRV_GetDC`, no window surface) through gdi_display, which is only flushed by threads that process X
   events (`X11DRV_ProcessEvents`) or by a surface flush / full present.
   `DXMUL=0 PROBE=wpf.exe tests/r174/drive.sh 98 OUT 10 super 10 20 0 -- 600 100 100 600 400 nogpu hosted partial`
   -> the square is still black 1.6 s after the resize in 7 of 10 (5 of 10 on the build without 077); it stays black
   for >= 5 s with the pointer away and turns red the moment the pointer moves over another pane.
   By the code (not reproduced separately): the offscreen window keeps the last *full* frame, so fix/061's
   `present_offscreen_client_surfaces()` on Expose, and any fix of 2 that presents again, bring back a frame without
   the later dirty rectangles (after a ribbon tab switch: the old tab's content).

## Fix directions
- 1: wined3d presenting again after VK_SUBOPTIMAL_KHR from a size mismatch (one line), or win32u copying only the
  swapchain's extent (stretched to the window, as DXGI / D3D9 do on Windows) until the swapchain matches.
- 2 and 3 need the offscreen X window to be the single source of the child's pixels: GDI drawing on a window with an
  offscreen client surface should land in it (and be copied + flushed when the DC is released), then moves / Expose can
  simply present again (`update_client_surfaces()` already walks the toplevel's surfaces after every window change).
  Copying the on-screen pixels of the moved rectangle (XCopyArea on the toplevel, like `X11DRV_MoveWindowBits`) is the
  smaller alternative for 2 and keeps partial presents, but not across Expose.
- Whatever is chosen: gdi_display requests issued by a thread that never processes events need a flush.

## Relevance
Inventor's viewport (D3D11 child) would show 1 until its next frame after a growth (not looked at in Inventor). Its WPF
panes are most likely software-rendered (Autodesk sets `RenderOptions.ProcessRenderMode = SoftwareOnly`, see 174), so 2
and 3 concern other WPF / D3D9 applications and anything that presents dirty rectangles; WebView2 children are other
processes' client surfaces and were not tested.
