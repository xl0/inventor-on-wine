/* Calls Inventor's own OSxFolder::GetMyDocumentsDir (WinSupport.dll), the source of the default
 * Content Center Files path (084). Usage: inv_mydocs.exe [BINDIR]
 * Build: x86_64-w64-mingw32-gcc -O2 -o inv_mydocs.exe inv_mydocs.c */
#include <windows.h>
#include <stdio.h>

int main(int argc, char **argv)
{
    const char *bin = argc > 1 ? argv[1] : "C:\\Program Files\\Autodesk\\Inventor 2027\\Bin";
    BOOL (*get)(WCHAR *, unsigned __int64);
    WCHAR path[MAX_PATH] = L"";
    HMODULE mod;
    BOOL ret;

    SetDllDirectoryA(bin);
    SetCurrentDirectoryA(bin);
    if (!(mod = LoadLibraryA("WinSupport.dll"))) { printf("LoadLibrary %lu\n", GetLastError()); return 1; }
    get = (void *)GetProcAddress(mod, "?GetMyDocumentsDir@OSxFolder@@SA_NPEA_W_K@Z");
    if (!get) { printf("no export\n"); return 1; }
    ret = get(path, MAX_PATH);
    printf("GetMyDocumentsDir %d %ls\n", ret, path);
    return 0;
}
