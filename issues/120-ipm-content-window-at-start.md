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
