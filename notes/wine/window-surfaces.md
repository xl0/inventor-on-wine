# Window surfaces, client surfaces, Expose (X11) — checked at wine-11.18-218-g4e819f054dd

- Toplevel GDI content lives in a window surface (win32u/dce.c, winex11
  bitblt.c x11drv_surface_*), owned by the window's process. On X Expose,
  winex11 event.c X11DRV_Expose -> win32u window.c expose_window_surface():
  with a surface it only re-flushes the surface (no WM_PAINT); without one it
  RedrawWindow()s. fix/005 also redraws the exposed part outside the surface
  clip region (client-surface areas) so the app re-presents.
- GL/Vulkan/DXGI content is a client surface (win32u window.c client_surface_*,
  winex11 init.c). "Offscreen" ones (child windows, DPI scaling, foreign-process
  windows, e.g. Chromium's GPU process drawing on the browser HWND) render to an
  X window under the dummy parent and are StretchBlt'ed onto the toplevel X
  window on each present (X11DRV_client_surface_present). Nothing keeps that
  image: an Expose loses it until the app presents again.
- Cross-process pixel format: set_window_pixel_format() on a foreign HWND posts
  WM_WINE_SETPIXELFORMAT; the owner sets clip_clients -> server
  PAINT_HAS_PIXEL_FORMAT -> surface region excludes the client rect
  (server/window.c get_surface_region), i.e. surface clip_region.
- An empty surface clip reaches x11drv_surface_set_clip as count 0 (rects
  non-NULL); clearing it passes rects NULL. fix/006 clips everything on count 0.
- Client surfaces of a *foreign* toplevel are only visible when offscreen
  (needs_offscreen_rendering: child window / DPI scaling); otherwise the client
  X window stays under the dummy parent. A foreign child's pixel format doesn't
  refresh the owner's surface clip until the owner's next SetWindowPos.
- Cross-process GL (wglSetPixelFormat on a foreign HWND) fails; Vulkan/DXGI works.
- Test harness for expose bugs on :98: `xset s on; xset s activate` then
  `xset s off; xset s reset` covers and re-exposes every window. On Xvfb the
  screensaver sends no Expose; map+kill a window instead (`xlogo -geometry
  400x400+0+0 & sleep 1; kill $!`). Xvfb + lavapipe
  (`VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.x86_64.json`) presents fine.
