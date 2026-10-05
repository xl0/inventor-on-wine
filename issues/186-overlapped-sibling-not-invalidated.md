# 186 A child that a resized sibling comes to overlap is not repainted (Windows sends it WM_PAINT)
Status: draft · Found in: 181 (probe `tests/r181/gpuchild.exe movesib`) · integ b5d75449ffe = upstream behaviour (not checked on master) · a lead for 174

## Symptom
Layout pass of a frame with two child panes, neither WS_CLIPSIBLINGS: pane A is resized so that it covers B's place, then
B is moved out of the way (same size). This is the order of every "pane above grows, pane below moves down" layout
(`tests/r174/frame.c`, `wpf.cs hosted`: browser pane, then status bar).
- Win11: B gets one WM_PAINT for its whole client area (0,0-300,40 in the probe), and its old bits are not shown at the
  new place (a B that ignores WM_PAINT shows the parent's background there). So Windows doesn't trust B's bits once a
  sibling covered them; the application repaints.
- Wine: B gets no WM_PAINT; its bits are copied from the old place (`move_window_bits()`). Whatever A drew there
  between the two SetWindowPos calls is copied with them, and nothing repaints B.
Not known: which of the two calls invalidates B on Windows (A growing over it, or B moving while covered), and whether
z-order matters (same result with B above and below A).

## Evidence
`tests/r181/gpuchild.exe movesib 2 [below]` (two D3D11 children; prints B's WM_PAINT count and rectangle per step):
- Win11: `A +60, B moved: B 606060 606060 ...; B got 1 WM_PAINT 0,0-300,40` (the step back, where A shrinks first: 0).
- Wine build/: `B got 0 WM_PAINT` (and B's place showed the parent before 181's fixes).
With 181's fix b540b45393c a moved offscreen client surface is presented again and invalidated, which hides this for
GPU-presented panes. GDI / software-rendered panes still rely on the copied bits.

## Why it may matter (174)
A pane that paints synchronously in its WM_SIZE (WPF's HwndTarget renders in OnResize; MFC panes with UpdateWindow)
draws into the shared window surface without being clipped by its siblings. If it covers a sibling's old place at that
moment, the sibling's later move copies those foreign pixels to its new place, and on Wine nothing asks it to repaint:
"misplaced / stale image fragments, repaired by hovering" (174's report). 174's probes paint their GDI children on
WM_PAINT or from a thread a few ms later, i.e. after the layout pass, so they would not have hit it. Not reproduced
with GDI children here; a probe needs a child that paints inside WM_SIZE.

## Task
Find the Windows rule (probe: GDI children that paint in WM_SIZE; each call separately; with and without
WS_CLIPSIBLINGS / WS_CLIPCHILDREN on the parent; update region of B after each call via GetUpdateRect), compare with
`server/window.c` set_window_pos (validation of the moved window's bits, exposure of siblings), fix there. user32:win /
user32:msg have the related tests.
