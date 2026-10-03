# 135 winewayland: popup owned by a window of another process maps behind it (Inventor trial welcome popup is invisible and blocks the main window)
Status: fixed · Branch: fix/134 (wt/134, not merged) · Found in: wayland test pass (notes/wine/wayland.md)

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

## Fix (fix/134, on top of 134's commit; design notes in notes/wine/wayland.md "Owned windows")
- `622c30ac444` / `1343468036b`: xdg-foreign-unstable-v2 / -v1 protocol XML (wayland-protocols 1.49).
- `168d30f5b35 winewayland: Set the parent of toplevel windows owned by another process.`: every mapped toplevel is
  exported; the handle is published as window property `__wine_wayland_exported_handle` (a global atom named by the handle,
  kept alive by a second property with that name, so the server releases it with the window). The owned window's process
  imports the handle and calls `set_parent_of`. No wineserver or win32u change.
- `74e69d5d9f2 winewayland: Fall back to xdg-foreign-unstable-v1 to import the parent toplevel.`: **mutter 42 has only v1**
  (the draft's "both advertised" meant exporter and importer; v2 came with mutter 44). All testing here went through v1; the
  v2 calls are the same sequence with other names and are compile-tested only.

## Verification
- Probe (`tests/wl_xowner.sh`, cross-process cases): B stays fully visible above A before and after clicks on A; with the
  owner hidden and shown again the import is `destroyed` and redone with the new handle; 0 protocol errors.
- Inventor (inv2): the trial popup is above the main window at start and after clicks on the main window; Wine foreground
  stays the popup. ![popup above the main window](attachments/135-trial-popup-above-owner.png) (content blank: 132.)
  Trace: the popup is first owned by the splash window, then by the main window; re-imported 65 ms after the main window's handle.

## Remains
- The owned window's process is not told when a foreign owner (re)maps; it retries at its next WindowPosChanged. Enough for
  the cases above (win32u moves owned popups along with their owner), but a popup that gets no further position change after
  its owner maps stays unparented.
- A loop built from stale parents across two processes cannot be detected by either process (needs SetWindowLongPtr owner
  changes in both without position updates); a protocol error on wlroots, a warning on mutter.
- Placement: the popup is where mutter puts it, not at its Win32 position (136).
