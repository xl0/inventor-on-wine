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

## Design study (2026-10-03, code reading + inst/wayland logs; nothing run)
Corrections to the above:
- The GL drawable IS created on the foreign HWND (`inst/wayland/xswapB-trace.log`: `wayland_opengl_surface_create
  client=... Created drawable`); wined3d doesn't fall back to its backup DC. Each Present ends in `wayland_drawable_swap`:
  `wayland_client_surface_update`/`_present` return early (process B has no `wayland_win_data` for A's window) and
  `eglSwapBuffers` commits to a wl_surface that never gets a role, so the compositor never maps it.
- `wl_xswap` (swapchain on a foreign HWND, "topology A") is not what WebView2 does. Chromium presents on its OWN child
  HWND whose top-level is foreign ("topology B"); our dcomp then does `present_to_foreign_target` (dcomp/device.c):
  an HWND swapchain on that child, and `wayland_client_surface_attach` finds no data for the foreign top-level and
  detaches. Probes for B exist (`tests/layered_child_gpu.c`, `xproc_hidden_present.c`) but read the screen with
  GetPixel: use `x/wshot.sh` on Wayland.
- On Wayland the WebView2 GPU processes seem to crash into the software path (inventor5.log: 12 gpu-process starts,
  9 DCompositionCreateDevice3, the 3 CreateSwapChainForComposition come from processes without Device3). Cause unknown.
GDI is no escape: a DC only gets a surface if the top-level is in-process (win32u/dce.c update_visible_region), the
Wayland driver has no pGetDC, win32u has no generic offscreen fallback (client_surface.offscreen is winex11 only).
X11 gets this for free: X window ids are global and the client window is XComposite-redirected onto the top-level.
Upstream has no cross-process work for non-X11 drivers (winemac has FIXMEs for the same gap). xdg-foreign only parents
top-levels: no embedding, positioning or clipping, so it is no solution here.

Candidate designs:
- A. Owner-side proxy subsurface over shm (recommended): the renderer reads back each frame (GL glReadPixels; Vulkan
  image copy) into a named section (header + 2-3 buffers) and posts a driver message to the top-level's process, which
  makes wl_shm buffers from the section fd (the driver already does that for window surfaces), attaches them to its own
  subsurface and commits. winewayland.drv only (+ Vulkan readback in win32u/vulkan.c), no wineserver change.
  450-550 lines GL-only, +200-300 hardening, +350-450 Vulkan. One GPU->CPU readback per frame (5.9 MB at 1678x884):
  fine for pages that present on damage, poor for video/4K. Frames wait on the owner's message pump; no clipping by
  overlapping siblings. Upstreamable: low.
- B. Shared window surfaces in win32u (1500+ lines, touches every driver, likely a server change): right in principle,
  not from our tree.
- C. dmabuf zero-copy via D3DKMT global handles + linux-dmabuf on top of A's proxy (1500-2500 more): can't be verified
  here (mutter composites in software).
Stages for A: M0 settle unknowns 1-3 (no code); M1 GL readback + proxy, gate = `wl_xswap` magenta and a topology-B probe
show their colour in screenshots; M2 Inventor Home page/Assistant/trial popup (resize, show/hide/reparent, several
surfaces per top-level, GPU-process restart, swap-interval guard); M3 Vulkan readback (lavapipe); M4 dmabuf, optional.
Unknowns: (1) does the GPU process block in eglSwapBuffers on a never-mapped surface with swap interval > 0 (its own
hidden composition popup gets interval 1; Mesa waits for a frame callback) — could be what kills the hardware path;
gdb `thread apply all bt` on the GPU pid, or d3d11_present on a hidden window with Present(1,0); (2) why the
hardware-path GPU processes die (Crashpad reports in the prefix, see notes/wine/dcomp.md); (3) is topology B blank for
the reason read from the code (layered_child_gpu.exe with +waylanddrv and a screenshot); (4) Vulkan FIFO on a
foreign-rooted surface; (5) can the sandboxed GPU process create a named section the owner opens (fallback: D3DKMT
global handle); (6) flipped buffer transform on a subsurface in mutter 42; (7) does Inventor's UI thread pump often
enough, and do posted messages disturb idle detection (088, 133); (8) cursor over cross-process children (085's fix is
X11-only); (9) sibling child windows overlapping WebView areas.
