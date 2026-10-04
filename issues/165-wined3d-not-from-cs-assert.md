# 165 Inventor: "Assertion failed … wined3d_private.h … cs->thread_id != GetCurrentThreadId()" box (seen once)
Status: fixed (not merged, awaiting review) · Owner: worker-165 · Branch: fix/165 (ea4f722a1f3, wt/165 on integ e00a74f6590) · Found in: new-host verification round (inv3 :100, build-s = integ e00a74f6590, 2026-10-04)

## Symptom
A Wine assertion box in Inventor: `Assertion failed! … dlls/wined3d/wined3d_private.h Line 4792 Expression:
cs->thread_id != GetCurrentThreadId()` (`wined3d_not_from_cs()`: a wined3d function that must not run on
the command-stream thread was called from it). The box is modal: the next harness connect failed
(`sheetmetal` FAIL at connect, 28.8 s) until that Inventor was replaced.
Screenshot: inst/round2/assert-s-1.png (not in the repo), log: inst/round2/sheetmetal-s-fail.txt.
Seen 1 time in 5 `run.sh all` runs, right after `params` closed its last document.

## Cause: upstream wined3d + winevulkan, none of our patches (131 is not involved)
The assertion is the last link of a chain; the bug is [150](150-vulkan-swapchain-destroyed-window-crash.md).
1. Inventor presents a document view and destroys the view window. Present only queues the work: if the command
   stream (CS) thread gets to it after the window is gone, win32u answers VK_ERROR_OUT_OF_DATE_KHR and
   `wined3d_swapchain_vk_recreate()` destroys the Vulkan swapchain, then fails to create the new one
   (`Failed to get surface capabilities, vr VK_ERROR_SURFACE_LOST_KHR` / `Failed to recreate swapchain`).
   The swapchain kept the freed `vk_images` / `vk_semaphores`, the old `image_count` and the destroyed handles.
2. The next present, or the destruction of the swapchain (the CS callback of the view's Release), used them:
   `err:vulkan:vkAcquireNextImageKHR` / `vkDestroySemaphore` / `vkDestroySwapchainKHR` `Exception 0xc0000005 in Unix call.`
3. winevulkan's `UNIX_CALL_CHECKED` then calls `ExitProcess(3)` on the calling thread, the CS thread (upstream
   de8a4357d3d, 2026-06-16; draft [168](168-winevulkan-exitprocess-on-cs-thread.md)).
4. ExitProcess kills all other threads and runs DLL_PROCESS_DETACH on the CS thread. Inventor's
   OGSDeviceDx11.dll (OGSDevices.dll static destructors) releases a D3D11 buffer there:
   `d3d11 buffer Release -> wined3d_buffer_decref -> adapter_vk_destroy_buffer -> resource_cleanup ->
   device_resource_released -> device_resource_remove -> wined3d_not_from_cs()`: the box. **The asserting
   function is `device_resource_remove()`, in Inventor.exe.**
5. The process stays with one thread in the box; its main thread is dead, so the Linux thread group leader is a
   zombie and `/proc/PID/environ` is unreadable (`run.sh: cannot open /proc/PID/environ: Permission denied`):
   run.sh sees no Inventor and starts a second one, the harness reports the box. Dismissing it (WM_CLOSE = Cancel =
   DebugBreak) ends in Inventor's crash handler, which only gets to create `Temp\Inventor<ts>.dmp` (0 bytes).

Evidence in the round's leftovers: `inst/invscen/inv3/params/dialog-1.png` 10:07:13 (the params harness saw the box
while exiting), the "cannot open … environ" line, `Inventor261004100714.dmp` with 0 bytes, no CER `.tmp` files.
The race is rare in Inventor: 8 iterations of the round's sequence on integ ran clean (0 "Failed to recreate
swapchain" lines in the 4 kept logs, ~20 document closes each).

## Reproduction
- Standalone, deterministic: `tests/r165/deadwin.exe [presents]` + `detach.dll` (D3D11 swapchain, DestroyWindow,
  Present, Release; the DLL releases a buffer at process detach) with `WINE_D3D_CONFIG=renderer=vulkan` (lavapipe on
  Xvfb is enough). integ e00a74f6590 and upstream master 4e819f054dd (wt/regress-master-build-h26): the three err
  lines above, `process detach on thread NNNN, not the main thread`, `Assertion failed: cs->thread_id !=
  GetCurrentThreadId(), file …/wined3d_private.h, line 4792`, exit code 3. GL renderer: exit 0.
  Windows (131 review probe, inst/131-review/probe2-win.txt): Present on a swapchain whose window is gone is S_OK.
