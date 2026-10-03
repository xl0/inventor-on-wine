# 148 Dialog keyboard navigation differs from Windows (IsDialogMessage, GetNextDlgTabItem/GroupItem)
Status: draft · Owner: - · Branch: - · Found in: 140 (probe `tests/r140/idm.exe`, wt/140-build 7071ce82478 vs Win11 VM) · Windows reference: `inst/140/idm-vm.txt`

## Symptom
No app failure known yet; found while establishing ground truth for 140. Of 1785 probe cases (119 window trees x
15 operations) the result on Wine differs from Windows in several hundred. Mostly edge shapes (controls inside
containers without WS_EX_CONTROLPARENT, hidden/disabled containers, non-dialog parents that call IsDialogMessage like
MFC control bars and frames), but some are plain cases. User-visible candidates: arrow keys do nothing in an MFC pane
or frame that is not a real dialog; a typed character reaches a control that would not get it on Windows.

## Evidence
`python3 tests/r140/cmp2.py inst/140/idm-vm.txt inst/140/idm-wine-fix.txt [-v LAYER]` (layers: ret cmd focus disp):
return value 86, WM_COMMAND(BN_CLICKED) 82, final focus 309, dispatched key messages 405 cases.
One line per case: `inst/140/idm-vm.brief.txt`, `inst/140/idm-wine-fix.brief.txt` (`tests/r140/brief.py`).

1. Unmatched WM_CHAR (msg.hwnd has no DLGC_WANTCHARS/WANTMESSAGE, no mnemonic matches): Windows eats it
   (IsDialogMessage TRUE, nothing dispatched); Wine dispatches it to msg.hwnd. Unmatched WM_SYSCHAR: Windows goes
   straight to WM_SYSCOMMAND(SC_KEYMENU) at the top-level window; Wine dispatches WM_SYSCHAR to msg.hwnd (same end
   result through DefWindowProc unless the control handles it). Tree 0: `0.CH_Q`, `0.SYS_Q`. (405 cases)
2. Mnemonic of a push button: Windows sends WM_COMMAND(id, BN_CLICKED) to the button's parent and leaves the focus;
   Wine sends BM_CLICK, which moves the focus to the button (`0.CH_A`: focus x on Windows, a on Wine).
   Wine also skips the WM_GETDLGCODE(char, &msg) to msg.hwnd for WM_SYSCHAR.
3. Mnemonic search scope and order: Windows searches msg.hwnd's group first, then all controls; it descends only into
   WS_EX_CONTROLPARENT windows, treats a msg.hwnd inside a plain container as that container, looks at
   hidden/disabled plain controls, and has a 1024-control cap. Wine walks every visible+enabled window's children from
   msg.hwnd. E.g. `6.CH_A`: a button inside a plain visible pane is found on Wine, not on Windows; with two buttons
   sharing a mnemonic the winner can differ.
4. Arrow keys when hwndDlg is not a dialog (plain window class, as with MFC): Windows sets the focus itself
   (`0.DOWN`: focus a); Wine sends WM_NEXTDLGCTL, which only DefDlgProc handles: focus stays (`0.DOWN`: focus x).
   Real dialogs agree (`4.DOWN`). (most of the 164 key-focus cases)
5. Tab / Shift+Tab with no tab stop to go to, or Shift+Tab with the focus on the dialog itself: Windows returns TRUE, Wine FALSE
   (`54.TAB`, `49.STAB`, `18.STAB`; 12 cases).
6. GetNextDlgTabItem / GetNextDlgGroupItem (74 cases): control inside a container that is not a control parent
   (`6.TABN`: Windows z = next after the container, Wine a = next sibling inside), dialog without children
   (`54.TABN`: Windows returns the dialog, Wine 0), hidden control parents, start control hidden.
   (Windows itself never returns from GetNextDlgTabItem in trees 66, 117, 118.)
7. Enter with a nested WS_CHILD dialog: Windows passes GetDlgItem(outer dialog, id) as the control (`70.RET`: ok),
   Wine the first control with that id in the whole tree (ok2 inside the child dialog); `77.RET`/`77.ESC`: for a
   child dialog with WS_EX_CONTROLPARENT but without DS_CONTROL Windows sends WM_COMMAND to the outer dialog, Wine to
   the child dialog.

## Guess at the component (guess)
user32 dialog.c: IsDialogMessageW, DIALOG_IsAccelerator, GetNextDlgGroupItem, DIALOG_GetNextTabItem. Items 1, 2 and 4
are small and self-contained; 3 and 6 mean reimplementing the traversal the way Windows does it (start control,
control-parent descent, group pass). Each needs its own conformance tests; the probe has the expected values.
