/* POSIX delete semantics ground truth (054): what DeleteFile/RemoveDirectory and
 * the disposition info classes do while other handles are open, and what those
 * handles see afterwards. Output: one line per observation, same on Wine. */
#include <windows.h>
#include <winternl.h>
#include <stdio.h>

#ifndef FILE_DISPOSITION_DELETE
#define FILE_DISPOSITION_DELETE 1
#define FILE_DISPOSITION_POSIX_SEMANTICS 2
#define FILE_DISPOSITION_ON_CLOSE 8
#define FILE_DISPOSITION_IGNORE_READONLY_ATTRIBUTE 0x10
#endif
#define FileStandardInformation_ 5
#define FileDispositionInformation_ 13
#define FileDispositionInformationEx_ 64

typedef struct { LARGE_INTEGER alloc, eof; ULONG links; BOOLEAN pending, dir; } STD_INFO;
static NTSTATUS (WINAPI *pNtQIF)(HANDLE, IO_STATUS_BLOCK *, void *, ULONG, int);
static NTSTATUS (WINAPI *pNtSIF)(HANDLE, IO_STATUS_BLOCK *, void *, ULONG, int);
static NTSTATUS (WINAPI *pNtCreateFile)(HANDLE *, ACCESS_MASK, OBJECT_ATTRIBUTES *, IO_STATUS_BLOCK *,
                                        LARGE_INTEGER *, ULONG, ULONG, ULONG, ULONG, void *, ULONG);
static char dir[MAX_PATH];

/* 1: exists, 0: not found, else the GetFileAttributes error (5: delete pending) */
static int exists(const char *p)
{
    DWORD err;
    if (GetFileAttributesA(p) != INVALID_FILE_ATTRIBUTES) return 1;
    err = GetLastError();
    return err == ERROR_FILE_NOT_FOUND ? 0 : err;
}

static int listed(const char *d, const char *name)
{
    char pat[MAX_PATH]; WIN32_FIND_DATAA fd; HANDLE h; int found = 0;
    sprintf(pat, "%s\\*", d);
    if ((h = FindFirstFileA(pat, &fd)) == INVALID_HANDLE_VALUE) return -1;
    do found |= !lstrcmpiA(fd.cFileName, name); while (FindNextFileA(h, &fd));
    FindClose(h);
    return found;
}

static void std_info(const char *what, HANDLE h)
{
    IO_STATUS_BLOCK io; STD_INFO si = {0}; BY_HANDLE_FILE_INFORMATION bi = {0};
    NTSTATUS s = pNtQIF(h, &io, &si, sizeof(si), FileStandardInformation_);
    BOOL r = GetFileInformationByHandle(h, &bi);
    printf("  %s: StdInfo %#lx links %lu pending %d; GFIBH %d links %lu\n", what, s, si.links,
           si.pending, r, bi.nNumberOfLinks);
}

static NTSTATUS disp(HANDLE h, BOOLEAN del)
{
    IO_STATUS_BLOCK io; return pNtSIF(h, &io, &del, sizeof(del), FileDispositionInformation_);
}

static NTSTATUS disp_ex(HANDLE h, ULONG flags)
{
    IO_STATUS_BLOCK io; return pNtSIF(h, &io, &flags, sizeof(flags), FileDispositionInformationEx_);
}

static void mkfile(const char *p, DWORD attr)
{
    HANDLE h = CreateFileA(p, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, attr, NULL);
    WriteFile(h, "hello", 5, NULL, NULL);
    CloseHandle(h);
}

