# 134 winewayland: an owned/modal dialog goes behind its owner when the owner is clicked, the app looks frozen
Status: draft · Found in: wayland test pass (notes/wine/wayland.md) · Blocks real Inventor use under Wayland

## Symptom
Inventor: File > Open (or Application Options): the dialog appears; one click on the (disabled) main window and the dialog
disappears behind it. Wine still has the dialog visible and the main window disabled, so every input is swallowed:
Inventor looks hung. No way back with the mouse (no taskbar); SetForegroundWindow from another process is refused.
On Windows (and under X with a WM that honours WM_TRANSIENT_FOR) an owned window always stays above its owner.

## Repro
Standalone: `tests/wl_xowner.c` mode `self`
(`x86_64-w64-mingw32-gcc -o tests/wl_xowner.exe tests/wl_xowner.c -luser32`):
`wine tests/wl_xowner.exe self` creates A and a WS_POPUP|WS_CAPTION window B owned by A, A disabled. Click A's client
area outside B: B vanishes behind A (inst/wayland/evidence/134-probe-modal-popup-above.png / ...-behind.png).
Inventor: `x/wshot.sh move 80 48; x/wshot.sh click` (Open), wait, click on the main window (e.g. 1300,700).
inst/wayland/evidence/134-open-dialog-gone-behind-owner.png. Wine side: `tests/wl_winlist.exe` still lists the 'Open' window
visible with owner = main window.

## Evidence
`grep -n "set_parent" dlls/winewayland.drv/*.c` is empty: managed windows become plain xdg_toplevels (window.c
`is_window_managed` -> ROLE_TOPLEVEL), only unmanaged (SWP_NOACTIVATE) owned windows become subsurfaces. The owner
relation never reaches the compositor.

## Open
Component (guess): winewayland.drv, `wayland_surface_make_toplevel`: call `xdg_toplevel_set_parent()` with the owner's
toplevel for owned managed windows (same process). Cross-process owners: see 135. Possibly also keep input blocked on
the compositor side (xdg_dialog needs a newer protocol; mutter 42 has none).
