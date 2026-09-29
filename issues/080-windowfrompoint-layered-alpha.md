# 080 WindowFromPoint ignores per-pixel alpha of UpdateLayeredWindow windows
Status: open (draft) · Owner: - · Branch: - · Found in: 062 (tests/layered_alpha.c)

## Symptom
tests/layered_alpha.exe: ULW_ALPHA popup over a red window, bands of alpha 0/1/16/64/128/255.
Windows: WindowFromPoint returns the window below for alpha 0, the popup for every alpha > 0.
Wine (Xvfb, no WM): WindowFromPoint returns the popup for alpha 0 and the window below for
alpha 1 (the rest right). Real clicks are right on Wine (X shape cuts alpha-0 pixels).
The server's window_from_point knows window regions, not per-pixel alpha.
