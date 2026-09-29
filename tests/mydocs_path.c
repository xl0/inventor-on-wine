/* Documents via shell PIDLs (084): Inventor's OSxFolder::GetMyDocumentsDir does
 * SHGetSpecialFolderLocation(CSIDL_PERSONAL) + SHGetPathFromIDListW.
 * Prints the PIDL shape and the names the desktop gives it per SHGDN flag.
 * Build: x86_64-w64-mingw32-gcc -O2 -o mydocs_path.exe mydocs_path.c -lshell32 -lshlwapi -lole32 -luuid */
#define COBJMACROS
#include <windows.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <stdio.h>

static void dump(const char *what, LPITEMIDLIST pidl)
{
    IShellFolder *desktop, *parent;
    LPCITEMIDLIST last;
    static const DWORD flags[] = { SHGDN_FORPARSING, SHGDN_INFOLDER | SHGDN_FORPARSING,
        SHGDN_FORADDRESSBAR | SHGDN_FORPARSING, SHGDN_NORMAL, SHGDN_INFOLDER };
    static const SIGDN sigdn[] = { SIGDN_FILESYSPATH, SIGDN_DESKTOPABSOLUTEPARSING,
        SIGDN_PARENTRELATIVEPARSING, SIGDN_DESKTOPABSOLUTEEDITING };
    WCHAR path[MAX_PATH], *name;
    STRRET str;
    SFGAOF attrs;
    UINT i, count = 0, size = ILGetSize(pidl);
    LPCITEMIDLIST p;
    HRESULT hr;

    for (p = pidl; p->mkid.cb; p = (LPCITEMIDLIST)((BYTE *)p + p->mkid.cb)) count++;
    printf("%s: %u items, %u bytes, first cb %u type %02x\n", what, count, size,
           pidl->mkid.cb, pidl->mkid.cb ? pidl->mkid.abID[0] : 0);
    path[0] = 0;
    printf("  SHGetPathFromIDListW %d %ls\n", SHGetPathFromIDListW(pidl, path), path);
    SHGetDesktopFolder(&desktop);
    for (i = 0; i < ARRAYSIZE(flags); i++)
    {
        hr = IShellFolder_GetDisplayNameOf(desktop, pidl, flags[i], &str);
        if (SUCCEEDED(hr) && SUCCEEDED(StrRetToBufW(&str, pidl, path, MAX_PATH)))
            printf("  desktop GetDisplayNameOf(%#lx) %ls\n", flags[i], path);
        else printf("  desktop GetDisplayNameOf(%#lx) %#lx\n", flags[i], hr);
    }
    if (SUCCEEDED(SHBindToParent(pidl, &IID_IShellFolder, (void **)&parent, &last)))
    {
        attrs = SFGAO_FILESYSTEM | SFGAO_FOLDER | SFGAO_FILESYSANCESTOR;
        hr = IShellFolder_GetAttributesOf(parent, 1, &last, &attrs);
        printf("  parent GetAttributesOf %#lx attrs %#lx\n", hr, attrs);
        hr = IShellFolder_GetDisplayNameOf(parent, last, SHGDN_FORPARSING, &str);
        if (SUCCEEDED(hr) && SUCCEEDED(StrRetToBufW(&str, last, path, MAX_PATH)))
            printf("  parent GetDisplayNameOf(FORPARSING) %ls\n", path);
        else printf("  parent GetDisplayNameOf(FORPARSING) %#lx\n", hr);
        IShellFolder_Release(parent);
    }
    for (i = 0; i < ARRAYSIZE(sigdn); i++)
    {
        hr = SHGetNameFromIDList(pidl, sigdn[i], &name);
        if (SUCCEEDED(hr)) { printf("  SHGetNameFromIDList(%#x) %ls\n", sigdn[i], name); CoTaskMemFree(name); }
        else printf("  SHGetNameFromIDList(%#x) %#lx\n", sigdn[i], hr);
    }
    IShellFolder_Release(desktop);
}

int main(void)
{
    LPITEMIDLIST pidl;
    WCHAR path[MAX_PATH];
    ULONG eaten;
    IShellFolder *desktop;

    /* Inventor doesn't need COM initialized on this thread for the call; try both */
    if (SUCCEEDED(SHGetSpecialFolderLocation(NULL, CSIDL_PERSONAL, &pidl)))
    { dump("SHGetSpecialFolderLocation(PERSONAL) no COM", pidl); ILFree(pidl); }
    CoInitialize(NULL);
    if (SUCCEEDED(SHGetSpecialFolderLocation(NULL, CSIDL_PERSONAL, &pidl)))
    { dump("SHGetSpecialFolderLocation(PERSONAL)", pidl); ILFree(pidl); }
    if (SUCCEEDED(SHGetKnownFolderIDList(&FOLDERID_Documents, 0, NULL, &pidl)))
    { dump("SHGetKnownFolderIDList(Documents)", pidl); ILFree(pidl); }
    SHGetDesktopFolder(&desktop);
    wcscpy(path, L"::{450D8FBA-AD25-11D0-98A8-0800361B1103}");
    if (SUCCEEDED(IShellFolder_ParseDisplayName(desktop, NULL, NULL, path, &eaten, &pidl, NULL)))
    { dump("ParseDisplayName(::{MyDocuments})", pidl); ILFree(pidl); }
    IShellFolder_Release(desktop);
    return 0;
}
