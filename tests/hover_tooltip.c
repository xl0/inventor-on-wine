/* hover_tooltip.exe [move] [hook]: does Edge show an HTML title tooltip when its window opens under a
   cursor that never moves (120)? Parks the cursor at 400,300, opens tip.html (one element with
   title="IPM Content" filling the page) in an Edge app window at 100,100 600x400, logs the visible
   windows of Edge processes for 12 s (a tooltip is a small WS_EX_TOPMOST|TRANSPARENT popup).
   "move": after 6 s moves the cursor by 1 px (positive control). "poke": after 6 s shows and moves
   a window elsewhere (Windows posts a WM_MOUSEMOVE at the unchanged position to Edge).
   "slide=MS": MS ms after the Edge window shows, moves it by 50,38 under the still cursor. "hook": install the global
   WH_GETMESSAGE hook of hover_tooltip_hook/hook.dll (copied next to the exe), which logs the mouse
   messages Edge's threads get to hook.txt. Exit 1 = tooltip seen.
   x86_64-w64-mingw32-gcc -O2 -D_WIN32_WINNT=0x0601 -o hover_tooltip.exe hover_tooltip.c */
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static HWND seen[1024], edge_win; static int n_seen, tooltip;
static DWORD t0;

static BOOL is_edge(DWORD pid)
{
    char path[MAX_PATH]; DWORD sz = sizeof path; BOOL ret = FALSE;
    HANDLE p = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!p) return FALSE;
    if (QueryFullProcessImageNameA(p, 0, path, &sz)) ret = !!strstr(path, "msedge.exe");
    CloseHandle(p);
    return ret;
}

static BOOL CALLBACK enum_cb(HWND hwnd, LPARAM lp)
{
    char cls[64], title[64]; RECT r; DWORD pid; LONG style, ex; int i;
    if (!IsWindowVisible(hwnd)) return TRUE;
    GetWindowThreadProcessId(hwnd, &pid);
    if (!is_edge(pid)) return TRUE;
    for (i = 0; i < n_seen; i++) if (seen[i] == hwnd) return TRUE;
    if (n_seen < 1024) seen[n_seen++] = hwnd;
    if (!edge_win) edge_win = hwnd;
    GetClassNameA(hwnd, cls, sizeof cls); GetWindowTextA(hwnd, title, sizeof title);
    GetWindowRect(hwnd, &r);
    style = GetWindowLongA(hwnd, GWL_STYLE); ex = GetWindowLongA(hwnd, GWL_EXSTYLE);
    printf("t=%5lu visible %p '%s' '%s' style %08lx ex %08lx %ld,%ld %ldx%ld\n", GetTickCount() - t0, hwnd, cls,
           title, style, ex, r.left, r.top, r.right - r.left, r.bottom - r.top);
    if ((ex & WS_EX_TOPMOST) && (ex & WS_EX_TRANSPARENT) && r.right - r.left < 300 && r.bottom - r.top < 60)
    {
        printf("  -> tooltip\n");
        tooltip = 1;
    }
    return TRUE;
}

int main(int argc, char **argv)
{
    static const char html[] = "<html><body style='margin:0'><div title='IPM Content' "
        "style='position:fixed;inset:0;background:#048'></div></body></html>";
    char cwd[MAX_PATH], cmd[2 * MAX_PATH + 512];
    STARTUPINFOA si = {sizeof si}; PROCESS_INFORMATION pi;
    BOOL move = FALSE, hook = FALSE, poke = FALSE;
    DWORD slide = 0, shown = 0;
    HWND other = NULL;
    HANDLE job = CreateJobObjectA(NULL, NULL);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION li = {0};
    POINT pt;
    FILE *f;

    setvbuf(stdout, NULL, _IONBF, 0);
    for (int i = 1; i < argc; i++)
    {
        if (!strcmp(argv[i], "move")) move = TRUE;
        if (!strcmp(argv[i], "hook")) hook = TRUE;
        if (!strcmp(argv[i], "poke")) poke = TRUE;
        if (!strncmp(argv[i], "slide=", 6)) slide = atoi(argv[i] + 6);
    }
    if (hook)
    {
        HMODULE dll = LoadLibraryA("hook.dll");
        HOOKPROC (*get_hook)(void) = dll ? (void *)GetProcAddress(dll, "get_hook") : NULL;
        if (!get_hook || !SetWindowsHookExA(WH_GETMESSAGE, get_hook(), dll, 0))
        {
            printf("hook failed %lu\n", GetLastError());
            return 2;
        }
    }
    li.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    SetInformationJobObject(job, JobObjectExtendedLimitInformation, &li, sizeof li);
    GetCurrentDirectoryA(sizeof cwd, cwd);
    f = fopen("tip.html", "w"); fputs(html, f); fclose(f);
    SetCursorPos(400, 300);
    sprintf(cmd, "\"C:\\Program Files (x86)\\Microsoft\\Edge\\Application\\msedge.exe\" --no-first-run "
            "--no-default-browser-check --user-data-dir=\"%s\\ud\" --window-position=100,100 --window-size=600,400 "
            "--app=file:///%s/tip.html", cwd, cwd);
    for (char *c = strstr(cmd, "file:///"); *c; c++) if (*c == '\\') *c = '/';
    t0 = GetTickCount();
    printf("start tick %lu\n", t0);
    if (!CreateProcessA(NULL, cmd, NULL, NULL, FALSE, CREATE_SUSPENDED, NULL, NULL, &si, &pi))
    {
        printf("CreateProcess failed %lu\n", GetLastError());
        return 2;
    }
    AssignProcessToJobObject(job, pi.hProcess);
    ResumeThread(pi.hThread);
    while (GetTickCount() - t0 < 12000)
    {
        if (move && GetTickCount() - t0 > 6000)
        {
            printf("t=%5lu moving cursor by 1 px\n", GetTickCount() - t0);
            SetCursorPos(401, 300);
            move = FALSE;
        }
        if (poke && GetTickCount() - t0 > 6000)
        {
            printf("t=%5lu moving a window elsewhere\n", GetTickCount() - t0);
            other = CreateWindowExA(WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW, "static", "other", WS_POPUP | WS_VISIBLE,
                                    800, 600, 50, 50, NULL, NULL, NULL, NULL);
            SetWindowPos(other, NULL, 810, 600, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
            poke = FALSE;
        }
        EnumWindows(enum_cb, 0);
        if (edge_win && !shown) shown = GetTickCount();
        if (slide && shown && GetTickCount() - shown >= slide)
        {
            printf("t=%5lu moving the Edge window by 50,38\n", GetTickCount() - t0);
            SetWindowPos(edge_win, NULL, 150, 138, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
            slide = 0;
        }
        Sleep(50);
    }
    GetCursorPos(&pt);
    printf("cursor at %ld,%ld; tooltip %s\n", pt.x, pt.y, tooltip ? "seen" : "not seen");
    CloseHandle(job);
    return tooltip;
}
