# 181 GPU-presented child windows go stale on screen when they are resized, moved or presented partially (offscreen client surfaces)
Status: fixed on fix/181 (wt/181, integ b5d75449ffe + 7 commits), awaiting review · Owner: worker-181 · Found in: 174 (probes `tests/r174/`) · X11, wined3d Vulkan and GL
All three defects are upstream's (same on master 4e819f054dd, also on Xvfb + lavapipe); our 061 only changed what an
Expose over a partial present shows (old frame instead of black). Inventor not run (seat in use).

## Symptom
A child window that a GPU API presents into is an offscreen client surface on winex11 (window-surfaces.md): the driver
presents into an X window under the dummy parent and copies it onto the toplevel X window. After the toplevel is resized
(by the WM or not) such children showed old or foreign pixels until the application presented the whole window again.
Applications that present once per change (WPF, anything that renders on WM_SIZE / WM_PAINT only) stayed wrong;
hovering repaired the rectangles WPF redraws.
![grown view](attachments/181-grown-view-strip.png) ![moved pane](attachments/181-moved-pane-not-shown.png)
Left: D3D11 view (yellow) after a growth, 50 ms layout. Right: WPF panes, the status bar (blue) only moved.

## Probes
- `tests/r181/gpuchild.exe MODE [N] [options]` (C, runs on the VM; every check reads the screen from another process,
  the thread under test idles without polling): `grow` (one ResizeBuffers + Present per resize), `move` (moved, no
  present), `movesib` (a sibling grows over it and presents, then it moves), `partial` (D3D9 COPY-effect presents of a
  40x40 rectangle, then a cover window beside / over it). `tests/r181/run.sh TAG vulkan|gl MODE..`.
- 174's `tests/r174/drive.sh` with `frame.exe` (D3D11 view) and `wpf.exe hosted partial` (hardware WPF panes), 30 WM
  resizes per run: `tests/r181/accept.sh TAG RENDERER LABEL`, `accept-wpf.sh` (needs the .NET prefix copy
  inst/181/pfx-net48), `all.sh LABEL` (everything on :98 as it is; ~1 h). Also there: `xmove.sh` (cross-process moved
  child), `quick.sh` / `rep.sh` (061 / 078 / cross-process probes, repeated), `fps.sh`, `sw.sh` (Xvfb :1470 + lavapipe).
  Results of the runs below: inst/181/r/.

## Windows ground truth (Win11 VM, 96 DPI)
| probe | Win11 |
|---|---|
| grow, grow flip, grow d3d9 | 0 bad of 6 each: one Present after ResizeBuffers / Reset shows the new size |
| move, move flip, move d3d9 | 0 of 4: the frame is shown at the new place, nothing presented |
| partial, partial top | 0 of 7: the square shows each partial present, also after a cover beside and over it |
| movesib (either z-order) | the moved child B gets WM_PAINT for its whole client area and its bits are *not* shown at the new place (draft [186](186-overlapped-sibling-not-invalidated.md)) |
| grow top (top-level, blt model) | 1 of 6 bad on Windows itself (the present in WM_SIZE misses the new strip): no reference |

## Defects, causes, fixes (branch fix/181)
1. **First present after a growth leaves the new strip undefined** (upstream; base 5 of 5 growths with gpuchild, 10 of
   15 with frame.exe; master the same).
   - Vulkan: win32u answers vkAcquireNextImageKHR with VK_SUBOPTIMAL_KHR once the window no longer matches the
     swapchain, wined3d (`wined3d_swapchain_vk_blit`, written before win32u did that) presented to the old-size
     swapchain anyway and recreated it afterwards, for the next frame. The host driver shows the old-size image
     unscaled; the rest of the (already resized) offscreen X window is what the server copied from the screen.
     Fix 2826e6ca776 `wined3d: Recreate the Vulkan swapchain before presenting when it doesn't match the window.`:
     an acquire that says SUBOPTIMAL is treated as out of date (recreate, acquire again, one present, no frame of the
     wrong size at all); a second SUBOPTIMAL in a row is presented as before.
   - GL (NVIDIA GLX; llvmpipe is fine): the GLX drawable follows the X window, and the resize request of the client
     window (`client_surface_update_geometry`, gdi_display) was still in Xlib's buffer when the frame was rendered
     (17 of 30 with frame.exe under awesome + picom, 3 of 3 with gpuchild).
     Fix f2012263e35 `winex11: Wait for the server to resize a client window.` (XSync after a size change; one round
     trip per resize of a client surface, none per present).
