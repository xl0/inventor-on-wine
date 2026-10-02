# 117 Chromium/WebView2 process startup costs on Wine (unaligned msedge.dll copied per process, GPU init, font lookup)
Status: draft · Owner: - · Found in: 085b (inv3, integ 7e8ed9554cb + fix/085b)

## Symptom
After 085b the trial popup shows content ~0.2-0.7 s later than on Windows; every WebView2/Edge process start
still pays fixed Wine-side costs, and Inventor starts ~20 WebView2 processes (agent + 2-3 Inventor instances).

## Evidence (inst/085b, tests/bigmap_perf.c)
- msedge.dll (154.0.4258.37): 335 MB image, FileAlignment 0x200, so no section can be mmapped from the file:
  map_image_into_view() preads all of it into anonymous memory in every process. bigmap_perf: map 250-280 ms
  (Win11: 1.2 ms first, 0.4 ms again; pages shared). ~340 MB of each WebView2 process's RssAnon is this copy
  (`grep RssAnon /proc/PID/status`: browser/utility ~390-450 MB, renderers ~800 MB).
  Prototype `inst/085b/image-share.patch` (server copies sections that aren't page-aligned in the file into
  the image's shared_map memfd, ntdll maps them MAP_PRIVATE): map 26 ms while another process holds the
  DLL, but the first mapping costs a ~200 ms copy inside wineserver (stalls all clients), it applies to
  every 0x200-aligned native DLL, and the popup A/B showed no gain (8.8 s both, 4+4). Memory win only.
- GPU process init 0.5 s: CollectDriverInfoD3D 0.28 s + eglInitialize (ANGLE d3d11 on wined3d/Vulkan) 0.23 s.
- DWriteFontProxy::MatchUniqueFont 150 ms in the browser (renderer's first style recalc blocks on it).
- First DComp present ~115 ms (DXGISwapChainImageBacking::Present 52 ms: EnqueueSetEvent waits for the GPU,
  DCLayerTree commit 63 ms).

## Repro
Chromium startup trace of the agent's WebView2: `inst/085b/edge-args-hack.patch` + `EA='--trace-startup=...
--trace-startup-format=json ...' inst/085b/run.sh TAG`, then `inst/085b/ctrace.py TAG '' 40`.
