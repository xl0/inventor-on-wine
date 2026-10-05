/* Owned top-levels of every kind winex11 treats differently, for "owned windows stay with their owner" (175):
 *   owner  WS_OVERLAPPEDWINDOW, green
 *   bar    Inventor's splitter: WS_POPUP|WS_SYSMENU, WS_EX_LAYERED|WS_EX_TOOLWINDOW, per-pixel alpha 3-6 (062)
 *   dlg    owned dialog: WS_POPUP|WS_CAPTION|WS_SYSMENU, WS_EX_DLGMODALFRAME, red
 *   tool   floating panel: WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_THICKFRAME, WS_EX_TOOLWINDOW, yellow
 *   noact  tooltip-like: WS_POPUP, WS_EX_NOACTIVATE|WS_EX_TOPMOST|WS_EX_TOOLWINDOW, magenta (unmanaged in winex11)
 *   plain  WS_POPUP only, shown without activation, orange (unmanaged in winex11)
 *   early  WS_POPUP|WS_SYSMENU shown before its owner, cyan
 *   sub    dialog owned by dlg (owner chain), brown
 *   late   dialog created by the "late" command, purple; xproc: dialog of another process ("xproc" command), blue
 * All have titles "r175 NAME" (find the X windows by name).
 *   owned.exe [SECS]   logs WM_SHOWWINDOW / WM_WINDOWPOSCHANGED / WM_SIZE / WM_ACTIVATE per window, runs commands
 *                      from the file r175.cmd in the current directory (deleted when read), one per line:
 *                      report | min | restore (ShowWindow) | sysmin | sysrestore (WM_SYSCOMMAND) | hide | show |
 *                      close | late (dialog created now) | xproc (child process with an owned dialog) |
 *                      bar (repaint the bar) | quit
 *   owned.exe auto     Windows semantics: owned windows around owner minimize / restore / hide, exit 0 = as expected
 * Build: x86_64-w64-mingw32-gcc -O2 -o owned.exe owned.c -lgdi32 */
#include <windows.h>
#include <stdio.h>

BOOL WINAPI SetProcessDPIAware(void);

enum { OWNER, BAR, DLG, TOOL, NOACT, PLAIN, EARLY, SUB, LATE, XPROC, COUNT };
static const struct { const char *name; DWORD style, ex; COLORREF color; int owner, x, y, w, h; } defs[COUNT] =
{
    { "owner", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, 0, RGB(0, 200, 0), -1, 200, 150, 600, 400 },
    { "bar", WS_POPUP | WS_CLIPSIBLINGS | WS_CLIPCHILDREN | WS_SYSMENU, WS_EX_LAYERED | WS_EX_TOOLWINDOW, 0, OWNER, 400, 200, 5, 300 },
    { "dlg", WS_POPUP | WS_CAPTION | WS_SYSMENU, WS_EX_DLGMODALFRAME, RGB(255, 0, 0), OWNER, 250, 250, 120, 100 },
    { "tool", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME, WS_EX_TOOLWINDOW, RGB(255, 255, 0), OWNER, 450, 250, 120, 100 },
    { "noact", WS_POPUP, WS_EX_NOACTIVATE | WS_EX_TOPMOST | WS_EX_TOOLWINDOW, RGB(255, 0, 255), OWNER, 250, 400, 80, 40 },
    { "plain", WS_POPUP, 0, RGB(255, 128, 0), OWNER, 350, 400, 80, 40 },
    { "early", WS_POPUP | WS_SYSMENU, 0, RGB(0, 255, 255), OWNER, 450, 400, 80, 40 },
    { "sub", WS_POPUP | WS_CAPTION | WS_SYSMENU, WS_EX_DLGMODALFRAME, RGB(128, 64, 0), DLG, 600, 400, 120, 100 },
    { "late", WS_POPUP | WS_CAPTION | WS_SYSMENU, WS_EX_DLGMODALFRAME, RGB(128, 0, 255), OWNER, 600, 200, 120, 100 },
    { "xproc", WS_POPUP | WS_CAPTION | WS_SYSMENU, WS_EX_DLGMODALFRAME, RGB(0, 128, 255), OWNER, 650, 300, 120, 100 },
};
static HWND wnd[COUNT];
static DWORD start;
static int quiet;

