# 141 Placing a dimension in a drawing: riched20 assertion in an endless OK-loop dialog
Status: draft · Owner: - · Branch: - · Found in: UI pass 2 (inst/ui2, inv :98, build/ 04293594c50) · Windows reference: unknown

## Symptom
In a drawing (.idw), General Dimension: pick two edges, click to place the dimension. Instead of Inventor's "Edit
Dimension" box a Wine message box appears: "Assertion failed! File: ../wine-src/dlls/riched20/caret.c Line: 232
Expression: ~para->nFlags & MEPF_REWRAP". Clicking OK ("exit the program") does not exit: the box disappears and a new
one (new HWND each time) appears at once, 4 of 4 clicks checked. Inventor never gets back to the user; the only way out is
killing the process. Cancel would start winedbg.
The box can also be invisible in a screenshot while it is mapped and on top of the stack (first occurrence: wintext listed
it, xwininfo showed it IsViewable, X screenshot showed the plain drawing; second occurrence painted normally). Seen once.

## Steps (2 of 2)
1. File > New > Standard.idw. Place Views > Base, file `C:\t\samples\2022\Models\Parts\Plate\Vertical Plate.ipt`, OK.
2. Annotate > Dimension. Click the left vertical edge of the view, click the right vertical edge, click in the empty
   sheet area above the view to place. -> assertion box (also 2 of 2 on Stapler.iam with a vertical edge pair of a
   projected view; the very first dimension of the session hit it).
(Picking a third object after two parallel edges gives Inventor's own "Invalid input for Request" box, that one is fine.)

## Evidence
`inst/ui2/assert1/bt.txt` (gdb sehbt of the main thread while the box is up):
```
_wassert -> riched20 cursor_coords + 314 <- create_caret + 37 <- editor_handle_message (WM_SETFOCUS)
 <- RichEditWndProc <- mfc140u (CallWindowProc subclass) <- user32 NtUserSetFocus <- DEFDLG_RestoreFocus <- DEFDLG_Proc
```
i.e. the Edit Dimension dialog (MFC, class #32770, contains a RichEdit) takes the focus while its paragraph is still
flagged MEPF_REWRAP; `create_caret` -> `cursor_coords` asserts. Screenshot: [attachments/141-edit-dimension-assert.png](attachments/141-edit-dimension-assert.png).
The OK loop: ucrt's `_wassert` + abort path does not terminate; Inventor's handler swallows it and the same focus
change asserts again (not analysed further).

## Notes
Not reduced: a standalone RichEdit that gets text / CHARFORMAT / ITextRange+ITextFont edits and then SetFocus() does
not trip the assert on build/ nor on wt/regress-master-build (4 variants, Xvfb). Build/ carries 5 riched20 commits
over master (37e4f275b01 font linking, 2f5657c5c99 / 548754b62eb / 863129789c9 ITextFont, 04293594c50 WM_CLEAR/WM_CUT):
whether this is a regression of one of them (or an old bug, the first pass never placed a dimension in a drawing) is
untested. Cheapest next step: `WINEDEBUG=+richedit` on a repro, or run with riched20 from wt/regress-master-build.

## Guess at the component (guess)
riched20 (paragraph wrap state when the control is focused before its first layout pass: e.g. hidden/zero-size control
at creation, or a text replace that marks the paragraph without rewrapping) and msvcrt `_wassert` abort handling.
