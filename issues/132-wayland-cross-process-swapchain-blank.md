# 132 winewayland: a D3D11/GL swapchain created by another process on a window is never shown (all WebView2 content is blank)
Status: wip (M0 done, M1 in progress) · Branch: fix/132 (wt/132, on top of fix/134) · Found in: wayland test pass (notes/wine/wayland.md) · Blocks real Inventor use under Wayland

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

## M0 results (2026-10-03; wt/134-build = integ 0b77b17a942 + fix/134, so WITH window-less composition swapchains, 131)
Probe: `tests/r132/xp.c` (`xp.exe host`, `xp.exe foreign|child|hidden|visible [HWND] interval=N ...`; quadrant image with a moving
bar, time per Present, STALL watchdog). Host session (mutter 42, llvmpipe), scratch prefix `inst/132/pfx`. Logs: `inst/132/m0/`, `inst/132/inv/`.
1. Topology B is blank for the reason read from the code: yes. `layered_child_gpu.exe` with +waylanddrv (`lcg.log`): the
   presenting process has win_data for its own child (no wayland_surface, it is a child window), 202 `wayland_client_surface_update`
   and not one `wayland_surface_reconfigure_client`: the attach finds no data for the foreign top-level and detaches.
   Screenshot `lcg.png`: the 400x300 host window is black, no green pixel.
