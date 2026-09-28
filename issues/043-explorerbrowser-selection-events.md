# 043 Inventor file dialogs: selecting / double-clicking a file does nothing
Status: fixed · Owner: worker-041/043 · Branch: fix/043-explorerbrowser-selection (b85fb14ce37) · Found in: UI test campaign (Place Component, Open)

## Symptom (integ d53133a66a1, :98)
Inventor's Open / Place Component / Save As dialogs host Wine's ExplorerBrowser
(ExplorerBrowserControl > SHELLDLL_DefView > SysListView32) in a WPF window.
Clicking a file highlights it in the list, but Inventor never hears about it:
the File name box stays empty, no preview/Options/"1 item selected".
Double-clicking a file does nothing (double-clicking folders navigates, which
the browser does itself). Workaround: type the name and press Enter / Open.
- Wine (ui1.ipt selected): ![wine](attachments/043-file-select-wine.png)
- VM (box.ipt selected): ![vm](attachments/043-file-select-vm.png)
  File name "box", thumbnail, Model State / Design View options, status
  "1 item selected".

## Suspects
The host learns about selection / default command through callbacks the
view makes to its browser: ICommDlgBrowser(2/3)::OnStateChange(CDBOSC_SELCHANGE),
OnDefaultCommand, IncludeObject (obtained via IServiceProvider on the site,
SID_SExplorerBrowserFrame / IID_ICommDlgBrowser), or
IExplorerBrowserEvents / IShellView events (e.g. IFolderView selection via
DIID_DShellFolderViewEvents). Check which one Wine's shellview/ExplorerBrowser
fails to call (`WINEDEBUG=+shell` during a click). The filter
("Component Files (*.ipt;*.iam)") is applied, so IncludeObject may already work.
Related: 041 (breadcrumb names) is the same dialog.

## Repro
Inventor on :98: Assemble > Place (or Ctrl+O), open a folder with an .ipt,
click the file.

## Outcome
Not an IFileDialog problem: Inventor's dialog is its own WPF window hosting an
ExplorerBrowser from native FwUICommon.dll. Its site answers
QueryService(IID_ICommDlgBrowser = SID_SExplorerBrowserFrame, ...) with an
ICommDlgBrowser3 (also IFileDialog* riids, SID_ExplorerPaneVisibility).
On OnStateChange(CDBOSC_SELCHANGE) and OnDefaultCommand it reads the selection with
IShellView -> IFolderView2::GetSelection(FALSE) -> IShellItemArray
(SIGDN_NORMALDISPLAY / SIGDN_FILESYSPATH / GetAttributes per item).
Wine already delivered the ICommDlgBrowser callbacks; GetSelection was a stub
(E_NOTIMPL), so Inventor saw no selection.
Windows ground truth (tests/ebrowser_events.c, VM): click -> OnStateChange(SELCHANGE),
GetSelection = 1 item; double-click -> SELCHANGE + OnDefaultCommand. No selection:
GetSelection(FALSE) = HRESULT_FROM_WIN32(ERROR_NOT_FOUND) with NULL, (TRUE) = the folder.
Fix: implement GetSelection (shell32 shlview test; removes a todo_wine in comdlg32 itemdlg).
Other differences seen, not needed by Inventor: Windows' view background IDispatch has
a DShellFolderViewEvents connection point (Wine: no IConnectionPointContainer), and
Windows calls ICommDlgBrowser3::OnPreViewCreated.
Needs Inventor verification (coordinator).
