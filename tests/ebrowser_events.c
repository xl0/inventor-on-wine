/* ExplorerBrowser host like Inventor's file dialog (issues 041, 043).
 * Prints the display names of the folder chain (SHGetNameFromIDList per SIGDN,
 * SHGetFileInfo), then hosts an ExplorerBrowser on DIR (default: .\ebdir with
 * box.ipt) with a site serving ICommDlgBrowser3, IExplorerBrowserEvents and a
 * DShellFolderViewEvents sink on the view, clicks the first item, then
 * double-clicks it (SendInput) and logs every callback.
 * Usage: ebrowser_events.exe [DIR] [noclick] [verbose] (verbose: log all QueryService calls)
 * Build: x86_64-w64-mingw32-gcc -O2 -o ebrowser_events.exe ebrowser_events.c -lole32 -loleaut32 -lshell32 -luuid -lcomctl32 */
#define COBJMACROS
#define _WIN32_WINNT 0x0a00
#include <windows.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <stdio.h>

static const GUID DIID_DSFVE = {0x62112aa2, 0xebe4, 0x11cf, {0xa5, 0xfb, 0x00, 0x20, 0xaf, 0xe7, 0x29, 0x2d}};
static DWORD t0;
static BOOL verbose;
static IExplorerBrowser *eb;
static IShellView *view;
static HWND main_hwnd;

#define LOG(...) do { printf("%5lu ", GetTickCount() - t0); printf(__VA_ARGS__); printf("\n"); fflush(stdout); } while (0)

static void log_selection(const char *who)
{
    IFolderView *fv;
    int n = -1, focus = -1;
    if (view && SUCCEEDED(IShellView_QueryInterface(view, &IID_IFolderView, (void **)&fv)))
    {
        IFolderView_ItemCount(fv, SVGIO_SELECTION, &n);
        IFolderView_GetFocusedItem(fv, &focus);
        IFolderView_Release(fv);
    }
    LOG("  %s: selected %d, focused %d", who, n, focus);
    if (view && SUCCEEDED(IShellView_QueryInterface(view, &IID_IFolderView2, (void **)&fv)))
    {
        /* what Inventor does on CDBOSC_SELCHANGE */
        IShellItemArray *array;
        HRESULT hr = IFolderView2_GetSelection((IFolderView2 *)fv, FALSE, &array);
        LOG("  IFolderView2::GetSelection %#lx", hr);
        if (SUCCEEDED(hr))
        {
            IShellItem *item;
            WCHAR *name;
            DWORD count;
            IShellItemArray_GetCount(array, &count);
            IShellItemArray_GetItemAt(array, 0, &item);
            IShellItem_GetDisplayName(item, SIGDN_NORMALDISPLAY, &name);
            LOG("  %lu items, first \"%ls\"", count, name);
            CoTaskMemFree(name);
            IShellItem_Release(item);
            IShellItemArray_Release(array);
        }
        IFolderView2_Release((IFolderView2 *)fv);
    }
}

