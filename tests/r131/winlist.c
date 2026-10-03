/* All windows of the session (top-level + descendants), hidden ones included, grouped by
 * process / thread liveness / class / parent: counts of what a process leaves behind (131).
 * winlist.exe [IMAGE-SUBSTRING] [raw]   raw: one line per window instead of counts.
 * Build: x86_64-w64-mingw32-gcc -O2 -o winlist.exe winlist.c */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *filter;
static char lines[20000][200];
static int nlines, raw;

static void image(DWORD pid, char *buf, DWORD size)
{
    HANDLE p = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    char path[MAX_PATH] = "?", *s;
    DWORD n = sizeof(path);
    if (p) { QueryFullProcessImageNameA(p, 0, path, &n); CloseHandle(p); }
    s = strrchr(path, '\\');
    snprintf(buf, size, "%s", s ? s + 1 : path);
}

static BOOL CALLBACK win(HWND h, LPARAM top)
{
    char cls[80], pcls[80] = "-", img[64], pimg[64] = "-", thr[16] = "tid?";
    DWORD pid, tid = GetWindowThreadProcessId(h, &pid), ppid = 0;
    HWND parent = GetAncestor(h, GA_PARENT);
    HANDLE t;
    RECT r;

    image(pid, img, sizeof(img));
    if (filter && !strstr(img, filter)) goto done;
    GetClassNameA(h, cls, sizeof(cls));
    GetWindowRect(h, &r);
    if (parent && parent != GetDesktopWindow())
    {
        GetClassNameA(parent, pcls, sizeof(pcls));
        GetWindowThreadProcessId(parent, &ppid);
        image(ppid, pimg, sizeof(pimg));
    }
    if ((t = OpenThread(THREAD_QUERY_LIMITED_INFORMATION, FALSE, tid)))
    {
        DWORD code = 0;
        GetExitCodeThread(t, &code);
        strcpy(thr, code == STILL_ACTIVE ? "live" : "dead");
        CloseHandle(t);
    }
    if (raw) snprintf(lines[nlines++], 200, "%04lx:%04lx %s thread %s %s %ldx%ld %s style %08lx ex %08lx parent %s/%s owner %p",
                      pid, tid, img, thr, cls, r.right - r.left, r.bottom - r.top, IsWindowVisible(h) ? "visible" : "hidden",
                      GetWindowLongA(h, GWL_STYLE), GetWindowLongA(h, GWL_EXSTYLE), pimg, pcls, GetWindow(h, GW_OWNER));
    else snprintf(lines[nlines++], 200, "%04lx %s thread %s %s %s parent %s/%s", pid, img, thr, cls,
                  IsWindowVisible(h) ? "visible" : "hidden", pimg, pcls);
done:
    if (top) EnumChildWindows(h, win, 0);
    return nlines < 19999;
}

int main(int argc, char **argv)
{
    int i, n;
    if (argc > 1 && strcmp(argv[1], "raw")) filter = argv[1];
    raw = !strcmp(argv[argc - 1], "raw");
    EnumWindows(win, 1);
    qsort(lines, nlines, sizeof(lines[0]), (int (*)(const void *, const void *))strcmp);
    for (i = 0; i < nlines; i += n)
    {
        for (n = 1; i + n < nlines && !strcmp(lines[i], lines[i + n]); n++) ;
        printf("%5d %s\n", n, lines[i]);
    }
    return 0;
}
