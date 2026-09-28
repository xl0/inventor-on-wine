# 006 winex11: empty window surface clip region means "no clip"
Status: open (draft) · Owner: - · Branch: - · Found in: issue 005 (Chromium installer UI)

## Symptom
A toplevel whose client area belongs to a client surface (GL/Vulkan, e.g. a
cross-process DXGI swapchain; server PAINT_HAS_PIXEL_FORMAT) gets an empty
surface clip region. win32u window_surface_set_clip() passes the region's rects
with count 0; dlls/winex11.drv/bitblt.c x11drv_surface_set_clip() does
`if (!count) XSetClipMask(None)` -> surface flushes paint the whole window
(white DIB) over the GL content. Evidence: `+bitblt` on the Autodesk installer:
`x11drv_surface_set_clip surface ..., rects 0x..., count 0` for the Chromium
browser window. Clearing the clip is window_surface_set_clip(NULL) -> set_clip(NULL, 0),
so `!rects` distinguishes it.

## Impact
Cosmetic after 005's fix (white flash on expose until the app repaints).
Without 005's fix it is what makes the lost GL content white instead of black.