/* site: IServiceProvider + ICommDlgBrowser3 */
static HRESULT WINAPI cdb_QI(ICommDlgBrowser3 *iface, REFIID riid, void **out);
static ULONG WINAPI cdb_AddRef(ICommDlgBrowser3 *iface) { return 2; }
static ULONG WINAPI cdb_Release(ICommDlgBrowser3 *iface) { return 1; }
static HRESULT WINAPI cdb_OnDefaultCommand(ICommDlgBrowser3 *iface, IShellView *sv)
{
    LOG("ICommDlgBrowser::OnDefaultCommand"); log_selection("OnDefaultCommand");
    return S_OK;
}
static HRESULT WINAPI cdb_OnStateChange(ICommDlgBrowser3 *iface, IShellView *sv, ULONG change)
{
    LOG("ICommDlgBrowser::OnStateChange %lu", change);
    if (change == CDBOSC_SELCHANGE) log_selection("OnStateChange");
    return S_OK;
}
static HRESULT WINAPI cdb_IncludeObject(ICommDlgBrowser3 *iface, IShellView *sv, PCUITEMID_CHILD pidl) { return S_OK; }
static HRESULT WINAPI cdb_Notify(ICommDlgBrowser3 *iface, IShellView *sv, DWORD type)
{
    LOG("ICommDlgBrowser2::Notify %lu", type);
    return S_OK;
}
static HRESULT WINAPI cdb_GetDefaultMenuText(ICommDlgBrowser3 *iface, IShellView *sv, LPWSTR buf, int len) { return S_FALSE; }
static HRESULT WINAPI cdb_GetViewFlags(ICommDlgBrowser3 *iface, DWORD *flags) { *flags = 0; return S_OK; }
static HRESULT WINAPI cdb_OnColumnClicked(ICommDlgBrowser3 *iface, IShellView *sv, int col) { return S_OK; }
static HRESULT WINAPI cdb_GetCurrentFilter(ICommDlgBrowser3 *iface, LPWSTR buf, int len) { return S_FALSE; }
static HRESULT WINAPI cdb_OnPreViewCreated(ICommDlgBrowser3 *iface, IShellView *sv)
{
    LOG("ICommDlgBrowser3::OnPreViewCreated");
    return S_OK;
}
static ICommDlgBrowser3Vtbl cdb_vtbl = {cdb_QI, cdb_AddRef, cdb_Release, cdb_OnDefaultCommand, cdb_OnStateChange,
    cdb_IncludeObject, cdb_Notify, cdb_GetDefaultMenuText, cdb_GetViewFlags, cdb_OnColumnClicked,
    cdb_GetCurrentFilter, cdb_OnPreViewCreated};
static ICommDlgBrowser3 cdb = {&cdb_vtbl};

static HRESULT WINAPI sp_QI(IServiceProvider *iface, REFIID riid, void **out) { return cdb_QI(&cdb, riid, out); }
static ULONG WINAPI sp_AddRef(IServiceProvider *iface) { return 2; }
static ULONG WINAPI sp_Release(IServiceProvider *iface) { return 1; }
static HRESULT WINAPI sp_QueryService(IServiceProvider *iface, REFGUID sid, REFIID riid, void **out)
{
    WCHAR a[40], b[40];
    StringFromGUID2(sid, a, 40); StringFromGUID2(riid, b, 40);
    if (verbose || IsEqualGUID(sid, &SID_SExplorerBrowserFrame)) LOG("QueryService %ls %ls", a, b);
    *out = NULL;
    if (IsEqualGUID(sid, &SID_SExplorerBrowserFrame)) return cdb_QI(&cdb, riid, out);
    return E_NOINTERFACE;
}
static IServiceProviderVtbl sp_vtbl = {sp_QI, sp_AddRef, sp_Release, sp_QueryService};
static IServiceProvider sp = {&sp_vtbl};

static HRESULT WINAPI cdb_QI(ICommDlgBrowser3 *iface, REFIID riid, void **out)
{
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IServiceProvider)) *out = &sp;
    else if (IsEqualIID(riid, &IID_ICommDlgBrowser) || IsEqualIID(riid, &IID_ICommDlgBrowser2)
             || IsEqualIID(riid, &IID_ICommDlgBrowser3)) *out = &cdb;
    else { *out = NULL; return E_NOINTERFACE; }
    return S_OK;
}