2. **A moved child is not shown at its new place** (upstream; base and master 4 of 4).
   `move_window_bits()` copies inside the window surface, which has no pixels of client surfaces (they are clipped
   out of it; the driver draws them on the toplevel X window).
   - ae755f9a36d `win32u: Move the bits of client surfaces with their window.`: a second copy on the host window,
     through a DC without window surface whose visible region is what the surface doesn't paint (internal
     `DCX_CLIENTSURFACES`; skipped for DPI-scaled surfaces and drivers without direct drawing). This is what moves a
     GPU child of *another* process when its container moves (`tests/r181/xmove.sh`: xp.exe child in a host panel that
     the host moves: new strip host grey on base, the child's frame on fix).
   - 15445777e10 `win32u: Present offscreen client surfaces again when they are moved.`: screen bits are only right
     if nothing drew over them, and WPF renders a pane inside its WM_SIZE: the pane resized first had already
     presented over the old place of the pane moved next (hardware WPF probe with the first commit alone: 10 of 15
     growths showed the browser pane's bottom strip in the status bar). So a surface of this process that moved
     without resizing is presented again from its offscreen window and invalidated. Windows sends WM_PAINT in that
     situation too (movesib) and WPF repaints on it.
3. **Partial presents stay invisible** (upstream; base 5 of 5). wined3d presents a partial destination rectangle of a
   COPY-effect swapchain (WPF's dirty rectangles) with GDI on the window DC it got when the swapchain was created.
   Three separate things:
   - The DC kept drawing into the window surface, under the client surface, where it is never shown: nothing
     invalidated it when the window got its pixel format flag afterwards (until the next resize / z-order change).
     gpuchild `partial` without `nudge`: never visible. Fix 2710d343633 `win32u: Invalidate the DCs of a window when
     its state is updated.`
   - Once the DC draws on the toplevel X window directly: the request stays in Xlib's buffer. gdi_display is flushed by
     `X11DRV_ProcessEvents`, which upstream only calls when the thread has X events since d3cb94b543e (Oct 2025), by a
     window surface flush or a full present. `partial nudge`: every square showed one present late, the last never
     (174: black 7 of 10 until the pointer moves). Fix ae81d7d783d `winex11: Flush the display after drawing on a
     window.` (XFlush in `add_device_bounds()`, i.e. after each GDI call that reaches the X11 driver; no round trip;
     the offscreen present path already flushed after its blit).
   - The offscreen X window never gets partial presents, so presenting it again on Expose (our 061) brought back the
     last *full* frame over the whole window: confirmed (cover beside the square: square lost on base; master shows
     black instead). Fix 2f2417ddf32 `win32u: Only present the exposed part of offscreen client surfaces again.`
     **Left open**: a cover *over* the square (no compositing manager) still shows the old frame there until the
     application repaints (fix/005's RedrawWindow asks it to; WPF does). Closing it needs partial presents to reach
     the offscreen window: wined3d keeping the presented front buffer, or DCs of such windows drawing into it.

## Verification
- gpuchild on :98 (NVIDIA, 144 DPI, awesome), fix build, N = 30 per mode, Vulkan and GL, with picom and without:
  grow, move, move d3d9, movesib, partial nudge, partial: **0 bad** in every run (24 runs of 30-32 checks).
  build/ without picom, N = 10: grow 5 (every growth), move 10, movesib 10, partial nudge 10 of 12 (GL 9), partial 12
  of 12, both renderers.
- 174's probes, 30 WM resizes each (awesome Mod4 drags / xdotool windowsize), with picom: frame.exe view base
  Vulkan 10 bad, GL 17 bad -> fix **0 / 0**; hardware WPF moved pane (base 2 of 3 in 174) -> **0 of 30**; WPF dirty
  rectangle (base 7 of 10) -> **0 of 30**; both renderers.
- The same without picom: frame.exe view build/ 9 of 20 (Vulkan and GL: 9 of 10 growths) -> fix **0 of 30 / 0 of
  30**; WPF moved pane and dirty rectangle **0 of 30** each, both renderers.
- The cover steps of `partial` only mean something where the cover really covers and makes an Expose (openbox without
  compositor; awesome moves the cover window away, picom keeps the pixels): build/ 8 of 8 bad, fix 1 of 8 = the cover
  over the square (left open, above), both renderers.
- Xvfb + lavapipe / llvmpipe (openbox): master Vulkan grow 3 of 6, move 6 of 6, movesib 6 of 6, partial 6 of 6; GL
  grow 0, the rest the same. Fix: 0 bad of 10 in grow, move, movesib, and the cover over the square in partial (1 of 8),
  both renderers.
- 061 / 078 / cross-process probes at 96 DPI, Vulkan and GL, awesome without picom: expose_present (plain, nopump,
  show), present_lag 60 1 and 30 0, xproc_hidden_present, xproc_swapchain_expose, layered_child_gpu: all pass, as on
  build/. `expose_present.exe show` with the GL renderer is flaky on both builds (30 runs: build/ 23 ok, fix 22 ok;
  Vulkan 30 of 30 on both): draft [187](187-gl-first-present-after-show-lost.md).
  (These probes are not DPI aware: in a 144 DPI prefix they fail on build/ too, colours read back as d4.)
- `tools/regress.sh run wt/181-build -m '^(d3d11|dxgi|d3d9|d3d8|ddraw|opengl32|user32|d3d10core|win32u|vulkan-1|gdi32|wined3d|d3d10_1|d2d1|dcomp)$'`:
  127 units, 0 worse than the integ baseline `deps/regress/b5d75449ffe…-h26` (x86_64 d3d11:d3d11 crashes in both;
  i386 user32:win 4 failures -> 0, flaky).
- Cost: `tests/d3d11_present.exe` (top-level, Vulkan, :98) 9 interleaved runs each: build/ 2416-2883 fps (mean 2683),
  fix 2543-2901 (mean 2710); a child swapchain in another process' window (`xp.exe child`, 3000 presents): 0.37-0.38
  ms per frame on both. Inventor's rubber band (uilat) not measured: seat in use.
- No conformance test added: Wine's D3D tests never read the screen, and what is fixed here is what reaches it. The
  check is `tests/r181/gpuchild.exe` (exit code; runs on Xvfb + lavapipe too).

## Where this meets other branches
dlls/win32u/dce.c `move_window_bits()` / `update_visible_region()` / `NtUserGetDCEx` flags and window.c
`update_client_surfaces()`, `present_offscreen_client_surfaces()` (ours, 061), `update_window_state()`: fix/171 works on
`register_window_surface` in the same files (other functions). dlls/winex11.drv/init.c `add_device_bounds()`,
`client_surface_update_geometry()`: not touched by fix/173 / fix/175 (event.c, window.c). dlls/wined3d/swapchain.c
`wined3d_swapchain_vk_blit()` / `swapchain_vk_present()`: next to 165's hunks, no overlap.

## Weak spots (for the review)
- `DCX_CLIENTSURFACES` is an internal flag on a public syscall (like NTGDI_RGN_MONITOR_DPI); the bits it moves can be
  another window's if that one drew there first (the second commit covers this process' surfaces only; for a foreign
  GPU child it is still better than the stale parent pixels, but not always right).
- A moved GPU child now gets a WM_PAINT that Windows doesn't send for a plain move (one repaint more).
- `update_window_state()` now invalidates DCs every time, also for the cross-process surface owner updates (027).
- XFlush per X11-driver GDI call: only DCs without a window surface get there (GPU windows, desktop, foreign
  top-levels), not measured on an application that draws much GDI over a GPU window.
- XSync under win32u's client `surfaces_lock`; an acquired swapchain image is dropped when the swapchain is recreated
  (valid Vulkan, tested on NVIDIA and lavapipe only).
- A surface that moves (or is exposed, 061) before its first present shows whatever its offscreen X window holds.
- Not done: DPI-scaled (Wine-virtualized) windows, top-level (onscreen) client surfaces after an Expose, partial
  presents under an Expose (above), and Inventor itself.
- The last change (RDW_INVALIDATE instead of RDW_INVALIDATE | RDW_NOERASE, the same thing for the server) was made
  after the regress run and the picom pass; the pass without picom, the openbox and the Xvfb runs are on the final build.

## Relevance
Inventor's viewport (D3D11 child): defect 1 after every growth until its next frame, defect 2 when a pane moves it.
Its WPF panes are most likely software-rendered (174), so defect 3 concerns other WPF / D3D9 applications. WebView2
children are other processes' client surfaces: defect 2's first commit. The stale ribbon / browser of 174's report is
not explained by any of this; 186 is the lead found here.
