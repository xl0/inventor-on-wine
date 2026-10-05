# 187 wined3d GL: the first present right after ShowWindow is lost in about a quarter of the runs
Status: draft · Found in: 181 (while repeating 078's checks) · integ b5d75449ffe (build/) and fix/181 alike · X11, NVIDIA, wined3d GL renderer only

## Symptom
`WINE_D3D_CONFIG=renderer=gl wine tests/expose_present.exe show` (D3D11 child of a top-level that is shown right before
the first Present; the child ignores WM_PAINT): the screen shows the window background (ffffff) instead of the first
frame 500 ms later; the next present shows. Exit 1.
:98 (NVIDIA, awesome 4.3, no compositing manager, 96 DPI), 30 runs each: build/ 23 ok, fix/181 22 ok. Vulkan renderer:
30 of 30 on both. Modes without the late ShowWindow (plain, `nopump`): 10 of 10 on both. 078 had it passing on
openbox (:100, :101) for both renderers.

## Guess (not verified)
The swap reaches the offscreen X window and is copied to the toplevel before the WM has mapped it (awesome reparents
and maps later than openbox); the Expose of the map then presents again (061), unless it arrives before the swap. Or the
GLX drawable of the just-shown window isn't ready. Check with the same loop on openbox and with `WINEDEBUG=+x11drv`
timestamps of MapNotify / Expose / the first `X11DRV_client_surface_present`.