static int idx(HWND hwnd) { int i; for (i = 0; i < COUNT; i++) if (wnd[i] == hwnd) return i; return -1; }

static LRESULT CALLBACK proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    int i = idx(hwnd);
    const char *name = i < 0 ? "?" : defs[i].name;
    DWORD t = GetTickCount() - start;
    switch (msg)
    {
    case WM_ERASEBKGND:
        if (i >= 0)
        {
            RECT rc;
            GetClientRect(hwnd, &rc);
            SetDCBrushColor((HDC)wp, defs[i].color);
            FillRect((HDC)wp, &rc, GetStockObject(DC_BRUSH));
            return 1;
        }
        break;
    case WM_SHOWWINDOW:
        if (!quiet) printf("%6lu %-5s WM_SHOWWINDOW %d status %ld\n", t, name, (int)wp, (long)lp);
        break;
    case WM_WINDOWPOSCHANGED:
    {
        WINDOWPOS *pos = (WINDOWPOS *)lp;
        if (!quiet && (pos->flags & (SWP_SHOWWINDOW | SWP_HIDEWINDOW | 0x8000 /* SWP_STATECHANGED */)))
            printf("%6lu %-5s WM_WINDOWPOSCHANGED flags %04x %d,%d %dx%d\n", t, name, pos->flags, pos->x, pos->y, pos->cx, pos->cy);
        break;
    }
    case WM_SIZE:
        if (!quiet && wp != SIZE_RESTORED) printf("%6lu %-5s WM_SIZE type %d\n", t, name, (int)wp);
        break;
    case WM_ACTIVATE:
        if (!quiet) printf("%6lu %-5s WM_ACTIVATE %d\n", t, name, (int)LOWORD(wp));
        break;
    case WM_CLOSE:
        if (!quiet) printf("%6lu %-5s WM_CLOSE\n", t, name);
        break;
    case WM_DESTROY:
        if (i == OWNER) PostQuitMessage(0);
        break;
    }
    fflush(stdout);
    return DefWindowProcA(hwnd, msg, wp, lp);
}

static void pump(int ms)
{
    DWORD end = GetTickCount() + ms;
    MSG msg;
    while ((int)(end - GetTickCount()) > 0)
    {
        MsgWaitForMultipleObjects(0, NULL, FALSE, 10, QS_ALLINPUT);
        while (PeekMessageA(&msg, 0, 0, 0, PM_REMOVE))
        {
            if (msg.message == WM_QUIT) { printf("quit\n"); fflush(stdout); ExitProcess(0); }
            DispatchMessageA(&msg);
        }
    }
}

static void create(int i)
{
    char title[32];
    sprintf(title, "r175 %s", defs[i].name);
    wnd[i] = CreateWindowExA(defs[i].ex, "r175", title, defs[i].style, defs[i].x, defs[i].y, defs[i].w, defs[i].h,
                             defs[i].owner < 0 ? 0 : wnd[defs[i].owner], 0, 0, 0);
}

