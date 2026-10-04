# 132 winewayland: a D3D11/GL swapchain created by another process on a window is never shown (all WebView2 content is blank)
Status: M1 + M2 + review fixes done (GL path, topology B only), 4 commits on fix/132 (integ e00a74f6590), tip 3b3fb1a465e; topology A (swapchain on a window owned by another process, `wl_xswap`) stays blank by design; multi-document views are blocked by 167 (in-process, on integ too) · Found in: wayland test pass (notes/wine/wayland.md)

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

## M0 redone on the new host (2026-10-04; build/ = integ e00a74f6590, gnome-shell/mutter 50.1, NVIDIA 595.91.07, modeset=1)
Logs: `inst/132/m0b/`, `inst/132/inv/m0b-*`. The session is GPU-accelerated now, on both sides:
- Compositor: mutter creates gbm renderers on all four render nodes, primary renderD130, no "not hardware accelerated" line;
  `wayland-info`: linux-dmabuf main device renderD130 with NVIDIA modifiers, `wl_eglstream_display`.
- Clients: `eglinfo -p wayland` = NVIDIA EGL 1.5, "NVIDIA RTX 6000 Ada"; Wine (`+wgl`) reports vendor NVIDIA Corporation.
  `d3d11_present.exe` renderer=gl: ok, 3000-5000 fps (adapter name is wined3d's fallback "GTX 470").
  renderer=vulkan: device on the RTX 6000 Ada, but Present on a VISIBLE window stalls for seconds (8 s, 19 s; `xp.exe visible`,
  gdb: `vkAcquireNextImageKHR` -> NVIDIA -> `drmSyncobjTimelineWait`); hidden windows present fine. Draft 166. So Wayland still
  means renderer=gl here, now on the GPU.
Answers:
1. Topology A and B are still blank (`xp-before.png`: host + yellow panels, no renderer colour). Same code, same reason.
2. The interval > 0 hang does NOT happen on NVIDIA's EGL: `xp.exe hidden interval=1` 60 presents, 0.26 ms average; topology B with
   interval=1 150 presents, 0.11 ms. 163 is Mesa-only (visible interval=1 paces at 15 ms on both).
3. The hardware-path GPU processes do NOT die on NVIDIA GL: Inventor started 3 GPU processes, all `--gpu-recent-crash-count=0`,
   no int3, all alive after 60 s; each does `DCompositionCreateDevice3(dxgi device)` + `CreateTargetForHwnd(own child)`.
   162 is llvmpipe (or Mesa) specific. The Home page and the trial popup are blank all the same (`inst/132/inv/m0b-60s.png`).
   What M1 has to carry is unchanged: dcomp composes the visual tree into its HWND swapchain on the GPU process' child
   (`present_to_foreign_target`, Present(0, 0)) = topology B through wined3d GL, now NVIDIA EGL instead of Mesa/llvmpipe.
   Vulkan first would not help: Wayland runs renderer=gl (166), and dcomp's swapchain uses the same wined3d device.
Design consequences: the readback must not lean on Mesa behaviour (private EGL context on NVIDIA's EGL, skipping eglSwapBuffers,
resizing a wl_egl_window that never swaps); to be verified on NVIDIA. Per-frame costs below are NVIDIA numbers unless marked.
x/wshot.sh needed a fix for shell 50 (its screenshot allow-list no longer has org.gnome.Screenshot; we own
org.gnome.SettingsDaemon.MediaKeys on the private bus now).

## M1 as built (2026-10-04, fix/132 = integ e00a74f6590 + 4 commits, winewayland.drv only, +998 lines)
Commits: d860a3d42e2 (event thread: prepare_read + poll instead of wl_display_dispatch_queue), 1bd265cb940 (sink: owner side),
b01483372eb (source: presenting side), 6f743d48169 (GL readback). New file `dlls/winewayland.drv/wayland_remote.c`.
Terms: source = the process that presents; sink = the process that owns the top-level.
- Who allocates: the source. Per client surface whose top-level is in another pid (decided in `wayland_client_surface_update`,
  before the win_data lookup that fails for a foreign HWND): a one-page control section (`struct remote_shared`: config under a
  sequence lock = pixel section handle, generation, buffer size, rect in the top-level (win32u's `monitor_rect`), visible; a `ready`
  mailbox; the sink's `released` mask and `attached` flag), a pixel section with 3 XRGB buffers per size (new section + generation
  on every size change), and a socketpair whose sink end is turned into a Wine handle (`wine_server_fd_to_handle`).
- What crosses: one driver message `WM_WAYLAND_REMOTE_SURFACE(pid, control section handle)` posted to the top-level (first contact
  only). Everything else is pulled by the sink: `NtOpenProcess(PROCESS_DUP_HANDLE)` + `NtDuplicateObject` of the handles named in the
  control page (control section, socket, pixel sections). Nothing named, no wineserver change. Works with Chromium's sandboxed GPU process.
- Who attaches: the sink, in its Wayland event thread, which polls the sockets next to the display fd: on a wake it re-reads the
  control page, pulls a new pixel section if the generation changed (wl_shm pool from the section fd + 3 wl_buffers), sets the
  subsurface position / viewport destination, takes the mailbox and attach + damage + commit. Socket EOF = source gone -> sink destroyed.
  Frames, geometry, resizes and source death therefore do not need the owner's message loop; only the first contact does (measured:
  `xp.exe host busy=10`: a running source keeps animating while the UI thread sleeps; a source started meanwhile appears when it pumps again).
- Buffers: latest-wins triple buffering, the source never waits (one shown, one in the mailbox, one being drawn; an unconsumed
  mailbox frame is taken back). The sink reports wl_buffer.release in `released`. Mailbox values carry the generation.
- GL: `wayland_drawable_swap` -> `wayland_drawable_present_remote`: glFinish in the client context, then glReadPixels(GL_BGRA) of the
  back buffer into the shared buffer from a private EGL context of the drawable (client GL state untouched; works with no current
  context); eglSwapBuffers is not called for remote surfaces. Rows are bottom-up: the sink sets
  `wl_surface.set_buffer_transform(FLIPPED_180)` (works on mutter 50; unknown 6 settled there, other compositors not tried).
  Resizing the never-swapped wl_egl_window works on NVIDIA's EGL (`follow=1`).
- Locks: sinks live under `win_data_mutex` (new `wayland_win_data_lock/unlock`), no win32u calls under it (geometry comes from the
  source; only ntdll server calls: NtDuplicateObject, NtQuerySection, handle_to_fd). wl_buffers (they have listeners) are created and
  destroyed only in the event thread; other threads mark a sink dirty/dead and wake it through an eventfd. Source side: one
  `source_mutex`, taken after win32u's `surfaces_lock`, held across the readback; the first-contact PostMessage is sent after releasing it.
- Sink hooks: `WAYLAND_DestroyWindow` (top-level or client window), `wayland_surface_destroy` (wl_subsurface dropped, re-made on
  the new surface with the last frame re-attached), `WAYLAND_WindowPosChanged` / fractional scale (re-evaluate).
- Validation in the sink (source = untrusted): config copied once per wake with bounded seqlock retries; width/height 1..16384 and
  3 * w * h * 4 <= INT_MAX; the section must be a section of at least that size (NtQuerySection) and its fd a regular file of at
  least that size (fstat) before the pool is created, so the compositor cannot fault; rect limited to +-32767; mailbox index 1..3 and
  matching generation; socket fd must be a socket, read with MSG_DONTWAIT; at most 64 sinks per process; bounded CAS loop on the
  shared mask; the pixel memory is never mapped in the sink.

### Gates (host session: mutter 50.1 + NVIDIA 595 EGL, renderer=gl; outputs in `inst/132/m1/`)
- `wl_xswap.exe`: magenta fills A's client area (632x446 at 644,346; `gate-xswap.png`). Topology B: `xp.exe child` and
  `layered_child_gpu.exe` (green 300x200 in the black host, `gate-lcg.png`), `xproc_hidden_present` (green before the hide, none after).
  Both topologies side by side with upright quadrants: [132-xp-topologies-m1.png](attachments/132-xp-topologies-m1.png).
- Geometry / lifetime (`tests/r132/geo.sh`, 11 screenshots `geo-N.png`): panel moved + resized (swapchain follows), panel hidden/shown
  (both topologies), top-level hidden/shown, top-level resized, renderer killed with -9 (image gone, owner's panel visible again),
  client window destroyed. Role change of the top-level (`xp.exe host flip=8`: subsurface popup -> managed toplevel, new
  wl_surface): both sinks re-attach, an idle source gets its last frame back.
- Protocol errors: none in a WAYLAND_DEBUG=1 run of all of the above (5091 protocol lines of the owner).
- Leaks, 200 create/destroy cycles (`tests/r132/leak.sh child|foreign`): owner fds 24 -> 24, maps 390 -> 390, handles 32 -> 32;
  renderer handles 36 after 10 cycles, 36 after 200, fds 123 for 20/100/200 cycles; wl objects of the owner balanced
  (surfaces 155 created / 145 destroyed = its own 10 windows; pools 80/80; buffers 236/234 = its own 2).
- In-process unchanged: `d3d11_present.exe` 5950-6280 fps on fix/132 vs 5330-6050 on integ (5 runs each); notepad draws and types.
- Hostile source (`tests/r132/evil.exe TOP [case]`, the protocol spoken from Win32 with bad data): bogus pid/handles, 200 first
  contacts (64 accepted, the rest refused, all gone when the process exits), wake handle that is a file, 2^31 x 2^31 buffers, section
  smaller than the buffers, section that is an event, sequence lock held forever, frame indices out of range, absurd rects,
  20000 generation changes in 2.5 s (0.93 s CPU in the owner): the owner survives all, no handle/fd growth, honest sources still show.

### Cost (NVIDIA RTX 6000 Ada, NVIDIA EGL 595.91.07, wined3d GL; 1678x884 = 5.93 MB per frame)
- Source, per presented frame: 0.93 ms back to back (glFinish 0.16 + context switch 0.03 + glReadPixels 0.70 + restore 0.04);
  4.2 ms when frames are 30 ms apart (finish 2.0 + readpixels 2.0: the GPU idles in between). One copy GPU -> shm, nothing when
  nothing is presented. Not measured on llvmpipe (the session no longer runs on it).
- Memory: 3 buffers = 17.8 MB shared per surface (+ one page), plus the compositor's texture.
- Sink: 0.15 ms CPU per frame (owner process, 400 frames). Compositor (wl_shm upload): 3.75 ms CPU per frame vs 0.95 ms for the
  same content presented in-process (EGL/dmabuf).
- Present() as seen by the app is unchanged (wined3d presents from its CS thread): 4.19 ms per frame overall for the probe's
  5.9 MB upload + present remotely vs 4.25 ms in-process visible.

### Inventor first look (inv2, Wayland, renderer=gl on NVIDIA, final build; `inst/132/inv/m1b-*`, no M2 fixing)
- The Home page and the trial popup show their content in the first screenshot that has the main window (25 s after start; at 15 s it was not up yet);
  [132-inventor-home-wayland-m1.png](attachments/132-inventor-home-wayland-m1.png). Hover highlights follow the pointer, clicking the
  popup's X closes it (input goes through the top-level as before). The Assistant pane shows its content (in the first run it was black right after
  it was opened by command, in two screenshots 10 s apart, and fine after the next scenario ran; cause not looked at). No transport warning in the logs,
  `invscen part` and `view` pass. Inventor may open the sandboxed GPU process with PROCESS_DUP_HANDLE.
- BREAKS, and worse than before M1: the Home page is an MDI child that stays visible BEHIND the active document; on Windows/X11
  the sibling on top clips it, the sink is not clipped, so the Home page covers the 3D view of every open document
  ([132-inventor-home-covers-document-m1.png](attachments/132-inventor-home-covers-document-m1.png); before M1 the Home page was
  blank but documents were visible). Same class: Chromium's GPU window is larger than its parent (1920 wide in a 1327-wide parent
  after the Assistant pane opens) and relies on parent clipping.
