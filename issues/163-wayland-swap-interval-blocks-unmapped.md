# 163 winewayland: a swap with interval > 0 blocks forever while the compositor does not show the surface
Status: draft · Found in: 132 M0 (Mesa EGL, old host; NVIDIA EGL on the new host does not block: hidden interval=1 0.26 ms per present) · Component: winewayland.drv opengl.c

## Symptom
`IDXGISwapChain::Present(1, 0)` (or wglSwapBuffers with swap interval 1) on a window that is not shown never returns:
hidden window, child window whose top-level belongs to another process (132, topology B). Likely also minimized or fully
occluded windows (compositors stop frame callbacks for them; not tested).

## Repro
`tests/r132/xp.exe hidden interval=1` (Wayland session, `WINE_D3D_CONFIG=renderer=gl`): "STALL: Present of frame 4 has not
returned". `interval=0` or `visible`: fine (visible interval=1: 16 ms per present).

## Cause
`wayland_drawable_swap` -> eglSwapBuffers; Mesa's Wayland platform throttles with a wl_surface frame callback when the swap
interval is > 0 and waits for it in `wl_display_dispatch_queue` (gdb: `inst/132/m0/hidden-i1-gdb.txt`). A surface without a
role, or one the compositor does not draw, gets no frame callback.

## Fix idea
Never let Mesa wait: always `eglSwapInterval(0)` and throttle in the driver with its own frame callback and a timeout
(what SDL/GLFW/Qt do), or at least use interval 0 while the client surface is not attached to a mapped toplevel (~15 lines,
does not cover minimized/occluded windows). 132's remote path does not call eglSwapBuffers and is not affected.
