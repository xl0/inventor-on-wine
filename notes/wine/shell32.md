# shell32: ExplorerBrowser / ShellView (checked against 4e819f054dd)

- `SID_SExplorerBrowserFrame` is `#define`d as `IID_ICommDlgBrowser` (as in the SDK);
  hosts answer `QueryService(IID_ICommDlgBrowser, IID_ICommDlgBrowser[23])`.
  ebrowser.c:get_interfaces_from_site fetches them once; the view (shlview.c) QIs its
  IShellBrowser (the ExplorerBrowser) for ICommDlgBrowser and calls OnStateChange /
  OnDefaultCommand / IncludeObject from ShellView_OnNotify.
- Windows' DefView is DirectUI: IFolderView::GetItemPosition is E_NOTIMPL there, so
  tests can't locate items that way. Its SVGIO_BACKGROUND IDispatch has a
  DShellFolderViewEvents connection point (SelectionChanged = 200); Wine's has none.
- Hosts typically read the selection with IFolderView2::GetSelection (Inventor does).
- `tests/ebrowser_events.c` (project repo) logs all host callbacks on Windows vs Wine.
