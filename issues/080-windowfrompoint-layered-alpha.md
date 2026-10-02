# 080 WindowFromPoint ignores per-pixel alpha of UpdateLayeredWindow windows
Status: open (draft) · Owner: - · Branch: - · Found in: 062 (tests/layered_alpha.c)

## Symptom
tests/layered_alpha.exe: ULW_ALPHA popup over a red window, bands of alpha 0/1/16/64/128/255.
Windows: WindowFromPoint returns the window below for alpha 0, the popup for every alpha > 0.
Wine (Xvfb, no WM): WindowFromPoint returns the popup for alpha 0 and the window below for
alpha 1 (the rest right). Real clicks are right on Wine (X shape cuts alpha-0 pixels).
The server's window_from_point knows window regions, not per-pixel alpha.

## Re-evaluation (062 rework)
Not the same logic as mouse input: real X input goes to the X window under the pointer (X shape),
then win32u only searches inside that top-level; SendInput clicks follow the server's cursor window,
which also comes from X events. Only server-side hit tests are wrong: WindowFromPoint,
ChildWindowFromPoint, drop-target lookup, fake mouse moves. Same for colour-key holes
(layered_alpha.exe kinds: Windows clicks the window below, a SendInput click on Wine the keyed window).
A fix needs the alpha != 0 region in the server (new request, a region upload per shape change;
with fix/062's threshold it differs from the X shape). Left open; the probe's WindowFromPoint
column also suffers from its own z-order changes (clicks activate the topmost back window).
