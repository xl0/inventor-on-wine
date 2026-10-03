# 142 Inventor Studio "Render Output" dialog: background between its controls shows stale pixels (low)
Status: draft (low, cosmetic) · Owner: - · Branch: - · Found in: UI pass 2 (inst/ui2, inv :98, build/ 04293594c50) · Windows reference: unknown

## Symptom
Inventor Studio > Render Image > Render (rendering itself works: 32 iterations, image drawn, scroll bars, save button).
In the top strip of the Render Output dialog (#32770, 683x600) the gaps between the "1 / 1" counter, the "Completed..."
label, the progress bar and the two buttons show leftovers of whatever was on screen there before: a tooltip text
("Press F1 for more help" from the ribbon tooltip that was open when Render was clicked) and slivers of the viewport
(black bars). Screenshot: [attachments/142-render-output-gap-ghost.png](attachments/142-render-output-gap-ghost.png).

## Steps (2 of 2)
Sheet-metal part (Mounting Bracket.ipt, flat pattern) > Environments > Inventor Studio > Render Image (ribbon) > Render in
the Render Image dialog. Wait 10 s. Closed and repeated once: same ghost, in the same place.

## Notes
The dialog's controls are Static/Button/msctls_progress32 children on a plain dialog; the gaps are painted by the
dialog's WM_ERASEBKGND / background brush. Looks like the dialog background is not painted (or is clipped out) in the
strip while the image area below is. Not checked: does it also happen with a dialog moved/resized, with/without openbox
compositing. Cannot judge against Windows without a capture (it surely does not show a tooltip there).

## Guess at the component (guess)
win32u/winex11 (window surface clip of a dialog that is created while a layered tooltip covers its position), or the
app's own owner-draw background. Low priority.
