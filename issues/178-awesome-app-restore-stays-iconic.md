# 178 awesome: a window minimized with SC_MINIMIZE can't be restored by the app; owned windows stay hidden
Status: draft (found by worker-175, not investigated) · Owner: - · Branch: - · Found in: 175's probe matrix (integ b5d75449ffe, awesome 4.3)

## Symptom
tests/r175/owned.exe (owner + owned dialogs / tool window / layered popup) under awesome (`x/awesome-rc.lua`),
`tests/r175/minloop.sh 5 [xproc]` (D, B from tests/r175/env.sh):
- `PostMessage(owner, WM_SYSCOMMAND, SC_MINIMIZE)` then `SC_RESTORE`: afterwards the owner is still iconic
  (IsIconic 1, rect -32000,-32000) and the windows it owns are still hidden (IsWindowVisible 0): 5 of 5.
  A second-level owned dialog (owned by an owned dialog) is never hidden and stays on screen alone.
- `ShowWindow(owner, SW_MINIMIZE)` / `SW_RESTORE`: restores (5 of 5), but the owned windows are not hidden while
  the owner is minimized (Win32 visible, on screen under awesome). With an owned dialog of another process
  (`xproc`) the first restore leaves bar / dlg / tool / early hidden.
- Restore from the WM side (`c.minimized = false` after SC_MINIMIZE) works: everything is back.
Same with and without 175's fix (inst/175/minloop-awesome.txt). openbox: SC_MINIMIZE / SC_RESTORE work.

## Guesses (not verified)
winex11 restores a managed window through Iconic -> Withdrawn -> Normal (the Mutter workaround in
window_set_wm_state); awesome may manage the re-mapped window as minimized again (`_NET_WM_STATE_HIDDEN` or
WM_HINTS initial state still on the window), and Wine follows the WM_STATE Iconic it then gets.
The owned windows staying visible for SW_MINIMIZE and the second-level dialog never being hidden are win32u's
part, on every WM (draft 180, with the Windows ground truth); what is specific to awesome is the failed restore.
Inventor relevance: its own minimize button is SC_MINIMIZE; under awesome the user restores from the WM.