/* DShellFolderViewEvents sink */
static HRESULT WINAPI disp_QI(IDispatch *iface, REFIID riid, void **out)
{
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IDispatch) || IsEqualIID(riid, &DIID_DSFVE))
    { *out = iface; return S_OK; }
    *out = NULL; return E_NOINTERFACE;
}
static ULONG WINAPI disp_AddRef(IDispatch *iface) { return 2; }
static ULONG WINAPI disp_Release(IDispatch *iface) { return 1; }
static HRESULT WINAPI disp_GetTypeInfoCount(IDispatch *iface, UINT *n) { *n = 0; return S_OK; }
static HRESULT WINAPI disp_GetTypeInfo(IDispatch *iface, UINT i, LCID l, ITypeInfo **ti) { return E_NOTIMPL; }
static HRESULT WINAPI disp_GetIDsOfNames(IDispatch *iface, REFIID riid, LPOLESTR *n, UINT c, LCID l, DISPID *d) { return E_NOTIMPL; }
static HRESULT WINAPI disp_Invoke(IDispatch *iface, DISPID id, REFIID riid, LCID l, WORD flags, DISPPARAMS *p,
                                  VARIANT *res, EXCEPINFO *ei, UINT *arg)
{
    LOG("DShellFolderViewEvents dispid %ld", id);
    if (id == 200) log_selection("SelectionChanged");
    if (res) { VariantInit(res); V_VT(res) = VT_BOOL; V_BOOL(res) = VARIANT_FALSE; }
    return S_OK;
}
static IDispatchVtbl disp_vtbl = {disp_QI, disp_AddRef, disp_Release, disp_GetTypeInfoCount, disp_GetTypeInfo,
    disp_GetIDsOfNames, disp_Invoke};
static IDispatch disp = {&disp_vtbl};

/* IExplorerBrowserEvents */
static HRESULT WINAPI ebe_QI(IExplorerBrowserEvents *iface, REFIID riid, void **out)
{
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IExplorerBrowserEvents)) { *out = iface; return S_OK; }
    *out = NULL; return E_NOINTERFACE;
}
static ULONG WINAPI ebe_AddRef(IExplorerBrowserEvents *iface) { return 2; }
static ULONG WINAPI ebe_Release(IExplorerBrowserEvents *iface) { return 1; }
static HRESULT WINAPI ebe_OnNavigationPending(IExplorerBrowserEvents *iface, PCIDLIST_ABSOLUTE pidl)
{
    LOG("IExplorerBrowserEvents::OnNavigationPending"); return S_OK;
}
static HRESULT WINAPI ebe_OnViewCreated(IExplorerBrowserEvents *iface, IShellView *sv)
{
    IConnectionPointContainer *cpc;
    IConnectionPoint *cp;
    IDispatch *d;
    DWORD cookie;
    HRESULT hr;

    LOG("IExplorerBrowserEvents::OnViewCreated");
    if (view) IShellView_Release(view);
    view = sv;
    IShellView_AddRef(sv);
    hr = IShellView_GetItemObject(sv, SVGIO_BACKGROUND, &IID_IDispatch, (void **)&d);
    LOG("  GetItemObject(SVGIO_BACKGROUND, IDispatch) %#lx", hr);
    if (FAILED(hr)) return S_OK;
    hr = IDispatch_QueryInterface(d, &IID_IConnectionPointContainer, (void **)&cpc);
    LOG("  QI(IConnectionPointContainer) %#lx", hr);
    if (SUCCEEDED(hr))
    {
        hr = IConnectionPointContainer_FindConnectionPoint(cpc, &DIID_DSFVE, &cp);
        LOG("  FindConnectionPoint(DShellFolderViewEvents) %#lx", hr);
        if (SUCCEEDED(hr))
        {
            hr = IConnectionPoint_Advise(cp, (IUnknown *)&disp, &cookie);
            LOG("  Advise %#lx", hr);
            IConnectionPoint_Release(cp);
        }
        IConnectionPointContainer_Release(cpc);
    }
    IDispatch_Release(d);
    return S_OK;
}
static HRESULT WINAPI ebe_OnNavigationComplete(IExplorerBrowserEvents *iface, PCIDLIST_ABSOLUTE pidl)
{
    LOG("IExplorerBrowserEvents::OnNavigationComplete"); return S_OK;
}
static HRESULT WINAPI ebe_OnNavigationFailed(IExplorerBrowserEvents *iface, PCIDLIST_ABSOLUTE pidl)
{
    LOG("IExplorerBrowserEvents::OnNavigationFailed"); return S_OK;
}
static IExplorerBrowserEventsVtbl ebe_vtbl = {ebe_QI, ebe_AddRef, ebe_Release, ebe_OnNavigationPending,
    ebe_OnViewCreated, ebe_OnNavigationComplete, ebe_OnNavigationFailed};