- In Inventor: debug build with `WINE_165_PRESENT_DELAY=30` (the CS thread sleeps 30 ms before each present, so
  every "present, then destroy the window" loses the race; inst/165/debug-165.patch). `run.sh all` on integ +
  debug: the box in `feat`, with the "cannot open … environ" line and the second Inventor
  (inst/165/delay1/: inventor.log has the whole chain with backtraces).
- What did not reproduce it: 8 iterations of the round's sequence (fresh Inventor, hello, winlist, uilat rubber,
  `all`) on integ + debug prints; starting a second Inventor next to a running one; gdb-injected faults on the CS
  thread (access violation and abort(): Autodesk CER writes a full dump and ends the process, no box) and the
  main thread killed with abort_thread (process lingers, no box).

## Fix (fix/165, upstream-style, no dependency on our stack)
- `954f2d2e4c7 wined3d: Leave no stale Vulkan swapchain behind when recreating it fails.`
  `wined3d_swapchain_vk_destroy_vulkan_swapchain()` resets the members it frees or destroys, a failed image
  creation no longer leaves `vk_swapchain` / the arrays set, and `wined3d_swapchain_vk_blit()` returns
  VK_ERROR_SURFACE_LOST_KHR when the swap-interval change inside it could not recreate the swapchain (else a
  division by `image_count` = 0). A swapchain without a Vulkan swapchain presents through the existing GDI path
  (as swapchains of the desktop window do) and its destruction is safe. Present keeps returning S_OK. This is 150's
  task.
- `ea4f722a1f3 dxgi/tests: Test presenting to a destroyed window.` (d3d10 device, DISCARD: Present, DestroyWindow,
  Present, Release.) With the Vulkan renderer it ends the test process with exit code 3 on integ and passes with
  the fix; with GL (regress.sh) it passes either way. **Not run on Windows**: the VM was down and is not this
  worker's; the expected S_OK is the 131 review's Windows result for the same calls.

## Verification
- deadwin.exe 2 / 0 / 5 presents, fix build: exit 0 with Vulkan (one "Failed to recreate swapchain" or "Failed to
  blit image, vr VK_ERROR_SURFACE_LOST_KHR", then silence) and with GL.
- dxgi test, plain Xvfb + lavapipe, x86_64: Vulkan 62 failures, GL 63 (the display-mode lines 1683, 2814-3723,
  7648, 7726 as before, GL also the flaky 8752), none in the new test.
- Inventor with the forced race (fix + debug patch, 30 ms present delay): `hello..sheetmetal` 8/8 with 6 handled
  recreate failures, `all` 13/13 with 4; no "in Unix call", no assertion (inst/165/delay2-fixed, delay3-fixed-full).
- Clean fix build (wine-11.18-533-gea4f722a1f3) on inv3: `run.sh all` 13/13, Home page renders with the suite's
  files in the recent list (inst/165/final1/home.png); 4 more iterations of the round's sequence (fresh Inventor,
  hello, winlist, uilat rubber, `all`) 13/13 each, no recreate failure, no box.
- `tools/regress.sh unit` on the fix build vs the baseline deps/regress/e00a74f6590…-h26, both arches: d3d9:device,
  d3d9:visual, ddraw:ddraw7, dcomp:dcomp pass (same todo counts); dxgi:dxgi fail/2 on both arches as in the baseline;
  d3d8:device i386 pass (no x86_64 test exe in either); d3d11:d3d11 i386 pass, x86_64 crash as in the baseline
  (3 of 3 on the fix build and 3 of 3 on build/; one run next to a running suite timed out instead). 0 worse.

## Left / weak spots
- 168: any other fault in a Vulkan Unix call on the CS thread still ends in this box (not changed: upstream design).
- 169: a D3D object released at process detach on a normal exit hangs (found with the probe, not in Inventor).
- After a failed recreation the swapchain stays on the GDI path until it is resized or released; no retry. For a
  dead window that is right; a live window whose surface creation failed once would stay slow (was: crash).
- `tools/invscen/run.sh` and `tools/prefix.sh kill-inventor` do not see an Inventor whose main thread is gone
  (they read `/proc/PID/environ`): a second Inventor is started and the half-dead one survives kill-inventor
  (kill it by PID after checking a task's maps). Shared tooling, reported, not changed.