static void paint_bar(void)
{
    BLENDFUNCTION blend = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    POINT pos = {defs[BAR].x, defs[BAR].y}, src = {0, 0};
    SIZE size = {defs[BAR].w, defs[BAR].h};
    BITMAPINFO bi = {{sizeof(bi.bmiHeader), size.cx, -size.cy, 1, 32, BI_RGB}};
    HDC hdc = CreateCompatibleDC(0);
    DWORD *bits;
    HBITMAP bmp = CreateDIBSection(hdc, &bi, DIB_RGB_COLORS, (void **)&bits, NULL, 0);
    RECT rc;
    int i;

    GetWindowRect(wnd[OWNER], &rc);  /* keep it at the same place relative to the owner */
    pos.x = rc.left + defs[BAR].x - defs[OWNER].x; pos.y = rc.top + defs[BAR].y - defs[OWNER].y;
    for (i = 0; i < size.cx * size.cy; i++) bits[i] = (i % size.cx) & 1 ? 0x06030303 : 0x03030303;
    SelectObject(hdc, bmp);
    if (!UpdateLayeredWindow(wnd[BAR], 0, &pos, &size, hdc, &src, 0, &blend, ULW_ALPHA)) printf("ULW failed %lu\n", GetLastError());
    DeleteDC(hdc);
    DeleteObject(bmp);
}

static void report(const char *what)
{
    RECT rc;
    int i;
    printf("report %s: active %s foreground %s\n", what, idx(GetActiveWindow()) < 0 ? "-" : defs[idx(GetActiveWindow())].name,
           idx(GetForegroundWindow()) < 0 ? "-" : defs[idx(GetForegroundWindow())].name);
    for (i = 0; i < COUNT; i++)
    {
        if (!wnd[i]) continue;
        GetWindowRect(wnd[i], &rc);
        printf("  %-5s %p visible %d iconic %d rect %ld,%ld %ldx%ld\n", defs[i].name, wnd[i], IsWindowVisible(wnd[i]), IsIconic(wnd[i]),
               rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top);
    }
    fflush(stdout);
}

static int check(const char *what, int owner_visible, int owner_iconic, int owned_visible)
{
    int i, fails = 0;
    if (IsWindowVisible(wnd[OWNER]) != owner_visible || IsIconic(wnd[OWNER]) != owner_iconic)
    { printf("FAIL %s: owner visible %d iconic %d\n", what, IsWindowVisible(wnd[OWNER]), IsIconic(wnd[OWNER])); fails++; }
    for (i = OWNER + 1; i < COUNT; i++)
        if (wnd[i] && IsWindowVisible(wnd[i]) != owned_visible)
        { printf("FAIL %s: %s visible %d\n", what, defs[i].name, IsWindowVisible(wnd[i])); fails++; }
    report(what);
    return fails;
}

static void command(const char *cmd)
{
    int i;
    printf("%6lu command %s\n", GetTickCount() - start, cmd); fflush(stdout);
    if (!strcmp(cmd, "report")) report("cmd");
    else if (!strcmp(cmd, "min")) ShowWindow(wnd[OWNER], SW_MINIMIZE);
    else if (!strcmp(cmd, "restore")) ShowWindow(wnd[OWNER], SW_RESTORE);
    else if (!strcmp(cmd, "sysmin")) PostMessageA(wnd[OWNER], WM_SYSCOMMAND, SC_MINIMIZE, 0);
    else if (!strcmp(cmd, "sysrestore")) PostMessageA(wnd[OWNER], WM_SYSCOMMAND, SC_RESTORE, 0);
    else if (!strcmp(cmd, "hide")) ShowWindow(wnd[OWNER], SW_HIDE);
    else if (!strcmp(cmd, "show")) ShowWindow(wnd[OWNER], SW_SHOW);
    else if (!strcmp(cmd, "close")) PostMessageA(wnd[OWNER], WM_CLOSE, 0, 0);
    else if (!strcmp(cmd, "late")) { create(LATE); ShowWindow(wnd[LATE], SW_SHOW); }
    else if (!strcmp(cmd, "xproc"))
    {
        char path[MAX_PATH], line[MAX_PATH + 64];
        STARTUPINFOA si = {sizeof(si)};
        PROCESS_INFORMATION pi;
        GetModuleFileNameA(0, path, sizeof(path));
        sprintf(line, "\"%s\" child %p", path, wnd[OWNER]);
        if (!CreateProcessA(NULL, line, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi)) printf("CreateProcess failed %lu\n", GetLastError());
    }
    else if (!strcmp(cmd, "bar")) paint_bar();
    else if (!strcmp(cmd, "quit")) { for (i = COUNT - 1; i >= 0; i--) if (wnd[i]) DestroyWindow(wnd[i]); }
}