static IExplorerBrowserEvents ebe = {&ebe_vtbl};

static void print_names(PIDLIST_ABSOLUTE pidl)
{
    static const struct { SIGDN s; const char *n; } sigdn[] = {
        {SIGDN_NORMALDISPLAY, "NORMALDISPLAY"}, {SIGDN_PARENTRELATIVEFORUI, "PARENTRELATIVEFORUI"},
        {SIGDN_PARENTRELATIVEFORADDRESSBAR, "PARENTRELATIVEFORADDRESSBAR"}, {SIGDN_PARENTRELATIVE, "PARENTRELATIVE"},
        {SIGDN_PARENTRELATIVEEDITING, "PARENTRELATIVEEDITING"}, {SIGDN_PARENTRELATIVEPARSING, "PARENTRELATIVEPARSING"},
        {SIGDN_DESKTOPABSOLUTEEDITING, "DESKTOPABSOLUTEEDITING"}};
    PIDLIST_ABSOLUTE p = ILClone(pidl);
    unsigned int i;

    for (;;)
    {
        SHFILEINFOW fi = {0};
        printf("level %u:", ILGetSize(p));
        for (i = 0; i < ARRAYSIZE(sigdn); i++)
        {
            WCHAR *name = NULL;
            HRESULT hr = SHGetNameFromIDList(p, sigdn[i].s, &name);
            if (SUCCEEDED(hr)) printf(" %s=\"%ls\"", sigdn[i].n, name);
            else printf(" %s=%#lx", sigdn[i].n, hr);
            CoTaskMemFree(name);
        }
        SHGetFileInfoW((const WCHAR *)p, 0, &fi, sizeof(fi), SHGFI_PIDL | SHGFI_DISPLAYNAME);
        printf(" SHGFI_DISPLAYNAME=\"%ls\"\n", fi.szDisplayName);
        if (!ILRemoveLastID(p)) break;
    }
    ILFree(p);
    fflush(stdout);
}

static void click(int x, int y, int count)
{
    INPUT in[2] = {{INPUT_MOUSE}, {INPUT_MOUSE}};
    SetCursorPos(x, y);
    in[0].mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
    in[1].mi.dwFlags = MOUSEEVENTF_LEFTUP;
    while (count--) SendInput(2, in, sizeof(INPUT));
}

static BOOL item_point(POINT *pt)
{
    IFolderView *fv;
    PITEMID_CHILD child;
    HWND defview, list;
    HRESULT hr;

    if (!view || FAILED(IShellView_QueryInterface(view, &IID_IFolderView, (void **)&fv))) return FALSE;
    hr = IFolderView_Item(fv, 0, &child);
    if (SUCCEEDED(hr)) hr = IFolderView_GetItemPosition(fv, child, pt);
    LOG("item 0 position %#lx (%ld,%ld)", hr, pt->x, pt->y);
    if (FAILED(hr)) pt->x = pt->y = 16; /* not implemented on Windows: first icon is at the top left */
    if (SUCCEEDED(hr)) ILFree(child);
    IFolderView_Release(fv);
    IShellView_GetWindow(view, &defview);
    list = GetWindow(defview, GW_CHILD);
    pt->x += 24; pt->y += 16;
    ClientToScreen(list, pt);
    return TRUE;
}