static HANDLE open_all(const char *p, DWORD access, DWORD share)
{
    return CreateFileA(p, access, share, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
}

#define SHARE_ALL (FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE)

static void file_cases(void)
{
    char f[MAX_PATH], buf[16]; HANDLE h, h2, m; DWORD n; BOOL r; void *v;

    sprintf(f, "%s\\a.txt", dir);
    printf("A: DeleteFile with another RW handle open (share all)\n");
    mkfile(f, 0);
    h = open_all(f, GENERIC_READ | GENERIC_WRITE | DELETE, SHARE_ALL);
    r = DeleteFileA(f);
    printf("  DeleteFile %d err %lu; exists %d listed %d\n", r, r ? 0 : GetLastError(), exists(f), listed(dir, "a.txt"));
    std_info("other handle", h);
    SetFilePointer(h, 0, NULL, FILE_BEGIN);
    r = ReadFile(h, buf, 5, &n, NULL);
    printf("  read %d n %lu; ", r, n);
    r = WriteFile(h, "xy", 2, &n, NULL);
    printf("write %d n %lu\n", r, n);
    r = DeleteFileA(f);
    printf("  DeleteFile again %d err %lu\n", r, r ? 0 : GetLastError());
    h2 = CreateFileA(f, GENERIC_WRITE, 0, NULL, CREATE_NEW, 0, NULL);
    printf("  CreateFile(CREATE_NEW) same name %s err %lu\n", h2 == INVALID_HANDLE_VALUE ? "fails" : "ok",
           h2 == INVALID_HANDLE_VALUE ? GetLastError() : 0);
    if (h2 != INVALID_HANDLE_VALUE) { std_info("new file", h2); CloseHandle(h2); }
    printf("  undelete (FileDispositionInformation FALSE) on old handle %#lx; exists %d\n", disp(h, FALSE), exists(f));
    CloseHandle(h);
    printf("  after close: exists %d\n", exists(f));
    DeleteFileA(f);

    printf("B: DeleteFile with another handle without FILE_SHARE_DELETE\n");
    mkfile(f, 0);
    h = open_all(f, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE);
    r = DeleteFileA(f);
    printf("  DeleteFile %d err %lu; exists %d\n", r, r ? 0 : GetLastError(), exists(f));
    CloseHandle(h);
    DeleteFileA(f);

    printf("C: read-only file\n");
    mkfile(f, FILE_ATTRIBUTE_READONLY);
    r = DeleteFileA(f);
    printf("  DeleteFile %d err %lu; exists %d\n", r, r ? 0 : GetLastError(), exists(f));
    h = open_all(f, DELETE, SHARE_ALL);
    printf("  Ex DELETE|POSIX %#lx\n", disp_ex(h, FILE_DISPOSITION_DELETE | FILE_DISPOSITION_POSIX_SEMANTICS));
    printf("  Ex DELETE|POSIX|IGNORE_READONLY %#lx\n", disp_ex(h, FILE_DISPOSITION_DELETE |
           FILE_DISPOSITION_POSIX_SEMANTICS | FILE_DISPOSITION_IGNORE_READONLY_ATTRIBUTE));
    CloseHandle(h);
    printf("  after close: exists %d\n", exists(f));
    if (exists(f)) { SetFileAttributesA(f, 0); DeleteFileA(f); }

    printf("D: info classes on handle 1 with handle 2 open\n");
    {
        static const struct { const char *name; int ex; ULONG flags; } t[] = {
            { "Disposition TRUE", 0, 0 },
            { "Ex DELETE", 1, FILE_DISPOSITION_DELETE },
            { "Ex DELETE|POSIX", 1, FILE_DISPOSITION_DELETE | FILE_DISPOSITION_POSIX_SEMANTICS },
        };
        int i;
        for (i = 0; i < 3; i++)
        {
            NTSTATUS s;
            mkfile(f, 0);
            h = open_all(f, DELETE | GENERIC_READ, SHARE_ALL);
            h2 = open_all(f, GENERIC_READ, SHARE_ALL);
            s = t[i].ex ? disp_ex(h, t[i].flags) : disp(h, TRUE);
            printf("  %s: %#lx; before close exists %d listed %d\n", t[i].name, s, exists(f), listed(dir, "a.txt"));
            std_info("other handle before close", h2);
            CloseHandle(h);
            printf("  after closing setter: exists %d listed %d\n", exists(f), listed(dir, "a.txt"));
            std_info("other handle", h2);
            CloseHandle(h2);
            printf("  after closing all: exists %d\n", exists(f));
            if (exists(f)) DeleteFileA(f);
        }
    }

    printf("E: FILE_FLAG_DELETE_ON_CLOSE handle closed while another is open\n");
    mkfile(f, 0);
    h = CreateFileA(f, DELETE, SHARE_ALL, NULL, OPEN_EXISTING, FILE_FLAG_DELETE_ON_CLOSE, NULL);
    h2 = open_all(f, GENERIC_READ, SHARE_ALL);
    CloseHandle(h);
    printf("  after close: exists %d listed %d\n", exists(f), listed(dir, "a.txt"));
    std_info("other handle", h2);
    CloseHandle(h2);
    printf("  after closing all: exists %d\n", exists(f));

    printf("F: DeleteFile with a byte-range lock held by another handle\n");
    mkfile(f, 0);
    h = open_all(f, GENERIC_READ | GENERIC_WRITE, SHARE_ALL);
    r = LockFile(h, 0, 0, 5, 0);
    r = DeleteFileA(f);
    printf("  lock ok; DeleteFile %d err %lu; exists %d\n", r, r ? 0 : GetLastError(), exists(f));
    CloseHandle(h);
    printf("  after close: exists %d\n", exists(f));
    DeleteFileA(f);

    printf("G: data / image mapping (view mapped, mapping + file handle open)\n");
    {
        static const struct { const char *name; int ex; ULONG flags; } t[] = {
            { "DeleteFile", -1, 0 },
            { "Disposition TRUE", 0, 0 },
            { "Ex DELETE", 1, FILE_DISPOSITION_DELETE },
            { "Ex DELETE|POSIX", 1, FILE_DISPOSITION_DELETE | FILE_DISPOSITION_POSIX_SEMANTICS },
            { "Ex DELETE|POSIX|FORCE_IMAGE", 1, FILE_DISPOSITION_DELETE | FILE_DISPOSITION_POSIX_SEMANTICS | 4 },
        };
        char self[MAX_PATH];
        int i, img;
        GetModuleFileNameA(NULL, self, MAX_PATH);
        for (img = 0; img < 2; img++)
        for (i = 0; i < 5; i++)
        {
            NTSTATUS s = 0;
            if (img) CopyFileA(self, f, FALSE); else mkfile(f, 0);
            h = open_all(f, GENERIC_READ | (img ? GENERIC_EXECUTE : GENERIC_WRITE), SHARE_ALL);
            m = CreateFileMappingA(h, NULL, img ? PAGE_READONLY | SEC_IMAGE : PAGE_READWRITE, 0, 0, NULL);
            v = MapViewOfFile(m, img ? FILE_MAP_READ : FILE_MAP_WRITE, 0, 0, 0);
            if (t[i].ex < 0) { r = DeleteFileA(f); s = r ? 0 : GetLastError(); }
            else
            {
                h2 = open_all(f, DELETE, SHARE_ALL);
                s = t[i].ex ? disp_ex(h2, t[i].flags) : disp(h2, TRUE);
                CloseHandle(h2);
            }
            printf("  %s view %d: %s %#lx; exists %d listed %d\n", img ? "image" : "data", v != NULL, t[i].name, s,
                   exists(f), listed(dir, "a.txt"));
            UnmapViewOfFile(v);
            CloseHandle(m);
            CloseHandle(h);
            printf("    after unmap+close: exists %d\n", exists(f));
            if (exists(f)) DeleteFileA(f);
        }
    }

    printf("H: DeleteFile of a hard link while the file is open through it\n");
    {
        char l[MAX_PATH];
        sprintf(l, "%s\\link.txt", dir);
        mkfile(f, 0);
        CreateHardLinkA(l, f, NULL);
        h = open_all(l, GENERIC_READ, SHARE_ALL);
        r = DeleteFileA(l);
        printf("  DeleteFile(link) %d; link exists %d, target exists %d\n", r, exists(l), exists(f));
        std_info("handle via link", h);
        CloseHandle(h);
        DeleteFileA(f);
    }
}

static void dir_cases(void)
{
    char d[MAX_PATH], f[MAX_PATH]; HANDLE h, h2; BOOL r;
    UNICODE_STRING us; OBJECT_ATTRIBUTES oa; IO_STATUS_BLOCK io; NTSTATUS s;

    sprintf(d, "%s\\sub", dir);
    sprintf(f, "%s\\sub\\f.txt", dir);

    printf("I: RemoveDirectory with a dir handle open (share all)\n");
    CreateDirectoryA(d, NULL);
    h = open_all(d, FILE_LIST_DIRECTORY | FILE_ADD_FILE | DELETE, SHARE_ALL);
    r = RemoveDirectoryA(d);
    printf("  RemoveDirectory %d err %lu; exists %d listed %d\n", r, r ? 0 : GetLastError(), exists(d), listed(dir, "sub"));
    printf("  undelete on dir handle %#lx\n", disp(h, FALSE));
    std_info("dir handle", h);
    RtlInitUnicodeString(&us, L"new.txt");
    InitializeObjectAttributes(&oa, &us, 0, h, NULL);
    s = pNtCreateFile(&h2, GENERIC_WRITE | SYNCHRONIZE, &oa, &io, NULL, 0, 0, FILE_CREATE,
                      FILE_SYNCHRONOUS_IO_NONALERT, NULL, 0);
    printf("  create file relative to deleted dir %#lx\n", s);
    if (!s) CloseHandle(h2);
    r = CreateDirectoryA(d, NULL);
    printf("  CreateDirectory same name %d err %lu\n", r, r ? 0 : GetLastError());
    CloseHandle(h);
    RemoveDirectoryA(d);

    printf("J: RemoveDirectory with a dir handle without FILE_SHARE_DELETE\n");
    CreateDirectoryA(d, NULL);
    h = open_all(d, FILE_LIST_DIRECTORY, FILE_SHARE_READ | FILE_SHARE_WRITE);
    r = RemoveDirectoryA(d);
    printf("  RemoveDirectory %d err %lu; exists %d\n", r, r ? 0 : GetLastError(), exists(d));
    CloseHandle(h);
    RemoveDirectoryA(d);

    printf("K: change notification on the dir / on the parent\n");
    CreateDirectoryA(d, NULL);
    h = FindFirstChangeNotificationA(d, FALSE, FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME);
    h2 = FindFirstChangeNotificationA(dir, FALSE, FILE_NOTIFY_CHANGE_DIR_NAME);
    r = RemoveDirectoryA(d);
    printf("  RemoveDirectory %d err %lu; exists %d\n", r, r ? 0 : GetLastError(), exists(d));
    printf("  wait on dir notif %lu, parent notif %lu\n", WaitForSingleObject(h, 100), WaitForSingleObject(h2, 100));
    r = CreateDirectoryA(d, NULL);
    printf("  CreateDirectory same name %d err %lu\n", r, r ? 0 : GetLastError());
    FindCloseChangeNotification(h);
    FindCloseChangeNotification(h2);
    RemoveDirectoryA(d);

    printf("L: RemoveDirectory of a dir whose only entry is a deleted-but-open file\n");
    CreateDirectoryA(d, NULL);
    mkfile(f, 0);
    h = open_all(f, GENERIC_READ, SHARE_ALL);
    r = DeleteFileA(f);
    r = RemoveDirectoryA(d);
    printf("  RemoveDirectory %d err %lu; exists %d\n", r, r ? 0 : GetLastError(), exists(d));
    CloseHandle(h);
    RemoveDirectoryA(d);

    printf("M: dir info classes, handle 1 set, handle 2 open\n");
    {
        int i;
        for (i = 0; i < 2; i++)
        {
            CreateDirectoryA(d, NULL);
            h = open_all(d, DELETE, SHARE_ALL);
            h2 = open_all(d, FILE_LIST_DIRECTORY, SHARE_ALL);
            s = i ? disp_ex(h, FILE_DISPOSITION_DELETE | FILE_DISPOSITION_POSIX_SEMANTICS) : disp(h, TRUE);
            CloseHandle(h);
            printf("  %s: %#lx; after closing setter exists %d listed %d\n", i ? "Ex DELETE|POSIX" : "Disposition TRUE", s,
                   exists(d), listed(dir, "sub"));
            CloseHandle(h2);
            printf("  after closing all: exists %d\n", exists(d));
            RemoveDirectoryA(d);
        }
    }

    printf("N: read-only directory\n");
    CreateDirectoryA(d, NULL);
    SetFileAttributesA(d, FILE_ATTRIBUTE_READONLY);
    r = RemoveDirectoryA(d);
    printf("  RemoveDirectory %d err %lu; exists %d\n", r, r ? 0 : GetLastError(), exists(d));
    if (exists(d)) { SetFileAttributesA(d, 0); RemoveDirectoryA(d); }

    printf("O: non-empty directory\n");
    CreateDirectoryA(d, NULL);
    mkfile(f, 0);
    r = RemoveDirectoryA(d);
    printf("  RemoveDirectory %d err %lu; exists %d\n", r, r ? 0 : GetLastError(), exists(d));
    DeleteFileA(f);
    RemoveDirectoryA(d);
}

int main(int argc, char **argv)
{
    char tmp[MAX_PATH];
    HMODULE nt = GetModuleHandleA("ntdll.dll");
    pNtQIF = (void *)GetProcAddress(nt, "NtQueryInformationFile");
    pNtSIF = (void *)GetProcAddress(nt, "NtSetInformationFile");
    pNtCreateFile = (void *)GetProcAddress(nt, "NtCreateFile");
    if (argc > 1) strcpy(tmp, argv[1]);  /* base dir with trailing backslash */
    else GetTempPathA(MAX_PATH, tmp);
    sprintf(dir, "%sdelposix", tmp);
    CreateDirectoryA(dir, NULL);
    file_cases();
    dir_cases();
    RemoveDirectoryA(dir);
    return 0;
}
