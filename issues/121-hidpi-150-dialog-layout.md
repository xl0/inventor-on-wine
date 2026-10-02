# 121 At 150 % (LogPixels 144) the Application Options footer buttons sit in the middle of the dialog
Status: draft · Owner: - · Found in: 118 environment campaign (inv4, build/ 43790927731) · Windows reference: unknown

## Symptom
With `HKCU\Control Panel\Desktop\LogPixels=144` and the prefix restarted (`prefix.sh stop/start`; without the restart
a system-aware window gets a 2880x1620 logical screen and half of it is painted, which is expected for
a live DPI change), Inventor scales ribbon, browser, fonts and dialogs by 1.5. The API scenarios
(hello, part, drawing) all pass; the viewport is 1327x787 (it was 2287x1327 before the restart).
Visual problems:
- Application Options (`AppApplicationOptionsCmd`): the dialog is clamped to the screen height (it is already
  screen-high at 96 DPI), the content is 1.5x larger, the footer (Import/Export/Close/Cancel/Apply) stays at
  the 96-DPI-proportional position in the middle of the dialog and overlaps "Tooltip appearance"
  (`issues/attachments/121-hidpi-dialog-footer.png`; at 96 DPI the footer is at the bottom).
- Autodesk Assistant pane: the "Tech Preview has ended..." toast wraps one word per line and overlaps its
  "Learn more" link (the pane keeps its pixel width, so the WebView2 CSS width shrinks to 230 px).
- Ribbon panels collapse to icons (logical window width 1280).

## Windows
Not verified (Inventor must not run on the VM). Windows scales the same Inventor at 150 % without the
footer overlap on 1080p laptops as far as is known, but the dialog size logic is Inventor's: this may be
Inventor behaviour at 1920x1080 / 150 % (screen-high dialog does not fit), only worth chasing if the VM
at 150 % (HiDPI setting + Inventor started once the user allows it) shows the footer at the bottom.

## Repro
`reg add "HKCU\Control Panel\Desktop" /v LogPixels /t REG_DWORD /d 144 /f` in inv4, `tools/prefix.sh stop inv4 && start`,
`INV=inv4 INVSCEN_KEEP=1 INVSCEN_CMD=AppApplicationOptionsCmd tools/invscen/run.sh cmd`, screenshot.
Restore with `reg delete ... /v LogPixels /f` and another restart (done in the campaign).

## Windows ground truth (VM, 2026-10-02): not obtainable at 150 %
The VM display is 1024x768 (virtual adapter, resolution greyed out in Settings). Settings > System > Display >
Scale offers only **100 % and 125 %** (150 % needs a larger resolution), so the 150 % check was skipped
(nothing changed; Settings closed). Side observation: at 100 % and 1024x768 the Autodesk Assistant pane's
"Tech Preview has ended..." toast wraps one word per line and overlaps "Learn more" on Windows too, so that
item is Inventor's layout in a narrow pane, not Wine-specific. A 125 % run would need a sign-out for
Inventor-wide DPI to be reliable; not attempted. To get 150 %: give the VM a bigger display (e.g. 1920x1080 via
the qemu video device / a different virtual GPU) first.
