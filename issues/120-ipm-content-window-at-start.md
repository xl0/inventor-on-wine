# 120 A visible "IPM Content" Chrome window (Unnamed Window, 82x24) at the top-left during Inventor start
Status: draft · Owner: - · Found in: 118 environment campaign (inv4 :101, build/ 43790927731)

## Symptom
About 15-25 s after launching Inventor (cold start), a tiny top-level window with an openbox frame ("Unnam...
indow") appears at 0,0 and stays until something closes it. It belongs to the WebView2 helper
(`Chrome_WidgetWin_1`, 82x24, text `IPM Content`, the agent's in-product-messaging host). The invscen
dialog watcher sees it for more than 1 s and fails the step: `hello` FAILs in `connect` with
`unexpected dialog(s): ''`, 5 of 5 cold starts on inv4 (also with 2 such windows at LogPixels 144).
A second run against the already running Inventor passes, so the suite looks green unless Inventor
was freshly started. The watcher dismisses it (WM_CLOSE) and the run goes on.

## Windows
The 82x24 IPM host window is not visible on a real desktop (it is a helper, WS_POPUP, never shown,
or shown off-screen). Not previously reported by the watcher in `inst/invscen/*/results` (first sighting
here), so it may be new since a recent build or specific to inv4's start timing; check with a cold start on
inv/inv2/inv3.

## Evidence
`issues/attachments/120-ipm-content-window.png` (top-left, 2x enlarged). Without the harness: `xwininfo -root -tree`
lists `msedgewebview2.exe` windows 82x24+0+0 (unmapped later) and 82x24+710+575.

## Repro
`tools/prefix.sh kill-inventor inv4`, then `INV=inv4 tools/invscen/run.sh hello` and a screenshot
(`x/shot.sh`) ~18 s after the start. To find the style that makes it frame-decorated and visible:
`WINEDEBUG=+win,+x11drv` on the agent's msedgewebview2, look for the window with title `IPM Content`.

## Windows ground truth (VM, 2026-10-02)
Cold start of Inventor on the VM (scheduled task `invstart`, Autodesk Access services already running, display
1024x768). A probe (`issues/attachments/120-vm-ipmprobe.c`, run through `vm/winrun.sh`) started the task and ran
EnumWindows every 100 ms for 25 s, logging every window of Inventor / Adsk* / msedgewebview2 processes with
class, title, visibility, style, ex-style and rect when first seen or changed
(`issues/attachments/120-vm-startup-windows.txt`). Screenshots every ~0.5 s over the same period.

- **No window titled "IPM Content" ever exists on Windows** (0 hits in the log), and nothing 82x24 and visible.
  No visible tiny window at 0,0 in any screenshot.
- The WebView2 hosts are all invisible (`vis=0`): `Chrome_WidgetWin_0` 768x519 (`style=06cf0000`) and
  `Chrome_WidgetWin_1` "Untitled"/"about:blank" 1020x720 at 4,0 (`06cf0000`/`46000000`, ex `00200100`);
  the agent's visible window is class `webview` (WS_POPUP, `94000000`, ex WS_EX_TOOLWINDOW `80`),
  640x480 at 0,0 for the first 0.1 s, then 860x500 at 82,110 (the trial popup).
- Other visible windows during startup: the Inventor splash `#32770` at t+3.4 s (860x525), a 5x3 / 5x554
  `HwndWrapper` (splitter), `FWxWindowButtons` 40x20.
- So on Windows the IPM host is a hidden (not WS_VISIBLE) Chrome window, or a differently titled one;
  the Wine 82x24 "IPM Content" is not shown by Windows. Note the title may be set only on a later navigation
  (the log has no 82x24 window at all), so check `WINEDEBUG=+win` on Wine for its creation style.
