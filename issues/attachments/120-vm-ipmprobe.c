#include <windows.h>
#include <stdio.h>
#include <string.h>
/* Start invstart, then enumerate top-level windows every 100 ms for 25 s; log every window of the
   Inventor/WebView2/Agent processes (or titled IPM) when first seen and when its visibility/rect changes. */
#define MAXW 512
static struct { HWND h; char cls[128], title[128]; LONG st, ex; RECT r; BOOL vis; DWORD pid; } seen[MAXW];
static int n; static DWORD t0;
static BOOL CALLBACK cb(HWND h, LPARAM l)
{
    char cls[128] = "", title[128] = ""; RECT r; DWORD pid;
    GetClassNameA(h, cls, sizeof cls); GetWindowTextA(h, title, sizeof title);
    GetWindowRect(h, &r); GetWindowThreadProcessId(h, &pid);
    LONG st = GetWindowLongA(h, GWL_STYLE), ex = GetWindowLongA(h, GWL_EXSTYLE); BOOL vis = IsWindowVisible(h);
    char exe[MAX_PATH] = "?";
    HANDLE p = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (p) { DWORD sz = sizeof exe; char full[MAX_PATH]; if (QueryFullProcessImageNameA(p, 0, full, &sz)) strcpy(exe, strrchr(full, '\\') + 1); CloseHandle(p); }
    if (!strstr(exe, "nventor") && !strstr(exe, "Adsk") && !strstr(exe, "msedgewebview2") && !strstr(title, "IPM")) return TRUE;
    int i; for (i = 0; i < n; i++) if (seen[i].h == h) break;
    if (i == n && n < MAXW) { seen[n].h = h; n++; seen[i].vis = 2; }
    if (i < MAXW && (seen[i].vis != vis || memcmp(&seen[i].r, &r, sizeof r) || strcmp(seen[i].title, title) || seen[i].st != st)) {
        printf("t=%5lu %s pid=%lu hwnd=%p cls='%s' title='%s' vis=%d style=%08lx ex=%08lx rect=%ld,%ld %ldx%ld%s\n",
            GetTickCount() - t0, exe, pid, h, cls, title, vis, st, ex, r.left, r.top, r.right - r.left, r.bottom - r.top, seen[i].vis == 2 ? " NEW" : "");
        seen[i].vis = vis; seen[i].r = r; seen[i].st = st; seen[i].ex = ex; strcpy(seen[i].title, title); strcpy(seen[i].cls, cls);
    }
    return TRUE;
}
int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    t0 = GetTickCount();
    system("schtasks /run /tn invstart > nul");
    while (GetTickCount() - t0 < 25000) { EnumWindows(cb, 0); Sleep(100); }
    return 0;
}
