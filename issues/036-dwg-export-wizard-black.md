# 036 Inventor dialogs render black (DWG export wizard, plain #32770 message box)
Status: wontfix (not a Wine bug: screenshot tool artifact, x/shot.sh fixed) · Owner: 036 worker · Branch: - · Found in: test campaign, invscen drawing2

## Symptom
`DrawingDocument.SaveAs("x.dwg", true)` on an .idw opens Inventor's "DWG
Drawing File Export Options" wizard (it does on Windows too, despite
SilentOperation — the scenario now uses the DWG translator instead). On Wine
(:98, integ 91495f487ad, wined3d-vk) the dialog frame appears but the whole
client area stays black: no picture, combo boxes, checkboxes or
Back/Next/Finish/Cancel buttons. It is alive: Escape closes it and the
SaveAs call returns.

- Windows: ![vm](attachments/036-dwg-export-options-vm.png)
- Wine: ![wine](attachments/036-dwg-export-options-wine.png)

On Windows it is a classic property-sheet wizard (bitmap, Configuration /
File Version combos, Post Process group, "Always Prompt for Options").

## Repro
Inventor running on :98, open any .idw (e.g. inst/invscen/drawing2/holed.idw),
File > Save As > Save Copy As, type DWG — or via the API as above. Screenshot
with x/shot.sh.

## Also: a plain message box
During a suite run Inventor showed an error box (an extrude was cancelled,
cause unknown, seen once). Also completely black
(![msgbox](attachments/036-messagebox-wine.png)), and while it was up the
MDI area behind it went black too. `tests/wintext.exe 'Autodesk Inventor
Professional'` (dumps window trees) shows an ordinary dialog:
`#32770` > `Static` (icon 32x32), `Static`, `SysLink` "The operation is
canceled. If you're experiencing an issue, ... <a href=...>Forum</a>", `Button`
"OK". A mouse click on the OK position closed it (xdotool `key` to it did not).

So it is not WPF/Qt specific: standard user32/comctl32 dialogs owned by
Inventor's main window don't paint. The trial welcome dialog (WebView2) and
the ribbon (WPF) do. Suspects: the owner/parent is a GPU/layered or
composited surface (cf. 027 layered colour-key, 023/005 surfaces), or
WM_PAINT/WM_CTLCOLOR handling with Inventor's dark theme hooks.

## Notes
Inventor.exe has WPF (wpfgfx_cor3, PresentationCore) and Qt6 loaded besides
Win32; `tests/wintext.exe` on the wizard would show its class tree.

## Outcome: the dialogs render; `xwd -root` shows them black
All Inventor dialogs looked black on :98 (Application Options too, and a MessageBox of a
tiny test app whose owner is maximized without a caption). But `xwd -id <dialog X window>`
returned the fully painted dialog, and Wine's trace shows the normal path (surface created,
1-rect clip, flushes after Expose). A plain `XGetImage` of the root (what x11vnc and
the screen show) has the dialogs painted exactly like the VM screenshot:
![raw XGetImage (left) vs xwd -root (right)](attachments/036-xgetimage-vs-xwd.png)

Cause: winex11 creates an X colormap per process, and openbox installs the focused
client's. With more than one colormap in the tree, `xwd -root` builds the image through its
multi-visual/colormap code (ReadAreaToImage: the "packed 24 bpp" output that x/shot.sh
treated as an NVIDIA quirk), and that path draws some Wine windows black. Reproduced
without Wine (Xlib client with a private TrueColor colormap, Xvfb or NVIDIA, with openbox
running): `xwd -root` black, root `XGetImage` fine; with no WM it is fine too.

Fix (harness): x/shot.sh now reads the root with XGetImage via ctypes (libX11), same
pixels as a raw grab. Earlier screenshot-based conclusions on :98/:99 (black dialogs,
parts of 005/037) may be affected; `xwd -id WIN` of one window is still fine.

Side notes: `tests/wintext.exe` now prints style/exstyle. XTEST keyboard on :98 had
Escape and Return stuck down (`xinput query-state 5`), so every dialog closed right after
opening; `xdotool keyup Escape keyup Return` cleared it.
