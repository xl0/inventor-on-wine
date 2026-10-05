# 189 GDI drawn directly on a window (no window surface) is not flushed until an X event or another flush
Status: draft · Found in: 181 (review) · upstream since d3cb94b543e (Oct 2025), integ the same · winex11

## Symptom
A DC without a window surface draws on the X window through gdi_display: client DCs of windows with a client surface
(GL / Vulkan / D3D windows), the desktop DC, windows of other processes. The requests stay in Xlib's output buffer.
`inst/181-review/rv181.exe gdiflush 1` (source next to it; `tests/r181/rv.sh TAG vulkan gdiflush 1`): a thread draws
on a window with a swapchain and then idles in MsgWaitForMultipleObjects; another process reads the screen 500 ms later:
| | FillRect | LineTo | Rectangle outline | ExtTextOut |
|---|---|---|---|---|
| Win11 | shown | shown | shown | shown |
| build/ (integ b5d75449ffe), fix/181 | not shown | not shown | not shown | not shown |
They appear with the next thing that flushes gdi_display: an X event for the thread, a window surface flush, a GPU
present to an offscreen client surface, a new display DC.

## Cause
`X11DRV_ProcessEvents()` ends with `XFlush( gdi_display )`, and until d3cb94b543e (`win32u: Check QS_DRIVER bit before
calling ProcessEvents.`) every message wait of every thread called it. Now it only runs when the thread's X connection
has events. Window surfaces have their own flush (`flush_window_surfaces()` at idle), direct drawing has nothing.

## What 181 did and did not do
fix/181 flushes after the image operations only (`X11DRV_PutImage`, `X11DRV_StretchBlt`, the XRender blits and
AlphaBlend): that is where wined3d's GDI presents (dirty rectangles of COPY-effect swapchains) and the client surface
bits moves land, and a flush is noise next to an image transfer. Its first version flushed in `add_device_bounds()`,
i.e. per primitive, which the review rejected:
- incomplete: pen primitives never get there (`add_pen_device_bounds()` returns early without `dev->bounds`), so LineTo
  and a Rectangle outline still were not shown;
- costly (reviewer's `rv181.exe gdibench`, DC of a swapchain window, :98): PatBlt 4x4 3.0 M -> 0.37 M calls/s, SetPixel
  3 M -> 0.4 M, InvertRect 5 M -> 0.4 M, Xorg CPU x 2.7 (one write() per primitive).
With the final version the benchmark is at base speed (PatBlt 4.0 M, SetPixel 2.9 M, InvertRect 4.8 M calls/s on both).

## Task
Flush gdi_display when a thread that drew directly goes idle, without a syscall per primitive: e.g. a driver entry
called from win32u's idle paths (where `flush_window_surfaces( TRUE )` is called), or a "dirty" flag set by the X11 GDI
entry points and checked there (XFlush with an empty buffer is only a lock). A thread that never waits for messages
(wined3d's command stream thread) is not covered by an idle flush; GdiFlush() (`NtGdiFlush` is a stub) would be the
Windows way for such a caller.
