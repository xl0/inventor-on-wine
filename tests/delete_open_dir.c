/* Deleting a directory tree while another handle (FILE_SHARE_DELETE, like a
 * change notification) is open on a subdirectory or a file: does the name
 * vanish at once (POSIX delete semantics), so the parent can be removed? */
#include <windows.h>
#include <stdio.h>

static void mk(const char *base)
{
    char p[MAX_PATH];
    CreateDirectoryA(base, NULL);
    sprintf(p, "%s\\sub", base); CreateDirectoryA(p, NULL);
    sprintf(p, "%s\\sub\\f.txt", base);
    CloseHandle(CreateFileA(p, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL));
}

static void run(const char *name, const char *base, int open_what)
{
    char sub[MAX_PATH], file[MAX_PATH];
    HANDLE h = INVALID_HANDLE_VALUE;
    BOOL r;

    mk(base);
    sprintf(sub, "%s\\sub", base);
    sprintf(file, "%s\\sub\\f.txt", base);
    if (open_what == 1)  /* directory handle, as for ReadDirectoryChangesW */
        h = CreateFileA(sub, FILE_LIST_DIRECTORY, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                        NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    else if (open_what == 2)
        h = FindFirstChangeNotificationA(sub, FALSE, FILE_NOTIFY_CHANGE_FILE_NAME);
    else if (open_what == 3)  /* open file */
        h = CreateFileA(file, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                        NULL, OPEN_EXISTING, 0, NULL);
    printf("%s: handle %s\n", name, h == INVALID_HANDLE_VALUE ? "none" : "open");

    r = DeleteFileA(file);
    printf("  DeleteFile(sub\\f.txt) %d err %lu, exists after %d\n", r, r ? 0 : GetLastError(),
           GetFileAttributesA(file) != INVALID_FILE_ATTRIBUTES);
    r = RemoveDirectoryA(sub);
    printf("  RemoveDirectory(sub) %d err %lu, exists after %d\n", r, r ? 0 : GetLastError(),
           GetFileAttributesA(sub) != INVALID_FILE_ATTRIBUTES);
    r = RemoveDirectoryA(base);
    printf("  RemoveDirectory(base) %d err %lu\n", r, r ? 0 : GetLastError());
    if (open_what == 2) FindCloseChangeNotification(h);
    else if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
    if (!r) printf("  after close: RemoveDirectory(base) %d err %lu\n", RemoveDirectoryA(base), GetLastError());
}

int main(void)
{
    char tmp[MAX_PATH], base[MAX_PATH];
    GetTempPathA(MAX_PATH, tmp);
    sprintf(base, "%sdelprobe0", tmp); run("no handle", base, 0);
    sprintf(base, "%sdelprobe1", tmp); run("dir handle on sub", base, 1);
    sprintf(base, "%sdelprobe2", tmp); run("change notification on sub", base, 2);
    sprintf(base, "%sdelprobe3", tmp); run("open file in sub", base, 3);
    return 0;
}
