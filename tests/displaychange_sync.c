/* displaychange_sync.c: is ChangeDisplaySettingsEx synchronous? (issue 104)
 * Main thread changes the mode (WxH from argv, default 800x600) and restores it. Prints, per change:
 * duration, whether WM_DISPLAYCHANGE reached the caller's window, another thread's window and
 * another process's window before the call returned (each handler sleeps 300 ms to show whether
 * the caller waits for it), how it was delivered (InSendMessageEx), and the screen size seen by
 * GetSystemMetrics / EnumDisplaySettings inside the handlers and right after the call.
 * Build: x86_64-w64-mingw32-gcc -O2 -o displaychange_sync.exe displaychange_sync.c -luser32 */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

static LONG seq;           /* bumped by every handler, process local */
static HANDLE ready;
static const char *who = "main";
static DWORD t0;

static void cur(int *w, int *h)
{
    DEVMODEA dm = {.dmSize = sizeof(dm)};
    if (!EnumDisplaySettingsA(NULL, ENUM_CURRENT_SETTINGS, &dm)) dm.dmPelsWidth = dm.dmPelsHeight = 0;
    *w = dm.dmPelsWidth; *h = dm.dmPelsHeight;
}

static LRESULT CALLBACK proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_DISPLAYCHANGE)
    {
        const char *thr = (const char *)GetWindowLongPtrA(hwnd, GWLP_USERDATA);
        DWORD ism = InSendMessageEx(NULL);
        int w, h;
        cur(&w, &h);
        printf("  %s/%s: WM_DISPLAYCHANGE %ux%u bpp %u at tick %lu, ism %#lx (%s), metrics %dx%d, enum %dx%d\n",
               who, thr, LOWORD(lp), HIWORD(lp), (UINT)wp, GetTickCount() % 100000, ism,
               ism & ISMEX_NOTIFY ? "notify" : ism & ISMEX_SEND ? "send" : "posted/none",
               GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN), w, h);
        fflush(stdout);
        Sleep(300);
        InterlockedIncrement(&seq);
        return 0;
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

static HWND mkwin(const char *name)
{
    HWND hwnd = CreateWindowA("dcs", name, WS_OVERLAPPEDWINDOW, 0, 0, 200, 100, NULL, NULL, NULL, NULL);
    SetWindowLongPtrA(hwnd, GWLP_USERDATA, (LONG_PTR)name);
    ShowWindow(hwnd, SW_SHOWNOACTIVATE);
    return hwnd;
}

static DWORD WINAPI thread(void *arg)
{
    MSG msg;
    mkwin("thread");
    SetEvent(ready);
    while (GetMessageA(&msg, NULL, 0, 0)) DispatchMessageA(&msg);
    return 0;
}

static void change(DEVMODEA *dm, const char *what)
{
    LONG before = seq, ret;
    DWORD t;
    int w, h;
    t0 = GetTickCount();
    ret = ChangeDisplaySettingsExA(NULL, dm, NULL, dm ? CDS_FULLSCREEN : 0, NULL);
    t = GetTickCount() - t0;
    cur(&w, &h);
    printf("%s: tick %lu..%lu ret %ld in %lu ms, handlers in this process before return %ld, after: metrics %dx%d enum %dx%d\n",
           what, t0 % 100000, (t0 + t) % 100000, ret, t, seq - before, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN), w, h);
    fflush(stdout);
}

int main(int argc, char **argv)
{
    WNDCLASSA wc = {.lpfnWndProc = proc, .lpszClassName = "dcs", .hInstance = GetModuleHandleA(NULL)};
    DEVMODEA dm = {.dmSize = sizeof(dm), .dmFields = DM_PELSWIDTH | DM_PELSHEIGHT, .dmPelsWidth = 800, .dmPelsHeight = 600};
    PROCESS_INFORMATION pi = {0};
    MSG msg;
    int i;

    RegisterClassA(&wc);
    if (argc > 1 && !strcmp(argv[1], "child"))
    {
        who = "child";
        mkwin("child");
        t0 = GetTickCount();  /* child times are relative to its own start */
        while (GetMessageA(&msg, NULL, 0, 0)) DispatchMessageA(&msg);
        return 0;
    }
    if (argc > 1 && !strcmp(argv[1], "list"))
    {
        DEVMODEA m = {.dmSize = sizeof(m)};
        for (i = 0; EnumDisplaySettingsA(NULL, i, &m); i++)
            printf("%lux%lu %lubpp %luHz\n", m.dmPelsWidth, m.dmPelsHeight, m.dmBitsPerPel, m.dmDisplayFrequency);
        return 0;
    }
    if (argc > 2) { dm.dmPelsWidth = atoi(argv[1]); dm.dmPelsHeight = atoi(argv[2]); }

    {
        STARTUPINFOA si = {.cb = sizeof(si), .dwFlags = STARTF_USESTDHANDLES};
        char cmd[MAX_PATH + 16];
        si.hStdOutput = si.hStdError = GetStdHandle(STD_OUTPUT_HANDLE);
        SetHandleInformation(si.hStdOutput, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
        snprintf(cmd, sizeof(cmd), "\"%s\" child", argv[0]);
        if (!CreateProcessA(NULL, cmd, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi)) printf("no child\n");
        WaitForInputIdle(pi.hProcess, 5000);
    }
    ready = CreateEventA(NULL, FALSE, FALSE, NULL);
    CreateThread(NULL, 0, thread, NULL, 0, NULL);
    WaitForSingleObject(ready, INFINITE);
    mkwin("main");
    while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) DispatchMessageA(&msg);
    Sleep(500);

    for (i = 0; i < 2; i++)
    {
        change(&dm, "set");
        Sleep(1000);
        while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) DispatchMessageA(&msg);
        change(NULL, "restore");
        Sleep(1000);
        while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) DispatchMessageA(&msg);
    }
    TerminateProcess(pi.hProcess, 0);
    return 0;
}