static LRESULT CALLBACK proc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    static int step;
    POINT pt;

    switch (m)
    {
    case WM_TIMER:
        switch (step++)
        {
        case 0:
            SetForegroundWindow(h);
            if (item_point(&pt)) { LOG("click (%ld,%ld)", pt.x, pt.y); click(pt.x, pt.y, 1); }
            break;
        case 2:
            if (item_point(&pt)) { LOG("double-click (%ld,%ld)", pt.x, pt.y); click(pt.x, pt.y, 2); }
            break;
        case 4:
            DestroyWindow(h);
            break;
        }
        return 0;
    case WM_SIZE:
        if (eb)
        {
            RECT rc = {0, 0, LOWORD(l), HIWORD(l)};
            IExplorerBrowser_SetRect(eb, NULL, rc);
        }
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

int main(int argc, char **argv)
{
    WNDCLASSW wc = {0, proc, 0, 0, GetModuleHandleW(0), 0, LoadCursor(0, IDC_ARROW), (HBRUSH)(COLOR_BTNFACE + 1), 0, L"ebev"};
    FOLDERSETTINGS fs = {FVM_ICON, 0};
    WCHAR dir[MAX_PATH] = {0};
    BOOL noclick = FALSE;
    int i;
    PIDLIST_ABSOLUTE pidl;
    RECT rc = {0, 0, 600, 400};
    DWORD cookie;
    HRESULT hr;
    MSG msg;

    t0 = GetTickCount();
    OleInitialize(NULL);
    for (i = 1; i < argc; i++)
    {
        if (!strcmp(argv[i], "noclick")) noclick = TRUE;
        else if (!strcmp(argv[i], "verbose")) verbose = TRUE;
        else MultiByteToWideChar(CP_ACP, 0, argv[i], -1, dir, MAX_PATH);
    }
    if (!dir[0])
    {
        HANDLE f;
        GetFullPathNameW(L"ebdir", MAX_PATH, dir, NULL);
        CreateDirectoryW(dir, NULL);
        SetCurrentDirectoryW(dir);
        f = CreateFileW(L"box.ipt", GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
        WriteFile(f, "x", 1, &cookie, NULL);
        CloseHandle(f);
    }
    hr = SHParseDisplayName(dir, NULL, &pidl, 0, NULL);
    printf("dir %ls: %#lx\n", dir, hr);
    if (FAILED(hr)) return 1;
    print_names(pidl);

    RegisterClassW(&wc);
    main_hwnd = CreateWindowW(L"ebev", L"ebrowser_events", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 50, 50, 640, 460, 0, 0, 0, 0);
    hr = CoCreateInstance(&CLSID_ExplorerBrowser, NULL, CLSCTX_INPROC_SERVER, &IID_IExplorerBrowser, (void **)&eb);
    LOG("CoCreateInstance %#lx", hr);
    if (FAILED(hr)) return 1;
    {
        IObjectWithSite *ows;
        IExplorerBrowser_QueryInterface(eb, &IID_IObjectWithSite, (void **)&ows);
        IObjectWithSite_SetSite(ows, (IUnknown *)&sp);
        IObjectWithSite_Release(ows);
    }
    GetClientRect(main_hwnd, &rc);
    hr = IExplorerBrowser_Initialize(eb, main_hwnd, &rc, &fs);
    LOG("Initialize %#lx", hr);
    IExplorerBrowser_SetOptions(eb, EBO_NOBORDER);
    IExplorerBrowser_Advise(eb, &ebe, &cookie);
    hr = IExplorerBrowser_BrowseToIDList(eb, pidl, SBSP_ABSOLUTE);
    LOG("BrowseToIDList %#lx", hr);
    SetTimer(main_hwnd, 1, 1500, NULL);
    if (noclick) KillTimer(main_hwnd, 1);
    while (GetMessageW(&msg, 0, 0, 0)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    IExplorerBrowser_Unadvise(eb, cookie);
    if (view) IShellView_Release(view);
    IExplorerBrowser_Destroy(eb);
    IExplorerBrowser_Release(eb);
    LOG("done");
    return 0;
}
