# 132 winewayland: a D3D11/GL swapchain created by another process on a window is never shown (all WebView2 content is blank)
Status: draft · Found in: wayland test pass (notes/wine/wayland.md) · Blocks real Inventor use under Wayland

## Symptom
Under winewayland.drv every WebView2 (Chromium) surface of Inventor is blank: the Inventor Home page (Recent
documents, New/Open), the Autodesk Assistant pane (white) and the trial welcome popup (white 640 px + black
remainder, never gets its content). The same prefix and build under X (winex11.drv) renders all of them.
`--disable-gpu` in WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS does not change it. Native Inventor UI, ribbon, dialogs and
the 3D viewport (swapchain in Inventor's own process) are fine.
Chromium's GPU process presents into the "Intermediate D3D Window" child of the browser process (WS_EX_LAYERED |
WS_EX_NOREDIRECTIONBITMAP | WS_EX_TRANSPARENT, Inventor.exe owns the window, the swapchain belongs to another process).

## Repro (standalone, no Inventor)
`tests/wl_xswap.c` (build: `x86_64-w64-mingw32-gcc -o tests/wl_xswap.exe tests/wl_xswap.c -ld3d11 -ldxgi -luser32 -ldxguid`)
```
x/wayland.sh start; eval "$(x/wayland.sh env)"; export WINEPREFIX=...
wt/wayland-build/wine tests/wl_xswap.exe > A.log &          # prints child=HWND
wt/wayland-build/wine tests/wl_xswap.exe HWND               # D3D11 swapchain on A's child, clears magenta for 12 s
x/wshot.sh out.png
```
Wayland: A's window stays empty. X (`DISPLAY=:101 build/wine`, same two commands): magenta fills A's client area.
Same swapchain on a child window of the SAME process (`tests/wl_childswap.exe [layered] [popup]`, also with the
Chromium-style layered child) is shown on Wayland, so layered/child/NOREDIRECTIONBITMAP are not the problem; the
process boundary is.

## Evidence
- inst/wayland/evidence/132-xswap-wayland.png (empty) vs 132-xswap-x11.png (magenta).
- inst/wayland/evidence/132-inventor-home-wayland.png (blank Home area, white Assistant pane) vs 132-inventor-home-x11.png.
- `WINEDEBUG=+waylanddrv` of the presenting process B: the GL drawable is created on wined3d's "WineD3D fake window"
  (`wayland_opengl_surface_create client=0x303de ... Created drawable`), never on the target HWND 0x503f2 of process A
  (no `wayland_win_data`/`wayland_client_surface_update` for it). Same log for the same-process case shows the drawable on
  the child HWND. (inst/wayland/xswapB-trace.log, childswap-trace.log.)

## Open
Component (guess): winewayland.drv. A wl_surface/subsurface can only be attached in the connection that owns the
parent surface; process B has no win_data for A's window, so its present has no destination. winex11 works because X
window ids are global. Needs a cross-process path (e.g. B renders into a shared buffer that A's driver
attaches, or wineserver-mediated client surface). Also affects any DXVK/Vulkan app presenting into a foreign HWND.
X-side background: notes/wine/window-surfaces.md ("offscreen" client surfaces of foreign-process windows, StretchBlt'ed onto
the toplevel on each present; issues 027, 061). winewayland has no equivalent; win32u's `needs_offscreen_rendering`/foreign pixel-format
machinery is driver hooks (`client_surface_*`), the Wayland driver does not implement the foreign case.
Untested: whether the Vulkan path (WINE_D3D_CONFIG=renderer=vulkan is not usable on Wayland here, see notes) behaves the same.
