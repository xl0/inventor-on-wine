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
- `42b838976ee` / `3f39e865966`: xdg-foreign-unstable-v2 / -v1 protocol XML (wayland-protocols 1.49).
- `f218e9bda6e winewayland: Set the parent of toplevel windows owned by another process.`: every mapped toplevel is
  exported; the handle is published as window property `__wine_wayland_exported_handle` (a global atom named by the handle,
  kept alive by a second property with that name, so the server releases it with the window). The owned window's process
  imports the handle and calls `set_parent_of`. No wineserver or win32u change.
  "Other process" is decided by the owner's process id at WindowPosChanged (not by "no win_data": an in-process owner that
  died with its thread looked foreign and its owned windows kept a stale parent, `rv texit`). No `set_parent(nil)` is sent
  when there is no parent to unset (`rv msgowner 200`, `rv spam HWND 200`: 0 of 200, was 200 of 200).
- `8f3e175b631 winewayland: Fall back to xdg-foreign-unstable-v1 to import the parent toplevel.`: **mutter 42 has only v1**
  (v2 came with mutter 44). The host tests go through v1, the VM run (GNOME 50, KDE, sway) through v2.
- Both globals are bound when both exist (v2 is used): binding v1 only without v2 would need the registry events to be
  collected first; one unused proxy seemed the smaller cost.

## Parent loops across processes
The parents set by another process are invisible to the driver, and on wlroots `invalid_parent` is posted on the *parent's*
toplevel, i.e. it kills the Wayland connection of the owner's (exporting) process; mutter only logs "would create a loop".
- Legal Win32 route (review): the server refuses raw owner loops, but the driver maps owners to their root window, so
  "A owned by B, B owned by a child window of A" is a loop for the toplevels. Guard: before treating an owner as importable
  the driver walks GA_ROOT(GW_OWNER) from the owner (16 steps max) and does not import if it reaches the window itself.
  Probe: `wl_xowner.exe owner loop` + `wl_xowner.exe HWND child` (case "loop" in tests/wl_xowner.sh): pre-guard build 1 mutter
  loop warning, with the guard 0; B keeps A as parent, A gets none.
- Stale route (still open): A imported B, then A is un-owned with SetWindowLongPtr and no position update (its import
  stays), then B becomes owned by A. Neither process can see the other's stale import; `rv xloop1` + `rv xloop2` still
  produce the mutter warning (both processes stay alive there). Closing it needs a driver notification for owner changes.

## Verification
- Host probe (`tests/wl_xowner.sh`, cross-process cases): B stays fully visible above A before and after clicks on A; with the
  owner hidden and shown again the import is `destroyed` and redone with the new handle; 0 protocol errors.
- VM (vmwl/results-134.md, pre-review branch): export_toplevel / import_toplevel / set_parent_of (v2) on GNOME 50, KDE and
  sway, 0 protocol errors.
- Inventor (inv2): the trial popup is above the main window at start and after clicks on the main window; Wine foreground
  stays the popup (re-checked on the final build). ![popup above the main window](attachments/135-trial-popup-above-owner.png)
  (content blank: 132.) Trace: the popup is first owned by the splash window, then by the main window; re-imported 65 ms
  after the main window's handle.

## Remains
- The owned window's process is not told when a foreign owner (re)maps; it retries at its next WindowPosChanged (one
  property read per position change while the owner has no handle). Enough for the cases above (win32u moves owned popups
  along with their owner), but a popup that gets no further position change after its owner maps stays unparented.
- The stale-parent loop above.
- Keys typed after clicking the disabled main window do not reach a popup of another process (see 134, Remains).
- Placement: the popup is where mutter 42 puts it, not at its Win32 position (136).
