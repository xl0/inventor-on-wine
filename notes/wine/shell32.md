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
- SHAddToRecentDocs (Win11): PIDL, PATHA/W, SHELLITEM, APPIDINFO, APPIDINFOIDLIST add the item (async;
  SHARD_LINK with a path-only IShellLink added nothing, APPIDINFOLINK untested);
  files under %TEMP% are skipped. Wine handles only PIDL/PATHA/PATHW (067).
- Desktop `GetDisplayNameOf(SHGDN_FORPARSING)` of a regitem asks the folder for its path
  only if `HKCR\CLSID\{...}\ShellFolder\WantsFORPARSING` exists (exceptions: MyComputer,
  MyDocuments since fix/084), else it returns lowercase `::{guid}`, which
  `SHGetPathFromIDListW` returns as TRUE when the ShellFolder attributes are unreadable
  (the item is then flagged SFGAO_FILESYSTEM). Windows has no WantsFORPARSING for {450D8FBA}.
  ShellFolder keys are written last in `SHELL_RegisterShellFolders`, only if all user
  folders registered (084).
