# 174: Main-window regions stay black or stale after Awesome Mod+mouse resize

Status: open, user-reported on the laptop; root cause not established.

## Environment

- Inventor Professional 2027.1, existing `prefixes/inv`, 144 DPI.
- Wine `b5d75449ff`, `wine-11.18-537-gb5d75449ff` (2026-10-04 build).
  Includes the fixes for 124 and 125.
- Ubuntu 24.04, Awesome on X11, NVIDIA-primary single X screen (RTX 3080).
- `WINE_D3D_CONFIG=renderer=vulkan`.
- Picom: `--config /dev/null --backend glx --vsync --no-use-damage`.
  These settings were checked during the report; the earlier viewport tearing
  had been resolved with this compositor configuration.

## Observed behavior and recovery

1. Resize Inventor's main window using **Awesome Mod+mouse**, not its border.
   Exact size, drag direction and timing were not recorded.
2. Most visual glitches are transient, but some black/stale regions remain
   after the resize. The screenshot shows affected ribbon/panel areas and
   black bands; the model viewport is still visible.
3. Move the mouse over a black area: **icons redraw as the pointer passes**.
4. Resize again: after another round of transient glitches, **the entire
   window redraws correctly**.

Expected: the window repaints fully when resizing ends, without requiring
hover or another resize to reveal its contents.

The user's exact recovery description:

> Mod+mouse. When I drag the mouse over the black area, the icons are redrawn,
> and on the next resize, adter another round of quick glitches, I get the
> whole window redrawn correctly.

![Stale and black regions after resizing Inventor](attachments/174-mod-resize-stale-regions.png)

Screenshot published with the user's permission, cropped to Inventor and
stripped of metadata. The original is retained privately.

## Distinction from earlier reports

- [124](124-open-dialog-resize-coreclr-crash.md): the old Open-dialog failure
  involved a fatal exception, with GDB holding the process during capture.
  **This report is not that paused-process case.** At inspection Inventor was
  sleeping normally, not ptrace-stopped; GDB was armed with no capture marker.
  Hover and the next resize also demonstrate continued UI processing.
- [077](077-wm-move-no-sizemove.md): WM-driven size/move handling is relevant
  context, but this is not yet established as a regression of that fix.
- [062](062-wpf-layered-splitter-black.md): another black-region report;
  no shared cause has been demonstrated.

## Investigation / handoff

The recovery pattern suggests missed repaint/invalidation after WM-driven
resizing. This is a hypothesis, not proof of which layer is responsible.
Compare final Win32/X11 geometry, update/clip regions and repaint delivery
before hover versus after recovery; include border resizing as a control.
No such trace or border-resize comparison has been collected locally.
Maximize/restore recovery and behavior without picom have not been tested.

Private evidence is in `inst/local/debug-20261004-203715-b5d75449ff/`,
including `persistent-resize-artifacts.png`, the launch log and GDB log.
No raw logs, core data or model files are attached.
