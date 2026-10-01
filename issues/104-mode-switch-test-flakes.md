# 104 Display-mode-change tests flaky on the regress Xvfb (d3d9, ddraw, dxgi)
Status: fixed (cf0b757f4d6, needs merge to integ) · Owner: worker-104 · Branch: fix/104-mode-switch (wt/104) · Found in: issue 100 harness fixes

Since regress Xvfb has RandR modes 640x480..1920x1080 (tools/xvfb-modes.c), d3d9:d3d9ex, d3d9:device,
ddraw1/2/4 (sometimes dxgi) are flaky, mostly under host load: "Expected message 0x7e" (no
WM_DISPLAYCHANGE), "Got unexpected screen size", "Failed to get display mode", ChangeDisplaySettings -5.

## Findings
- Baseline (build/ = integ 492d5679270) d3d9:device x64: idle 30/30 pass; 120 CPU spinners: 7/30 pass.
- Cause 1 (Wine, win32u): `release_display_manager_ctx` released the global display_device_init mutex
  *before* `cleanup_devices()`. Every mode change makes the setter and explorer (RandR events) rebuild
  the display registry with force=TRUE; one process's cleanup then deleted the PCI GPU key another
  process had just marked not-present in `prepare_devices` and was re-adding. Trace (+system):
  `find_gpu_from_path Failed to find gpu with path "PCI\VEN_0005..."` -> `lock_display_devices Failed
  to read display config` -> `display_mode_changed Failed to update display cache` (returns before the
  WM_DISPLAYCHANGE broadcast) -> wined3d "Failed to read the current display mode", test 4244/4256.
  Fix: run cleanup_devices before releasing the mutex. Traced run under load: 10/10 pass after.

  Committed cf0b757f4d6 on fix/104-mode-switch.
- Windows ground truth (tests/displaychange_sync.c, Win11 VM): the VM (OVMF + -vga std, Basic Display)
  has a single mode 1024x768, so only same-mode CDS_FULLSCREEN changes work (800x600 = BADMODE, which
  still broadcasts). Calling thread's window gets WM_DISPLAYCHANGE synchronously inside the call
  (InSendMessageEx 0); other threads' and other processes' windows get it as a notify message
  (ISMEX_NOTIFY) and the call does not wait for them (call 343 ms = own 300 ms handler + 40).
  Restore (NULL) with nothing to restore: no broadcast.
- Wine (after fix): new mode visible to GetSystemMetrics/EnumDisplaySettings in every handler and right
  after return (synchronous, as Windows). Difference: Wine delivers to other threads/processes with
  SendMessageTimeout(HWND_BROADCAST, SMTO_ABORTIFHUNG, 2000) (ISMEX_SEND), so the call waits for every
  other window's handler (3 x 300 ms -> ~1 s). Stricter than Windows; not a flake cause; left alone.

## Rates (x86_64, 30 runs each, `regress.sh unit`, 120 spinners; base = build/ 492d5679270, fix = cf0b757f4d6)
| unit | base pass/fail/crash | fix |
|------|----------------------|-----|
| d3d9:device | 10/19/1 | 30 pass |
| d3d9:d3d9ex | 10/20/0 | 30 pass |
| ddraw:ddraw1 | 7/6/17 | 30 pass |
| ddraw:ddraw2 | 3/16/11 | 30 pass |
| ddraw:ddraw4 | 7/16/7 | 29 pass, 1 exit-time crash (complete log, no mode failure) |
| ddraw:ddraw7 | 0/17/13 | 30 "fail" = only the constant 5933 todo-succeeded (fog on lavapipe) |
| dxgi:dxgi | 0/18/12 | 30 fail: constant 1674/1683 + load timing only (see below) |
"crash" in base = ddraw/d3d tests dereferencing objects they failed to create after the display error.
Remaining dxgi failures are not mode-switch: 1674/1683 (deterministic: Xvfb's built-in 1920x1200
mode has no dotclock -> 0 Hz in the mode list; can't be removed from a client), 7940/7944 frame-latency
semaphore waits of 100 ms and 8752 budget event within 1 s (load timing, also in base).

- Idle after fix (30 runs each): d3d9 device/d3d9ex, ddraw1/2/4 30/30 pass; ddraw7 only the constant
  todo; dxgi constant 2 + 8752 budget event 4/30 + 1 d3d12 lavapipe crash (issue 100 finding 2 / 102).
- Not the cause: Xvfb/xvfb-modes (modes apply fine), RandR intermediate states (explorer can query X
  between the CRTC disable and enable of a mode set, but rebuilds are serialized on the init mutex and
  the setter's own forced rebuild after the X calls wins), test timing.
- Harness bug found on the way: regress.sh waited for the X socket only; xvfb-modes sometimes failed
  XOpenDisplay (Xvfb not accepting yet, 1/40 starts) -> "modes missing on :N", whole unit run aborted.
  Now waits for xdpyinfo (0/60).
- Regression subset (user32 win32u d3d8/9 ddraw dxgi d3d10core d3d11 winex11 gdi32 explorer, both arches,
  wt/104-regress): identical statuses/failure counts to deps/regress/492d5679270 baseline.

## Repro
Manual: Xvfb :400 + xvfb-modes, prefix wt/104-prefix, scratch runner (WINEDEBUG=+timestamp,+pid,+system,+xrandr).
`tools/regress.sh unit d3d9:device -a x86_64 -n 30 -b wt/104-build` with 120 `while :` spinners.
