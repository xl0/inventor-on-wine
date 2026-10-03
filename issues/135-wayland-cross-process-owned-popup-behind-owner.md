# 135 winewayland: popup owned by a window of another process maps behind it (Inventor trial welcome popup is invisible and blocks the main window)
Status: draft · Found in: wayland test pass (notes/wine/wayland.md)

## Symptom
At every Inventor start AdskLicensingAgent opens the "webview" trial-welcome popup (WS_POPUP, owner = Inventor's main
window, other process). On Wayland it is mapped but stacked below the Inventor main window, so the user sees no popup while
Inventor's input is blocked (it is the modal-ish popup: clicks on the main window do nothing until the popup is closed).
Verified: File > Open click twice -> no dialog; `tests/wl_winctl.exe HWND close` on the popup -> dialog opens on the next click.
`tests/wl_winctl.exe HWND hide` on the main window (ShowWindow SW_HIDE) reveals the popup underneath
(inst/wayland/evidence/135-trial-popup-behind-owner.png; its content is blank, see 132). tools/invscen is not affected because
the harness closes the popup by WM_CLOSE (so this only bites manual use).
Related: 085 (same popup staying white on X; fixed there).
Wine logs `err:win:set_window_owner cannot set owner (nil) on other process window` for it (probably also on X).

## Repro (standalone)
`tests/wl_xowner.c`: `wine tests/wl_xowner.exe` (process A, prints owner=HWND), then `wine tests/wl_xowner.exe HWND` (process B:
WS_POPUP window owned by A's window). Both map, B above A at first; click into A's client area: B drops behind A
(inst/wayland/evidence/135-probe-xowner-after-click.png). Under X B stays above (WM_TRANSIENT_FOR to A's X window).

## Open
Component (guess): winewayland.drv. xdg_toplevel.set_parent needs the parent's xdg_toplevel object, which only exists in
the owner's connection, so cross-process owners need xdg-foreign (zxdg_exporter_v1/zxdg_importer_v1: both advertised by
mutter 42, see the registry dump in a +waylanddrv log) or another route. Same-process case: 134.
