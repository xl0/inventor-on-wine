# 091 Each Chromium/WebView2 frame costs several times more on Wine (Assistant spinner: ~40 % CPU)
Status: fixed (awaiting review; partial, see Left) · Owner: 091 worker · Branch: fix/091 (wt/091 on integ bd4259df723, build wt/091-build) · Found in: 088 (inv3, integ 9c1eea5beac + fix/088)

## Symptom
Inventor idle at Home with the Autodesk Assistant pane open: the Assistant's WebView2 GPU
process ~21-29 % and renderer ~14-24 % of a core, plus a share of wineserver.
Cause on the page side: the outer page (ase-cdn.autodesk.com/adp/ad-csi-panel-web/r1/index.html)
keeps its MUI indeterminate spinner running next to/under the loaded iframe. Windows pays
for that too, but less: same URL in Edge (spinner visible), CDP Performance.getMetrics:
Blink work is the same (Wine 49 style recalcs/s, main thread 5.7 % busy; VM 64/s, 4.6 %),
yet Wine's renderer process uses ~17 % and GPU ~21 %; VM Get-Process: renderer 1.8-3.6 %,
GPU 0.2-1.3 % (Windows' CPU accounting undercounts; still several times less).

## Where Wine spends it (Assistant GPU process, before 088's dcomp fix)
- per present in wined3d_cs: get_window_parents, get_window_rectangles, get_windows_offset,
  get_visible_region, release_semaphore server calls (~3-5 per frame);
- IPC: Mojo named pipes = server read/write + IOCP remove_completion/get_thread_completion,
  select, event_op: ~40 server requests per frame in the renderer, similar in the GPU process;
- winex11 xrenderdrv_SelectFont -> get_xft_aa_flags -> XGetDefault() on every new DC: with no
  RESOURCE_MANAGER on the X server and no ~/.Xdefaults, Xlib re-reads ~/.Xdefaults and
  ~/.Xdefaults-<host> (2 openat + uname) each call (~200 syscalls/s here; caching the two Xft
  values at init would fix it).
Renderer: 64 % of samples in Chromium code, 29 % kernel (perf by DSO).

## Task
Cut per-frame server round trips (window geometry for presents from shared memory, cheaper
named-pipe I/O); measure with Edge on the Assistant URL (reproduces without Inventor).

## Findings (091 worker, inv3/:100, NVIDIA, wined3d-vk, integ bd4259df723)
Repro without Inventor: Edge on C:\t\091\spin.html (MUI-like spinner: rotating span + SVG dash animation),
scripts inst/091/edge.sh, edgerun.sh (A/B), reqmix.sh PID SECS OUT (server requests/s by name and thread,
strace of pipe writes), incl.py (inclusive % per symbol from `perf script` with dwarf call graphs).
Both the Assistant and spin.html run at ~48 frames/s.

### Per frame before (GPU process 2740 requests/s = 57/frame, renderer ~3000-3400/s = 62-68/frame)
- GPU IO thread (Mojo named pipes + IOCP): ~31/frame (remove_completion 10, select 9, read 4, write 3,
  event_op 2, get_thread_completion 1-2, get_async_result 1). Viz thread 6.5, CrGpuMain 6, GpuVSyncThread 2
  (event_op), dcomp composition thread 2 (GetAncestor of the foreign target + its Sleep).
- wined3d_cs: 10/frame, all from win32u's client surface around each vkQueuePresentKHR of the dcomp target
  (child of the browser's window, i.e. a tree spanning processes): client_surface_update before and after the
  present each = get_window_parents (GetAncestor GA_ROOT) + get_window_rectangles (toplevel) +
  get_windows_offset (map_window_points), then GetDCEx for the offscreen copy = get_window_parents +
  get_visible_region, + 2 release_semaphore (frame latency).
- ReleaseDC of that DC -> reset_dc_state -> xrenderdrv_SelectFont -> XGetDefault: no resource database
  here, so Xlib re-reads ~/.Xdefaults and ~/.Xdefaults-<host> every time (2 openat + uname per frame).
- Renderer: all IPC and sync (Compositor + IO threads), nothing presentation related.
- CPU (perf, GPU process): server round trips ~31 %, wined3d_cs spin ~15 % (108), Chromium code ~25 %,
  NVIDIA ~11-14 %, client surface geometry ~5 %, Xft re-read ~1 %. Renderer: ~48 % Chromium, ~45 %
  server round trips (NtRemoveIoCompletion alone 18 %).
- Measurement: unpinned numbers swing 2x with host load (GPU 12-25 %); pinned to CPUs 52-57 with nice-19
  spinners on the siblings (inst/091/spin.sh) the same work costs about half (GPU ~7 %, renderer ~5.5 %):
  most of the per-frame cost is cross-process wakeups, so the count of round trips is the lever.

### Fixes (fix/091) -- protocol change: 969 -> 970 (desktop_shm_t.windows_serial)
- `winex11.drv: Read the Xft resources once.`
- `server: Count window tree, position, style and region changes in the desktop shared memory.`
  (windows_changed(): set_window_pos, Z-order list changes (update_shared_children: link, reparent, destroy),
  styles/ex-styles, window regions, layered info, window/monitor DPI, monitor list; all desktops of the
  winstation)
- `win32u: Cache window parents, rectangles and offsets of other processes while no window changes.`
  (per-thread last answer of get_window_parents / get_window_rectangles / get_windows_offset)
- `win32u: Keep the visible region of DCs on windows of other processes while no window changes.`
  (DCE remembers the serial of its visible region; "cross-process invalidation is not supported yet")

### After
- GPU process 2740 -> 2231 requests/s in Inventor (geometry requests 0 while idle; Edge ~2/s of
  serial bumps); wined3d_cs 10 -> 2 per frame; no openat/uname per frame.
- tests/xproc_geometry.c (helper process owns a child of our toplevel; we move/shrink/hide the toplevel and
  move a sibling above/below; helper's GetAncestor/MapWindowPoints/GetWindowRect/GetDCEx region must follow
  at once): Win11 pass, Wine base pass, fix pass. ns/call (helper):
  | | GetAncestor(GA_ROOT) | MapWindowPoints | GetWindowRect(top) | GetDCEx+ReleaseDC |
  |---|---|---|---|---|
  | Win11 VM | 578 | 16 | 8 | 6334 |
  | Wine before | 9384 | 9114 | 9083 | 23340 |
  | Wine after | 153 | 219 | 196 | 1718 |
- Edge spin.html, pinned, interleaved A/B (A = base win32u.so + winex11.so, B = fix; 8 pairs, 20-30 s):
  GPU+renderer+wineserver task-clock median 17.7 -> 16.3 % (B lower in 6/8 pairs), wineserver 5.55 -> 4.7 %.
- Inventor + Assistant, pinned WebView2 + wineserver, 3 pairs: Assistant GPU 10.9 -> 11.0 %, renderer 9.6
  -> 9.5 %, all WebView2 22.1 -> 22.2 %: no difference beyond noise (one B run at load 26 discarded: 16.7).
- regress user32 win32u gdi32 dcomp dxgi d3d11 imm32 (fix vs base modules on the same build): 0 worse of 88.

## Left
- 107: Mojo IPC = ~10 round trips per message (named pipe overlapped I/O + IOCP), ~75 % of all requests.
- 108: wined3d_cs spins 2000 pauses (28 us here) after every queue drain, ~15 % of the GPU process.
- Composition swapchains (dxgi hack: hidden popup) still present to an unmapped X window every frame,
  then dcomp copies + presents again (~2 % of the GPU process).
- Pitfall: never `cp` over a .so that running Wine processes map (A/B swaps crashed explorer and Edge);
  copy + rename. WINEDEBUG=+server on any client turns on server tracing for the whole server lifetime
  (1 GB of log in minutes).
