# Dialog manager (IsDialogMessage, GetNextDlg*Item) — checked at wine-11.18-511-gd18a5dcd1ef + fix/140

Windows facts are black-box results of `tests/r140/idm.exe` on Win11 (issue 140; raw `inst/140/idm-vm.txt`).

- Who calls it: besides real dialogs, MFC calls `IsDialogMessage(m_hWnd, msg)` from `CControlBar` / `CFormView` /
  `CDialog::PreTranslateMessage` for any message whose window is below that CWnd. hwndDlg is then a plain window
  (no DefDlgProc, DM_GETDEFID answers 0), and its children can be anything (WinForms controls in Inventor's panes).
- `user32/dialog.c:IsDialogMessageW` on Wine: WM_GETDLGCODE(key, &msg) to msg.hwnd, then per key: Tab =
  WM_NEXTDLGCTL for dialogs, `GetNextDlgTabItem` + SetFocus otherwise; arrows = `GetNextDlgGroupItem`, then
  WM_NEXTDLGCTL (a non-dialog ignores it: no focus change, Windows sets the focus itself); Enter/Escape = WM_COMMAND;
  WM_CHAR/WM_SYSCHAR = `DIALOG_IsAccelerator`, and when that finds nothing the message is dispatched (Windows eats an
  unmatched WM_CHAR). Differences to Windows: issue 148.
- `DIALOG_IsAccelerator` walks all windows below hwndDlg in tree order from msg.hwnd, descending into every window that
  is visible and enabled (Windows: only WS_EX_CONTROLPARENT), until it is back at its start. The start need not be
  reachable (hidden pane with the focus inside, or a WM_GETDLGCODE handler changing the tree): fix/140 ends the walk
  the second time it wraps at hwndDlg (before: endless loop), and does nothing when msg.hwnd is hwndDlg (before: for
  a hidden/disabled/childless dialog the walk went on with the dialog's siblings, other top-level windows included).
  A match is clicked with BM_CLICK (Windows: WM_COMMAND to the parent, no focus change).
- `GetNextDlgGroupItem` still spins when GetParent() does not lead to hwndDlg (WS_CHILD|WS_POPUP container, hwndDlg
  = desktop): issue 148.
- Windows' mnemonic search: start control = msg.hwnd or its ancestor whose parent is the dialog or a control parent;
  pass 1 = that control's group (GetNextDlgGroupItem order), pass 2 = all controls after it until it comes around.
  Each pass gives up after 1024 controls (visible as ~1024 x WM_GETDLGCODE + WM_GETTEXT per control when the start
  control cannot be reached, e.g. it is hidden). It never returns when msg.hwnd sits in a hidden or disabled control
  parent and the char matches nothing in its group, and GetNextDlgTabItem never returns for a control in a hidden
  control parent when the dialog has no reachable tab stop. So "Windows hangs too" is possible in this area: probe first.
- Neither Windows nor Wine moves the focus when an ancestor of the focus window is hidden (ShowWindow, SWP_HIDEWINDOW)
  or disabled; key messages keep going to the invisible window. `win32u/window.c:show_window` only handles
  hwnd == focus. WinForms (`Control.SetVisibleCore`) moves the focus itself before hiding, possibly to a child of
  the control being hidden.
- IsDialogMessage returns TRUE for every key message whose window is hwndDlg or below it on Windows, hidden and
  disabled windows included. A WM_CHAR / WM_SYSCHAR whose window is the dialog itself is swallowed without any
  message or search (only Alt+Space reaches WM_SYSCOMMAND): mnemonics don't work while the dialog has the focus.
