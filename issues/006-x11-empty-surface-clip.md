# 006 winex11: empty window surface clip region means "no clip"
Status: fixed · Owner: worker-006 · Branch: fix/006-x11-empty-surface-clip (on master) · Found in: issue 005 (Chromium installer UI)

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

## Outcome
Fix 48c0d96bed3: x11drv_surface_set_clip() tests `!rects` instead of `!count`;
count 0 goes to XSetClipRectangles(n=0) = clip everything (xrectangles_from_rects
relies on malloc(0) != NULL, true on glibc/macOS).
Repro: `tests/xproc_swapchain_expose.c` on Xvfb (lavapipe, renderer=vulkan):
popup + child process presenting on it and on a child window, then an xlogo
window mapped over it and killed. master: popup grey (245) after the expose;
fixed: black (content lost, not re-presented = issue 005, no white surface).
No conformance test: X expose behaviour, not observable through Win32.
Side observations (not filed): owner's surface clip isn't refreshed when a
foreign child gets a pixel format (repro forces SetWindowPos); offscreen
present on lavapipe/Xvfb shows R/B swapped.