int main(int argc, char **argv)
{
    WNDCLASSA cls = {0, proc, 0, 0, GetModuleHandleA(0), 0, LoadCursorA(0, (LPCSTR)IDC_ARROW), 0, 0, "r175"};
    int i, secs = argc > 1 ? atoi(argv[1]) : 30, fails = 0, autom = argc > 1 && !strcmp(argv[1], "auto");

    SetProcessDPIAware();
    start = GetTickCount();
    RegisterClassA(&cls);
    if (argc > 2 && !strcmp(argv[1], "child"))  /* a dialog owned by the window of another process */
    {
        wnd[OWNER] = (HWND)(ULONG_PTR)strtoull(argv[2], NULL, 16);
        create(XPROC);
        wnd[OWNER] = 0;
        ShowWindow(wnd[XPROC], SW_SHOW);
        while (IsWindow(GetWindow(wnd[XPROC], GW_OWNER))) pump(200);
        return 0;
    }
    for (i = 0; i < LATE; i++) create(i);
    ShowWindow(wnd[EARLY], SW_SHOWNOACTIVATE);
    pump(500);
    ShowWindow(wnd[OWNER], SW_SHOW);
    pump(1000);  /* the WM may place the window */
    paint_bar();
    ShowWindow(wnd[BAR], SW_SHOWNOACTIVATE);
    ShowWindow(wnd[TOOL], SW_SHOWNOACTIVATE);
    ShowWindow(wnd[NOACT], SW_SHOWNOACTIVATE);
    ShowWindow(wnd[PLAIN], SW_SHOWNOACTIVATE);
    ShowWindow(wnd[DLG], SW_SHOW);
    ShowWindow(wnd[SUB], SW_SHOW);
    pump(1500);
    report("start");

    if (autom)
    {
        quiet = 0;
        fails += check("shown", 1, 0, 1);
        ShowWindow(wnd[OWNER], SW_MINIMIZE); pump(1000);
        fails += check("owner minimized", 1, 1, 0);
        ShowWindow(wnd[OWNER], SW_RESTORE); pump(1000);
        fails += check("owner restored", 1, 0, 1);
        SendMessageA(wnd[OWNER], WM_SYSCOMMAND, SC_MINIMIZE, 0); pump(1000);
        fails += check("owner minimized (SC_MINIMIZE)", 1, 1, 0);
        SendMessageA(wnd[OWNER], WM_SYSCOMMAND, SC_RESTORE, 0); pump(1000);
        fails += check("owner restored (SC_RESTORE)", 1, 0, 1);
        ShowWindow(wnd[OWNER], SW_HIDE); pump(1000);
        fails += check("owner hidden (owned windows stay)", 0, 0, 1);
        ShowWindow(wnd[OWNER], SW_SHOW); pump(1000);
        fails += check("owner shown", 1, 0, 1);
        printf("%d failures\n", fails);
        return fails != 0;
    }
    for (i = 0; i < secs * 5; i++)
    {
        FILE *f;
        char line[64], cmds[16][64];
        int n = 0, j;
        pump(200);
        if (!(f = fopen("r175.cmd", "r"))) continue;
        while (n < 16 && fgets(line, sizeof(line), f)) { line[strcspn(line, "\r\n")] = 0; if (line[0]) strcpy(cmds[n++], line); }
        fclose(f);
        DeleteFileA("r175.cmd");
        for (j = 0; j < n; j++) { command(cmds[j]); pump(100); }
    }
    report("end");
    return 0;
}
