# 091 Each Chromium/WebView2 frame costs several times more on Wine (Assistant spinner: ~40 % CPU)
Status: open (draft) · Owner: - · Branch: - · Found in: 088 (inv3, integ 9c1eea5beac + fix/088)

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
