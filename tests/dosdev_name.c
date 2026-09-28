/* DOS device names ("con", "nul.txt", "com1", ...) in bare, relative and full
 * paths: RtlIsDosDeviceName_U, GetFullPathNameW, RtlDosPathNameToNtPathName_U
 * and CreateFileW. Windows 11 treats "C:\dir\con.iam" as an ordinary file
 * (Inventor saves one fine there). Runs in a temp dir D (printed as <D>),
 * which also is the cwd; D\sub exists, D\none does not.
 * Build: x86_64-w64-mingw32-gcc -O2 -o dosdev_name.exe dosdev_name.c -lntdll */
#define _WIN32_WINNT 0x0600
#include <windows.h>
#include <winternl.h>
#include <stdio.h>

ULONG WINAPI RtlIsDosDeviceName_U(PCWSTR);
NTSTATUS WINAPI RtlDosPathNameToNtPathName_U_WithStatus(PCWSTR, UNICODE_STRING *, PWSTR *, void *);

static WCHAR dir[MAX_PATH];

/* print with <D> for the temp dir, \xNNNN for non-ASCII */
static void pr(const WCHAR *s)
{
    size_t n = wcslen(dir);
    while (*s)
    {
        if (!_wcsnicmp(s, dir, n)) { printf("<D>"); s += n; continue; }
        if (*s < 0x20 || *s > 0x7e) printf("\\x%04x", *s); else putchar(*s);
        s++;
    }
}

static void probe(const WCHAR *path, BOOL create)
{
    WCHAR full[MAX_PATH], *part = NULL;
    UNICODE_STRING nt;
    NTSTATUS status;
    ULONG len;

    printf("[");
    pr(path);
    printf("] dosdev=%08lx", RtlIsDosDeviceName_U(path));
    len = GetFullPathNameW(path, MAX_PATH, full, &part);
    printf(" full=");
    if (len) { pr(full); printf(" part="); if (part) pr(part); else printf("NULL"); }
    else printf("<err %lu>", GetLastError());
    status = RtlDosPathNameToNtPathName_U_WithStatus(path, &nt, &part, NULL);
    printf(" nt=");
    if (!status) { pr(nt.Buffer); printf(" part="); if (part) pr(part); else printf("NULL"); RtlFreeUnicodeString(&nt); }
    else printf("<%08lx>", status);
    if (create)
    {
        HANDLE h = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
        if (h == INVALID_HANDLE_VALUE) printf(" create=err%lu", GetLastError());
        else
        {
            WCHAR fin[MAX_PATH];
            DWORD type = GetFileType(h);
            if (!GetFinalPathNameByHandleW(h, fin, MAX_PATH, 0)) swprintf(fin, MAX_PATH, L"<err %lu>", GetLastError());
            printf(" create=ok type=%lu final=", type);
            pr(fin);
            CloseHandle(h);
            if (type == FILE_TYPE_DISK) DeleteFileW(path);
        }
    }
    printf("\n");
}

int main(void)
{
    static const WCHAR *names[] = {
        L"con", L"CON", L"con.iam", L"con.txt.bak", L"con ", L"con  .txt", L"con.", L"con. .",
        L"con:", L"con::", L"con:foo", L"con .:",
        L"nul", L"nul.txt", L"nul ", L"nul.", L"nul:", L"nul::", L"nul. . :", L"nul:aaa",
        L"aux", L"aux.c", L"prn", L"prn.x", L"prn ", L"prn:",
        L"com1", L"com1.txt", L"com1:", L"com0", L"com9", L"com\x00b9", L"com\x00b2", L"com\x00b3",
        L"com\x00b9.txt", L"lpt1", L"lpt1.txt", L"lpt\x00b9", L"lpt0", L"com10",
        L"conin$", L"CONOUT$", L"conin$.txt", L"conin$ ", L"conin$:", L"conerr$", L"foo.con", NULL };
    static const WCHAR *pfx[] = { L"", L"c:", L".\\", L"sub\\", L"<D>\\", L"<D>\\sub\\", L"<D>\\none\\", NULL };
    WCHAR path[MAX_PATH], sub[MAX_PATH];
    int i, j;

    GetTempPathW(MAX_PATH, dir);
    lstrcatW(dir, L"dosdev_test");
    CreateDirectoryW(dir, NULL);
    swprintf(sub, MAX_PATH, L"%ls\\sub", dir);
    CreateDirectoryW(sub, NULL);
    SetCurrentDirectoryW(dir);

    for (i = 0; names[i]; i++)
        for (j = 0; pfx[j]; j++)
        {
            if (!wcsncmp(pfx[j], L"<D>", 3)) swprintf(path, MAX_PATH, L"%ls%ls%ls", dir, pfx[j] + 3, names[i]);
            else swprintf(path, MAX_PATH, L"%ls%ls", pfx[j], names[i]);
            probe(path, j == 0 || j == 3 || j == 4 || j == 5);
        }

    probe(L"\\\\.\\con", FALSE);
    probe(L"\\\\.\\nul", FALSE);
    probe(L"\\\\.\\con.txt", FALSE);
    probe(L"\\\\.\\CONIN$", FALSE);
    probe(L"\\??\\con", FALSE);
    probe(L"\\??\\CONIN$", FALSE);
    probe(L"\\\\?\\con", FALSE);
    probe(L"\\con", FALSE);
    probe(L"\\nul", FALSE);
    probe(L"\\windows\\nul", FALSE);
    probe(L"\\windows\\con", FALSE);
    probe(L"con\\x", FALSE);
    probe(L"nul\\x", FALSE);

    /* NtCreateFile on an NT path: never parsed for DOS devices */
    {
        UNICODE_STRING us;
        OBJECT_ATTRIBUTES attr;
        IO_STATUS_BLOCK io;
        HANDLE h;
        NTSTATUS status;
        WCHAR nt[MAX_PATH + 8];

        swprintf(nt, ARRAYSIZE(nt), L"\\??\\%ls\\con.iam", dir);
        RtlInitUnicodeString(&us, nt);
        InitializeObjectAttributes(&attr, &us, OBJ_CASE_INSENSITIVE, NULL, NULL);
        status = NtCreateFile(&h, GENERIC_WRITE | DELETE | SYNCHRONIZE, &attr, &io, NULL, 0, 0, FILE_OVERWRITE_IF,
                              FILE_SYNCHRONOUS_IO_NONALERT | FILE_DELETE_ON_CLOSE, NULL, 0);
        printf("NtCreateFile(\\??\\<D>\\con.iam) = %08lx type=%lu\n", status, status ? 0 : GetFileType(h));
        if (!status) CloseHandle(h);
    }

    SetCurrentDirectoryW(L"C:\\");
    RemoveDirectoryW(sub);
    RemoveDirectoryW(dir);
    return 0;
}
