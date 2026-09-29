/* recentdocs_leak.c: process handle count around 100 SHAddToRecentDocs calls per flag
 * (soak finding: Wine leaks HKCU\...\CurrentVersion\Explorer key handles).
 * x86_64-w64-mingw32-gcc -O2 -o recentdocs_leak.exe recentdocs_leak.c -lshell32 -lole32 -luuid -lntdll */
#define COBJMACROS
#define _WIN32_WINNT 0x0601
#include <windows.h>
#include <shlobj.h>
#include <winternl.h>
#include <stdio.h>
#include <stdlib.h>

/* own handle count from SystemExtendedHandleInformation (Wine stubs GetProcessHandleCount) */
typedef struct { PVOID Object; ULONG_PTR Pid; ULONG_PTR Handle; ULONG Access; USHORT Bt, Type; ULONG Attr, Res; } HEX;
static DWORD handles(void)
{
    static ULONG_PTR *buf; ULONG sz = 1 << 24, n = 0, i;
    HEX *h;
    if (!buf) buf = malloc(sz);
    if (NtQuerySystemInformation(64, buf, sz, NULL)) return 0;
    h = (HEX *)(buf + 2);
    for (i = 0; i < buf[0]; i++) if (h[i].Pid == GetCurrentProcessId()) n++;
    return n;
}

int main(void)
{
    WCHAR path[MAX_PATH];
    IShellItem *item;
    SHARDAPPIDINFO appid;
    DWORD before, after;
    int i;

    CoInitialize(NULL);
    GetTempPathW(MAX_PATH, path);
    wcscat(path, L"recentdocs_leak.txt");
    CloseHandle(CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL));
    if (FAILED(SHCreateItemFromParsingName(path, NULL, &IID_IShellItem, (void **)&item))) return 1;
    appid.psi = item;
    appid.pszAppID = L"Soak.Test";

    before = handles();
    for (i = 0; i < 100; i++) SHAddToRecentDocs(SHARD_SHELLITEM, item);
    after = handles();
    printf("SHARD_SHELLITEM x100: handles %lu -> %lu\n", before, after);

    before = after;
    for (i = 0; i < 100; i++) SHAddToRecentDocs(SHARD_APPIDINFO, &appid);
    after = handles();
    printf("SHARD_APPIDINFO x100: handles %lu -> %lu\n", before, after);

    before = after;
    for (i = 0; i < 100; i++) SHAddToRecentDocs(SHARD_PATHW, path);
    after = handles();
    printf("SHARD_PATHW x100: handles %lu -> %lu\n", before, after);

    IShellItem_Release(item);
    DeleteFileW(path);
    return 0;
}