- Three Mtk-CRITICAL `mtk_region_ref: assertion 'region != NULL'` lines in the compositor log during the first run's document open,
  none in the second run or in any probe run: not attributed.

### Not done / M2, in order
1. Clipping and owner-side geometry (blocker for Inventor, see above). Plan: let the SINK evaluate its client window on every
   `WAYLAND_WindowPosChanged` of a window in that top-level (outside win_data_mutex): visible, rect in the top-level, and the visible
   region (`NtUserGetDCEx` + SYSRGN; the server already clips by parents and siblings) -> empty = hide, else crop to the bounding
   box with the viewport source rect; non-rectangular regions need alpha holes (ARGB buffers) or stay unclipped. That also closes
   the idle-source gap below; the source then only has to send frames and the buffer size.
2. Idle source: geometry and visibility come from the source at its presents and at its own window changes. If the owner hides,
   moves or re-frames a window while the source does not present, the sink keeps the old state until the next present
   (`inst/132/m1/idle-*.png`: both topologies stay visible after the owner hides the panels). A third process in the parent chain
   (WebView2's browser process) is seen by neither side.
3. Z-order: sinks sit just above the parent surface like in-process client surfaces; popups in the subsurface role (tooltips, 145)
   and other client subsurfaces are not ordered against them. Two client surfaces of one window (win32u keeps an unused one) give
   two sinks; harmless so far because the unused one has no frame.
4. Pump dependence of the first contact (a source created while the owner's UI thread is busy appears when it pumps again).
5. Cursor over cross-process children (unknown 8), not looked at.
6. M3 Vulkan: `wayland_client_surface_lock/unlock_remote_buffer` are renderer-independent; win32u/vulkan.c needs the readback.
   Until then a Vulkan swapchain on a foreign-rooted window creates a source and a sink without frames (blank as before).
   Blocked in practice by 166 anyway.
7. Other compositors (KWin, sway, wlroots: FLIPPED_180 on a subsurface, buffers held until replaced) and Mesa/llvmpipe (private
   context on a swrast surface; 163 does not apply since nothing swaps): not run, the VM belonged to 157.

### Weak spots (for the review)
- The sink trusts `sink->hwnd` only for clean-up, but any process of the session can post the first-contact message for any
  top-level and take sink slots (64 per process, no per-source limit): denial of display for honest sources, not memory safety.
- A source can keep the owner's event thread busy (one bounded update per wake, pool creation per generation change: 47 us each
  in the flood test) and make the compositor map and upload up to 2 GB per pool; it could do the latter through its own connection too.
- PROCESS_DUP_HANDLE pull: the sink duplicates whatever handle value the source names, with the source's access; it only keeps
  sections of sufficient size and sockets. A handle to something else is closed again.
- Destroying wl_buffers of the previous generation while one may still be shown relies on compositors copying wl_shm content
  at commit (true for mutter); spec-wise the content is undefined until the next frame, which arrives with the resize.
- glFinish + a second context per frame: correct by the spec, measured only on NVIDIA.
- `wayland_win_data_lock()` is held during the sink update including server round trips (dup, query, fd) on generation changes.
- The poll loop replaces wl_display_dispatch_queue() for every process of the driver.

## M2 as built (2026-10-04, +2 commits: 6d3ae2fb1df geometry and clipping, 9c7f21021d4 per-process limit)
What changed against "M1 as built" (the items 1, 2 and the slot weakness of the lists above are done; they are kept as history):
- The sink no longer uses a position from the source. `struct remote_shared` lost `rect` / `visible`; the source only bumps `seq`
  (+2) and wakes when its view of the window changes, as a "look again" signal.
- Geometry comes from the server, per sink, in the sink's event thread and WITHOUT `win_data_mutex`: two plain
  `get_visible_region` requests (client window: client rect + visible region, both in screen coordinates, and its top window;
  then the top window's client rect). No win32u call at all, so nothing can take the user lock. The region is what GetDC would
  clip to: parents' client rects and, where the styles ask for it (WS_CLIPSIBLINGS on the window or an ancestor), the siblings
  above. Flags 0: children of the client window do not clip.
- Placement: bounding box of the region -> subsurface position and `wp_viewport` destination; the part of the buffer that
  belongs to it -> `wp_viewport.set_source`, computed as fractions of the client rect times the size of the buffer that is
  ATTACHED (tracked separately from the current generation: a source outside the committed buffer is a fatal protocol error);
  empty region, foreign or missing top window -> the subsurface is destroyed (hidden). Driver coordinates may be scaled: the
  server rects are mapped with client-rect ratio of the toplevel (`data->rects.client` vs the server's), identity here (100%).
- When is it evaluated: `sink->dirty` (set by `WAYLAND_WindowPosChanged` of ANY window whose root is the sink's toplevel, by
  `WAYLAND_SetWindowStyle` for WS_VISIBLE/CLIPSIBLINGS/CLIPCHILDREN, by the new `WAYLAND_SetParent` for the old root, by surface
  re-creation and scale changes) or a changed `seq` of the source. Frame-only wakes use the stored geometry and commit only the
  sink surface (the parent is committed when the position changes, not per frame as in M1).
- Re-homing: if the server says the client window now sits in another toplevel of this process (pane undocked / docked), the
  sink moves there at once; the source re-creates itself at its next present (seen 20 ms later in Inventor).
- Limits: 16 sinks per source pid (64 per process as before).
- Event thread: one bounded update per sink per loop iteration, display events are read and dispatched before each round.

### Gates, M2 build (same session; `inst/132/m1/`)
- `tests/r132/clip.sh` (13 steps, sources idle = every step is owner-side only): panel moved; panel made smaller than the
  source's child (cropped to the parent: only the top-left quadrant shows); panel resized (buffer stretched); sibling over the
  right half (cropped, the other panel loses the 10 px it covers); sibling over all of it (hidden); panel raised above the
  sibling (back); panel pushed over the toplevel's edge (cropped to 100 px); owner hides / shows the panel; toplevel hide / show;
  panel destroyed. All as expected.
- `geo.sh` (live sources) as in M1. Role flip (`host flip=8`) with an idle source: the idle sink is now at the right place after the
  role change (M1: 22 px off). 30 resizes of two half-covered panels with live `follow=1` sources: 348 set_source over 64 buffer
  generations, no protocol error.
- WAYLAND_DEBUG=1 on the owner for clip.sh, the role flip, the resize stress and the hostile cases: 0 protocol errors.
- Leaks, 200 cycles: owner fds 24 -> 24, maps 387 -> 387, handles 32 -> 32 (child); 24/388/32 (foreign); renderer handles 36 / 39 flat.
- Hostile source (`evil.exe`, updated for the new block; new case `hwnd`: bogus, desktop and own-process client windows): owner
  survives all; `msg`: 16 of 200 sources accepted. Flood of generation changes + window-change notifications on one panel while
  an honest source animates in the other: the honest bar keeps moving in 4 screenshots 2.5 s apart, the owner's UI thread keeps
  its 5 s prints, owner CPU 15 % of a core.
- In-process: d3d11_present 5260-5900 fps.

### Cost, M2 build (NVIDIA RTX 6000 Ada / NVIDIA EGL, 1678x884, 5.93 MB per frame; the machine was busier than at the M1 run)
- Source: unchanged code, 0.93 ms per frame back to back, 4.2 ms at 30 ms spacing (M1 numbers).
- Sink: 0.35 ms CPU per frame (M1 run: 0.15); compositor 5.8 ms CPU per remote frame vs 1.6 ms in-process (M1 run: 3.75 vs 0.95).
- Geometry: 2 server requests per evaluation; Inventor session of ~8 min with five scenarios, tab switches, pane and window
  operations: 7120 evaluations, none per frame.

### Inventor on inv2 / Wayland, M2 build (`inst/132/inv/m2-*`)
- Start: Home page, Assistant pane and trial popup with content; the Home page ends at the Assistant pane now (Chromium's GPU
  window is wider than its parent, e.g. 1569 px in a 1327 px parent with a document's browser pane open, and is cropped): [132-m2-home-clipped-to-parent.png](attachments/132-m2-home-clipped-to-parent.png).
- Open part (`invscen view`): the Home page sink is hidden, the 3D view shows:
  [132-m2-document-visible.png](attachments/132-m2-document-visible.png). Home tab clicked: Home page back; document tab: hidden
  again; all documents closed (`hello`): Home page back. MDI children have WS_CLIPSIBLINGS, so the server region does it.
- invscen hello 2/2, part 11/11, asm 10/10, drawing 8/8, view 3/3 PASS, no dialogs.
- Assistant pane: closed with its X -> hidden at once; reopened by command; undocked by double-click -> floats as its own
  toplevel with content ([132-m2-assistant-undocked.png](attachments/132-m2-assistant-undocked.png)); docked again -> back in
  the main window. Main window restored, resized (1300x800), maximized, restored, maximized: both sinks follow.
- Open drawing next to the part: NOT fixed and not 132: the drawing is active but the part's 3D view (an in-process GL client
  surface) stays on top; same on integ. Draft 167. It also hides the Assistant sink after a resize.
- Visible region shape: 9 of 7120 evaluations had more than one rectangle (the Home window, bounding box = the whole window, a
  transient): rect clip + hide is what Inventor needs; non-rectangular clipping is not implemented.
- Z-order (investigated only): the File (application) menu shows above the Home page; a ribbon tooltip that reaches down over
  the Home page is cut off at the page's edge = it is under the sink
  ([132-m2-tooltip-under-home.png](attachments/132-m2-tooltip-under-home.png)); the Assistant pane's "Help" menu and the
  "closed" balloon over the 3D view show. Same mechanism as 145: popups in the subsurface role are re-placed directly above the
  parent surface, below every sink / client surface (see 167 for the fix direction). Does not fall out of the clipping work.
- Compositor log: `Mtk-CRITICAL mtk_region_ref` lines appear per opened document on integ as well (1 each): in-process, 167.

### Remains
1. 167 (in-process client surfaces vs z-order) for multi-document work; 145 / tooltips under sinks with it.
2. Non-rectangular visible regions (bounding box only), children of the client window do not clip, window regions
   (SetWindowRgn has no driver hook here) are only seen at the next evaluation.
3. A third process in the parent chain (WebView2's browser process) that changes its windows: seen when the source presents or
   the owner changes something (dcomp presents at least once a second; not measured separately).
4. First contact needs the owner's message loop; Vulkan (M3, blocked by 166); other compositors / Mesa not run; DPI scaling
   other than 100 % not run (the mapping is there, by ratio).

### Weak spots after M2 (for the review)
- The source names the client window (read once at first contact). The sink shows the frames where the server says that window
  is, in whatever toplevel of the owner contains it: a source can still draw over any window of the owner, as before.
- `get_visible_region` is a server request made from the driver (winex11 / winemac do the same); its semantics (which flags,
  STATUS_BUFFER_OVERFLOW with a short reply buffer) are relied on.
- The event thread drops `win_data_mutex` in the middle of the sink list walk for the queries; it relies on "only this thread
  removes sinks" and re-reads the next entry afterwards.
- A re-homed sink is shown at the window's intermediate position for a moment (seen: one evaluation at (4,28) during a dock).
- Evaluation rate is bounded only by the owner's own window changes and the source's notifications (one per wake).
- The rest of the M1 list still holds (old-generation buffers destroyed while shown, second EGL context, poll loop for all
  processes, lock held across the dup / pool creation).

## After the adversarial review (2026-10-04; fix/132 rewritten as 4 commits, the old tip is kept as branch `fix/132-m2`)
Commits: 031aa6ee145 (event thread: poll loop), 27a491fea96 (sink, with the server-side geometry and the limits folded in),
89fa1151047 (source), 3b3fb1a465e (GL readback). Everything above this section describes how it got here; where it differs,
this section is what the branch does.

### Scope change: topology B only
A source is accepted only for a window OF ITS OWN PROCESS that is rooted in the top-level the first-contact message was posted to.
Topology A (process B presents to a window owned by process A: `wl_xswap`, `xp.exe foreign`) is no longer carried: the source
side does not create a source for a foreign window (one WARN per client surface: "Window %p belongs to another process, its
client surface will not be shown"), nothing is posted, the owner creates no sink, the window stays blank as on integ.
Chromium / WebView2 (own child of a foreign top-level) is unaffected.
Windows 11 for the record (`tests/r132/xwin.exe` in the VM, D3D11 hardware device with WARP as fallback, B8G8R8A8, process B on a child
panel of process A; screen pixel in the middle of the panel):

| what process B does on A's window | result | on screen while B runs | after B exited |
|---|---|---|---|
| GetDC + FillRect | GetDC non-NULL, FillRect 1 | B's colour | B's colour (until A repaints) |
| swapchain, DXGI_SWAP_EFFECT_DISCARD (blt model) | CreateSwapChain S_OK, Present S_OK | B's colour | B's colour |
| swapchain, FLIP_SEQUENTIAL | S_OK, S_OK | B's colour | B's colour |
| swapchain, FLIP_DISCARD | S_OK, S_OK | B's colour | B's colour |
| own WS_CHILD of A's panel + DISCARD / FLIP_SEQUENTIAL swapchain on it | S_OK, S_OK | B's colour | panel's own colour (the child is gone) |

So Windows allows all of it, including drawing on another process' window (X11 too). Wayland stays stricter on purpose: there
the owner's process would have to show the foreign frames itself, and any process of the prefix could then cover any window.

### Per finding
1. Authorization (HIGH). The first-contact message now carries the client window (`wparam`) instead of a pid; the sink
   (`wayland_remote_sink_create`, UI thread, no driver lock) takes the source pid from `NtUserGetWindowThread(client)`, refuses
   own-process and invalid windows and windows whose GA_ROOT is not the posted top-level, and only then opens that process. The
   control block must carry a magic number and name the same window before anything is written to it (a third party could
   otherwise make the sink write `attached` into an unrelated section of the window's owner). At every geometry evaluation the
   window must still belong to the source pid (`NtUserGetWindowThread`, outside `win_data_mutex`; a window handle can be reused),
   and its top window must be the sink's or another top-level of this process (re-homing), else the sink is hidden.
   Pid spoofing has no parameter left to spoof; slot exhaustion costs the attacker one own child window per slot.
2. Limits. No global limit any more (the event thread's poll array grows); 16 sinks and 1 GiB of buffers per SOURCE PROCESS.
   A process can only use up its own share, whatever top-level it targets; N attacker processes cost the owner N * 16 small
   sinks. (A per-top-level cap would let the attackers' sinks starve the honest ones of that top-level, so I chose per process.)
3. Poll loop. `wl_display_flush` result is used: on EAGAIN the display fd is polled for POLLOUT as well, a POLLOUT-only wake
   does not read, EPIPE falls through to the read (which reports the error), other errors end the loop. Note: with libwayland
   1.24 a producing thread whose request does not fit blocks in libwayland itself (ppoll POLLOUT); the event loop's part is
   the remainder that a non-blocking flush left behind.
4. Wake socket. Drained with 4 KB reads until short (at most 256 KB per round), one update per round.
5. Size. 8192 px per side (both sides enforce it; a larger window is not shown and WARNs), within the 1 GiB per process.
6. Wording. The NtQuerySection / fstat check only makes sure the pool is valid when it is created. The source keeps a writable
   handle and could shrink the file afterwards; the compositor survives that through libwayland's SIGBUS handling for wl_shm
   pools, and answers with an error that ends the OWNER's connection (not tried). Possible hardening, not done: wineserver
   creates section files with `memfd_create("wine-mapping", MFD_EXEC)`, i.e. without MFD_ALLOW_SEALING; with that flag the sink
   (or the server, for sections the driver asks for) could add F_SEAL_SHRINK before making the pool.
Found while testing the backpressure case, also fixed: with the compositor not releasing buffers the sink took every frame and
ended up holding all three, the source dropped its remaining frames ("No free buffer") and the LAST frame never showed. The sink
now leaves the mailbox alone while two buffers are unreleased and takes the latest frame when one is released.

### Verification of the final branch (mutter 50.1 + NVIDIA EGL, renderer=gl; `inst/132/m1/`)
- Gate change: `wl_xswap.exe`: blank, 0 magenta pixels, one WARN (by design). Topology B: `run2.sh` (two processes, own children
  in p1 and p2), `layered_child_gpu` green 300x200, `xproc_hidden_present` green before / none after the hide.
- `evil.exe` (WAYLAND_DEBUG=1 on the owner, 0 protocol errors, owner alive after every case, fds 24 before and after):
  `foreign` = valid control blocks for the victim's panel, the victim's top-level, a third process' window and an own window
  outside the top-level: all four refused (attached=0), 0 red pixels on screen; `magic` (wrong magic, block naming another
  window): refused; `msg`: bogus messages refused, 16 of 200 sources accepted; `quota`: 16 of 17 sources accepted, their
  8192x8192 buffers refused beyond 1 GiB; `nosock`, `huge` (2^31 and 8193 px), `small`, `nosect`, `odd`, `index`: as before;
  `ok`: the red frame is exactly on the source's own child (970..1268 x 440..638), gone when the process dies.
- Wake flood (`evil.exe TOP wake 200`: 200 MB of wake bytes, nothing else): owner CPU 3.55 s before the drain fix (M2 build,
  the whole 3.5 s of the flood at 100 %), 0.09 s after. Generation flood (20000 in 2.6 s): 0.94 s of owner CPU, as before.
- Backpressure (`tests/r132/backpressure.sh`, compositor SIGSTOPped 14 s while a source presents 12000 frames and, with
  TITLES=3000, the owner's UI thread queues 9 MB of title changes): strace of the owner shows 13525 sendmsg EAGAIN and 6641
  event-loop polls with POLLIN|POLLOUT on the display fd, no spinning (0.6 s CPU in 14 s incl. the title loop); after SIGCONT
  the titles finish, the owner lives and the LAST frame is on screen (bar at x 296; before the buffer fix it stopped ~20-30
  frames early). `burst.sh` (UI thread only): window turns red after SIGCONT on the old and the new loop.
- `geo.sh` (11 steps), `clip.sh` (13 steps), both with WAYLAND_DEBUG=1: as before, 0 protocol errors. Leaks over 200 cycles:
  owner fds 24 -> 24, maps 388 -> 388, handles 32 -> 32; renderer handles 36 flat. `leak.sh foreign` (topology A): no sink, no growth.
- In-process fps (`d3d11_present.exe`, 8 interleaved runs, one prefix per build): integ 5079-6056 (median 5773), fix/132
  5135-5973 (median 5641). Batches run one build after the other differ by +-40 % in either direction on this shared host.
- Inventor on inv2 / Wayland, final build (`inst/132/inv/r-*`): Home page, Assistant pane and trial popup with content at 45 s
  (4 sinks, none refused, no topology A warning: all of Inventor's WebView2 surfaces are topology B); invscen hello 2/2,
  part 11/11, drawing 8/8, view 3/3; part open -> 3D view visible, Home tab -> Home page, document tab -> 3D view.

### Remains / stays as listed
167 (in-process client surfaces vs z-order; tooltips under sinks), non-rectangular regions, third process in the parent chain,
first contact needs the owner's message loop, Vulkan (M3, 166). Mutter-only / not exercised: wl_buffers of the previous
generation destroyed while possibly attached, FLIPPED_180 on a subsurface, scale != 100 %, Mesa EGL, a source truncating its
section. The event loop covers a flush that returned EAGAIN in the event thread; data left behind by ANOTHER thread's failed
flush while the event thread already sleeps in poll is not noticed until the next wake (same as wl_display_dispatch_queue).
