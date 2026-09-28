/* Junction created like AdskLicensingInstHelper "makelink" (Go, go-winio):
 * CreateDirectory(link), CreateFile(link, GENERIC_WRITE, OPEN_EXISTING,
 * FILE_FLAG_BACKUP_SEMANTICS), FSCTL_SET_REPARSE_POINT with a mount point
 * buffer ("\??\C:\..." + print name, both NUL-terminated), then use paths
 * through the link: attributes, file open, CreateProcess (issue 052).
 * Usage: junction_mklink.exe DIR   (DIR absolute, created fresh)
 * Build: x86_64-w64-mingw32-gcc -O2 -o junction_mklink.exe junction_mklink.c */
#include <windows.h>
#include <winioctl.h>
#include <stdio.h>

typedef struct {
    DWORD tag; WORD len, reserved;
    WORD sub_off, sub_len, print_off, print_len;
    WCHAR buf[1024];
} MP;

int wmain(int argc, WCHAR **argv)
{
    WCHAR dir[MAX_PATH], target[MAX_PATH], link[MAX_PATH], path[MAX_PATH], self[MAX_PATH], nt[MAX_PATH];
    STARTUPINFOW si = {sizeof(si)};
    PROCESS_INFORMATION pi;
    DWORD ret, exitcode;
    HANDLE h;
    MP mp = {0};
    int sl, pl;

    if (argc > 1 && !wcscmp(argv[1], L"child")) return 42;
    if (argc < 2) { printf("usage: DIR\n"); return 2; }
    wcscpy(dir, argv[1]);
    CreateDirectoryW(dir, NULL);
    swprintf(target, MAX_PATH, L"%ls\\1.0", dir);
    CreateDirectoryW(target, NULL);
    swprintf(path, MAX_PATH, L"%ls\\sub", target);
    CreateDirectoryW(path, NULL);
    GetModuleFileNameW(NULL, self, MAX_PATH);
    swprintf(path, MAX_PATH, L"%ls\\sub\\probe.exe", target);
    printf("copy self: %d\n", CopyFileW(self, path, FALSE));

    /* the helper passes forward slashes in the link path */
    swprintf(link, MAX_PATH, L"%ls/Current", dir);
    printf("CreateDirectory link: %d err %lu\n", CreateDirectoryW(link, NULL), GetLastError());
    h = CreateFileW(link, GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    printf("CreateFile link: %s err %lu\n", h == INVALID_HANDLE_VALUE ? "fail" : "ok", GetLastError());

    swprintf(nt, MAX_PATH, L"\\??\\%ls", target);
    sl = wcslen(nt); pl = wcslen(target);
    mp.tag = IO_REPARSE_TAG_MOUNT_POINT;
    mp.sub_off = 0; mp.sub_len = sl * 2;
    mp.print_off = (sl + 1) * 2; mp.print_len = pl * 2;
    memcpy(mp.buf, nt, (sl + 1) * 2);
    memcpy(mp.buf + sl + 1, target, (pl + 1) * 2);
    mp.len = 8 + (sl + 1 + pl + 1) * 2;
    ret = DeviceIoControl(h, FSCTL_SET_REPARSE_POINT, &mp, 8 + mp.len, NULL, 0, &exitcode, NULL);
    printf("FSCTL_SET_REPARSE_POINT: %lu err %lu\n", ret, GetLastError());
    CloseHandle(h);

    swprintf(link, MAX_PATH, L"%ls\\Current", dir);
    printf("attrs link: %#lx\n", GetFileAttributesW(link));
    swprintf(path, MAX_PATH, L"%ls\\Current\\sub\\probe.exe", dir);
    printf("attrs through link: %#lx\n", GetFileAttributesW(path));
    swprintf(self, MAX_PATH, L"\"%ls\" child", path);
    ret = CreateProcessW(path, self, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi);
    printf("CreateProcess through link: %lu err %lu\n", ret, GetLastError());
    if (ret)
    {
        WaitForSingleObject(pi.hProcess, 10000);
        GetExitCodeProcess(pi.hProcess, &exitcode);
        printf("child exit %lu\n", exitcode);
    }
    return 0;
}