2. A swap with interval > 0 on a surface the compositor never maps blocks forever: yes. `xp.exe hidden interval=1` stalls at
   frame 4; gdb (`hidden-i1-gdb.txt`): wined3d_cs thread in `wayland_drawable_swap` -> eglSwapBuffers -> Mesa -> `wl_display_dispatch_queue`
   -> poll (waiting for a frame callback that never comes). interval=0: 60 presents, 1.1 ms average. Topology B with interval=1 stalls
   the same way (`child-i1.out`); topology A does not, only because win32u keeps no swap interval for a foreign window
   (`get_window_swap_interval` returns 0 for WND_OTHER_PROCESS).
   It is NOT what hurts WebView2: dcomp presents its foreign-target swapchain with `Present(0, 0)` (dcomp/device.c
   present_to_foreign_target), and gdb on two live software-path GPU processes of Inventor shows no thread in eglSwapBuffers.
   General driver bug, drafted as 163 (a vsync'ed swapchain on a hidden window hangs its render thread).
3. Why the hardware-path GPU processes die: a Chromium FATAL, not a Wine crash and not Wayland. Every one of the 9 (3 browser
   processes x `--gpu-recent-crash-count=0..2`, ~2 s after start, restarted every ~8 s) hits the same int3 (`+seh`, same address);
   its Crashpad report says `components\viz\service\display_embedder\skia_output_surface_impl.cc:1277`,
   `backend_format.isValid()=0 backend()=5 color_type=4 sample_count=1 willGlFBO0=1` (no renderable default RGBA8 format in
   Skia's caps of the ANGLE/D3D11 context). The 4th start (`crash-count=3`) takes the software path (`DCompositionCreateDevice(NULL)`,
   `CreateTargetForHwnd(own child)`, composition swapchain) and lives.
   Control: same build, same prefix on Xvfb (winex11, wined3d GL on llvmpipe): the same 9 crashes, then the software path draws the
   Home page and the trial popup (`inst/132/inv/m0x-60s.png`). So the hardware path dies with wined3d's GL renderer (or llvmpipe)
   on X too; on the GPU displays (Vulkan renderer) it lives. Drafted as 162. Consequence for Wayland: WebView content can only
   appear ~25-30 s after its browser process starts (three crash rounds), through the software path = dcomp -> HWND swapchain on
   the GPU process' own child of Inventor's top-level = topology B, Present(0, 0), wined3d GL.
Effect on the M1 plan: none on the design. The swap-interval guard is not needed for the remote path (it never calls
eglSwapBuffers, see below) and would not change what is blank in Inventor; the in-process case is 163.

## State at pause (2026-10-03, machine reboot; nothing of M1 is written yet)
Done: M0 (above). Built: `wt/132` = branch `fix/132` at 5d59ae5fddf (= fix/134, no commits of mine), `wt/132-build` configured
(xkbregistry stand-in, see notes/wine/wayland.md) and fully built (`make` rc 0, winewayland.so present); resume with
`taskset -c 20-59,80-119 make -j40`. Probe `tests/r132/xp.c` (+ exe). Scratch prefix `inst/132/pfx` (1.6 GB, made with wt/134-build
under Wayland; `inst/132/env.sh` sets the env). Drafts 162 (hardware-path CHECK with wined3d GL), 163 (swap interval hang).
Left clean: Wayland session stopped, Xvfb :1330 gone, inv2 back on build/ under :99 without Inventor, lease released.

Not done, in order:
1. M1 code (design below), commits: transport, owner-side proxy, GL readback hook, hardening.
2. Gate: `xp.exe host` + `xp.exe foreign P1` + `xp.exe child P2 color=00ffff` screenshots (quadrants upright: checks the y flip),
   `follow=1` resize, `cycle=200` leak check (fds, handles, wl objects on both sides), kill the renderer mid-stream,
   `WAYLAND_DEBUG=1` for protocol errors, notepad / `d3d11_present.exe` fps unchanged, `xproc_hidden_present`, `layered_child_gpu`.
3. Per-frame cost at 1678x884 on llvmpipe (ms of the readback, 5.9 MB per frame, 3 buffers = 17.8 MB per surface).
4. First look at Inventor on inv2/Wayland (Home page, trial popup, Assistant); expect content only ~30 s after start (162).
5. notes/wine/wayland.md, this file.

M1 design as planned (decided from the code, nothing of it tested):
- Renderer side (process that presents; `wayland_client_surface_update`, called by win32u on window changes and at every present):
  toplevel belongs to another pid -> the client surface gets a "source": a one-page control section (config under a seqlock:
  generation, pixel section handle, buffer size, rect in the toplevel = win32u's `monitor_rect`, visible; a `ready` mailbox;
  owner-written `released` mask) and, per size, a pixel section with 3 buffers (latest-wins: one shown, one in the mailbox, one
  to draw; the renderer never waits). Topology A needs the foreign check BEFORE `wayland_win_data_get(hwnd)`, which fails there.
- GL: `wayland_drawable_swap` for a remote surface does glReadPixels(GL_BGRA) of the back buffer straight into the free shm
  buffer, from a private EGL context of the drawable made current around the read (no client GL state touched, works without a
  current context), and does NOT call eglSwapBuffers (so no frame-callback wait, 163, and no second copy on llvmpipe).
  GL rows are bottom-up: owner sets `wl_surface.set_buffer_transform(FLIPPED_180)` (unknown 6: check on mutter 42; fallback
  GL_MESA_pack_invert or a CPU flip).
- First contact: one driver message posted to the toplevel (`wparam` = renderer pid, `lparam` = its control section handle).
  The owner PULLS: NtOpenProcess(PROCESS_DUP_HANDLE) + NtDuplicateObject of the handles the renderer names (nothing named, no
  server change, the renderer needs no access to the owner). Not yet known: whether Inventor may open the sandboxed GPU process
  that way (wineserver's default process DACL says yes; fallback: named section).
- After that nothing depends on the owner's message pump: the renderer wakes the owner through a socketpair (owner's end passed
  as a Wine handle, `wine_server_fd_to_handle` / `_handle_to_fd`), which the owner's Wayland dispatch thread polls next to the
  display fd (`waylanddrv_unix_read_events` becomes a prepare_read/poll loop). On a wake the owner re-reads the control page:
  new generation -> pull the pixel section, wl_shm pool + 3 wl_buffers; geometry -> subsurface position, viewport destination;
  mailbox -> attach + damage + commit. EOF on the socket = renderer gone -> proxy destroyed (stale image goes away).
  No new thread: a message-pumping driver thread would bring 133 (WaitForInputIdle) back.
- Owner side trusts nothing: values copied once and validated (size caps, 3 * stride * height <= real section size by NtQuerySection
  and fstat before the pool is created, so the compositor cannot fault either), bounded seqlock retries, the fd must be a socket and
  is read with MSG_DONTWAIT, a cap on proxies per process; pixel memory is never mapped in the owner. Proxies live in a list under
  `win_data_mutex`; no win32u calls there (geometry comes from the renderer). Hooks: toplevel wayland_surface destroyed / re-created
  (re-make the wl_subsurface on the new parent, re-attach the last buffer), `WAYLAND_DestroyWindow` of the client window (topology A).
- Known gaps to state in the report: a renderer that stops presenting does not learn that an ancestor in a THIRD process was
  hidden or moved (dcomp presents at least once a second, so WebView2 lags at most ~1 s); no clipping by overlapping siblings;
  proxies are placed just above the parent like in-process client surfaces (popups/menus stay above).
