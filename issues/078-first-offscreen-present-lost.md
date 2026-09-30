# 078 Presents to offscreen client surfaces can miss the screen (copy races the driver)
Status: fixed (awaiting review) · Owner: worker-078 · Branch: fix/078 (wt/078 on integ 1a3728c8a6e) · Found in: 061 test (tests/expose_present.c)

## Symptom
Originally (061 worker, :101 = ac:00.0, the GPU that also drives the host console, 3/3 runs):
a D3D11 child window (offscreen client surface) presents once, 1 s later the screen is still
black; later presents show. Win11 shows the first frame.

## What it really is
The first-present case doesn't reproduce here on integ 1a3728c8a6e (0 of ~60 runs): NVIDIA :100
(openbox) and Xvfb+lavapipe (openbox, awesome, no WM), wined3d vk and gl, first present right after
swapchain creation (`nopump`), toplevel shown right before it (`show`), cold wineserver, 1100x900
child, Inventor running on the same display, sync interval 0/1; also not with fix/061's re-present
disabled. (The draft's "Xvfb+lavapipe" isn't backed by the 061 transcript: its lavapipe runs used
the two-present test.) The general form does reproduce:

tests/present_lag.c (N presents of red/green/blue, then 500 ms, is the last one on screen):
NVIDIA :100, wined3d-vk: the **last frame never showed in 11 of 12 runs** (screen stuck on the
previous frame, once two frames back); wined3d-gl (EGL path) 4 of 10. lavapipe 0 of 10, VM 0.
Offscreen surfaces are StretchBlt'ed from the redirected client X window onto the toplevel right
after vkQueuePresentKHR / eglSwapBuffers return (X11DRV_client_surface_present), but NVIDIA
hasn't written the image into the window yet (GPU work still queued). Nothing copies again until
the next present (or an Expose, fix/061), so the last frame of a burst - or the only one - stays
hidden. The first-present symptom on :101 is this race with nothing before it (black).
Proof: `vkQueueWaitIdle` between present and copy -> 12/12 correct. The GLX path already waits
(glXWaitForSbcOML; flushes glFinish); the Vulkan path and EGL swaps didn't.

## Fix (fix/078)
- `win32u: Wait for Vulkan presents to offscreen surfaces before copying them.` (30994497668):
  win32u_vkQueuePresentKHR waits for the queue when any presented surface is offscreen.
- `winex11.drv: Wait for EGL swaps to offscreen surfaces before copying them.` (63eb786792c):
  glFinish after eglSwapBuffers for offscreen surfaces, like the flush paths.

## Verification
- present_lag final frame, NVIDIA :100: vk 1/12 -> 12/12 ok, gl(EGL) 6/10 -> 12/12. lavapipe and VM
  (sync interval 0 and 1) ok before and after. Immediate per-present reads stay noisy on Wine
  (Present returns before wined3d's CS thread presents), only the final check is a verdict.
- tests/expose_present.c (now also checks the first present; modes `nopump`, `show`): VM pass
  (all modes), Wine NVIDIA + lavapipe, vk + gl pass (before and after).
- Inventor viewport cost (inv3 :100, uilat base -> fix): rubber 99.7 -> 100 fps, lag p50 2.3 -> 2.7
  p95 4.8 -> 4.7 ms; orbit 48.9 -> 48.8 fps; pan 59.6 -> 59.6 fps; step latencies within noise.
- regress d3d11 d3d10core d3d9 d3d8 ddraw dxgi opengl32 user32 win32u vulkan-1 winevulkan vs
  integ 1a3728c8a6e: 0 worse of 91 units.
- DXVK 3.1.1 is out of scope: nothing it presents shows in these probes at all (096).

## WebView2 (085 popup) on the server: not this bug
Time from the white popup appearing to non-white (0.7 s screenshots, inst/078/*):
before (build/, same commit) 8.4-12.3, 9.3-13.2, 8.4-12.3 s; after 8.3-12.3, 8.3-12.3 s. Unchanged.
tools/pixgrab.py on the GPU process's offscreen window (what WebView2 last presented) during the
white phase: white too; it switches to the spinner when the screen does. WebView2 renders white
frames itself (~1 present/s) until the page draws; no present is lost.
`WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS=--disable-gpu` (kept on the laptop) doesn't reach any
msedgewebview2 command line (the variable is in the processes' environment), so it has no effect.

## Laptop hypotheses (hybrid NVIDIA/Intel PRIME, awesome, real monitor)
- This race is worse there: a PRIME (on-demand) or vblank-paced present lands later than on the
  headless server, so copies pick up the previous frame more often; Chromium presents only on
  damage, so its last frame (content after the white/spinner) can stay hidden until the next
  present (mouse over the popup). The fix covers queue-side work; if the X server applies the
  present later (Present extension at vblank), vkQueueWaitIdle doesn't cover it. Check on the
  laptop: `tests/present_lag.exe 60 1` final line, before/after fix/078.
- Narrow-column layout: not reproducible here. Suspects: HiDPI (laptop DPI != 96: WebView2 runs
  with --embedded-browser-webview-dpi-awareness=2, client surfaces then use DPI scaling /
  monitor vs virtual rects) and 085's hardware DComp path (visual offsets/clips ignored).
  Check the laptop's LogPixels and xrandr DPI; retry with 085's dcomp commits reverted.

## Repro
`WINE_D3D_CONFIG=renderer=vulkan wine tests/present_lag.exe 60 1` on an NVIDIA display (exit 1 =
last frame not on screen). Scripts: inst/078/invrun.sh (Inventor + screenshots every 0.7 s),
inst/078/offwin.sh (pixgrab of WebView2's offscreen window during that run).
