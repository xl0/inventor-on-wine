/* win32u window surface list race (issue 171): the owner thread changes the surface of its layered
 * window (resize across the 128 px surface granularity) while another thread calls UpdateLayeredWindow
 * on it; optional extra threads paint + pump (surface flushes, which hold the surface list lock).
 * Build: x86_64-w64-mingw32-gcc -O1 -o ulwrace.exe ulwrace.c -lgdi32 -luser32
 *   ulwrace race [N] [FLUSHERS] [hold]   N owner rounds (default 3000). Last line and exit code:
 *                                        DONE 0, FAULT 3 (a call returned an NTSTATUS), HANG 2.
 *                                        hold: stay alive at the end for a debugger.
 *   ulwrace destroy [N] [FLUSHERS] [hold]   the owner thread creates and destroys N windows while the other
 *                                        thread updates them
 *   ulwrace exit [N] [FLUSHERS] [hold]   N short-lived owner threads leave the window to the thread exit
 *                                        (5th argument "x" in these two modes: the first update of each window
 *                                        comes from the other thread, issue 173)
 *   ulwrace bench [N] [FLUSHERS]         one thread: us per SetWindowPos resize (new surface), per
 *                                        UpdateLayeredWindow and per create + show + destroy
 *   ulwrace basic                        what cross-thread UpdateLayeredWindow / SetWindowPos /
 *                                        SetLayeredWindowAttributes do (results, rects, messages, time)
 */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static HWND L;
static volatile LONG owner_ops, ulw_ops, flush_ops, faults, stop, owner_tid;
static int hold, destroy_mode, first_ulw_other;
static HDC arg2;
static HANDLE ready;

static void fault(const char *what, DWORD ret)
{
    printf("FAULT %s returned %#lx (owner %ld, ulw %ld ops)\n", what, (unsigned long)ret, owner_ops, ulw_ops);
    fflush(stdout);
    InterlockedIncrement(&faults);
}

static volatile LONG log_msgs;
static LRESULT CALLBACK wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (log_msgs && (msg == WM_WINDOWPOSCHANGING || msg == WM_WINDOWPOSCHANGED || msg == WM_MOVE || msg == WM_SIZE ||
                     msg == WM_NCCALCSIZE || msg == WM_GETMINMAXINFO || msg == WM_PAINT || msg == WM_ERASEBKGND))
        printf("    msg %#x in thread %s\n", msg, GetCurrentThreadId() == (DWORD)owner_tid ? "owner" : "OTHER");
    return DefWindowProcA(hwnd, msg, wp, lp);
}

static HWND mk(DWORD style, DWORD ex, int x, int y, int w, int h)
{
    WNDCLASSA wc = {0};
    wc.lpfnWndProc = wndproc; wc.lpszClassName = "ulwrace"; wc.hbrBackground = GetStockObject(GRAY_BRUSH);
    RegisterClassA(&wc);
    return CreateWindowExA(ex, "ulwrace", "ulwrace", style, x, y, w, h, NULL, NULL, NULL, NULL);
}

static void pump(void)
{
    MSG msg;
    while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) DispatchMessageA(&msg);
}

static HDC mem_dc(DWORD colour)
{
    BITMAPINFO bi = {{sizeof(BITMAPINFOHEADER), 512, 512, 1, 32, BI_RGB}};
    DWORD *bits; int i;
    HDC dc = CreateCompatibleDC(0);
    HBITMAP bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, (void **)&bits, NULL, 0);
    for (i = 0; i < 512 * 512; i++) bits[i] = colour;
    SelectObject(dc, bmp);
    return dc;
}

static BOOL ulw(HWND hwnd, HDC dc, const POINT *pos, const SIZE *size)
{
    BLENDFUNCTION bf = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    POINT pt = {0, 0};
    return UpdateLayeredWindow(hwnd, NULL, (POINT *)pos, (SIZE *)size, dc, &pt, 0, &bf, ULW_ALPHA);
}

/* ---- race ---- */

static DWORD WINAPI owner_proc(void *arg)
{
    int i, n = (int)(INT_PTR)arg;
    L = mk(WS_POPUP | WS_VISIBLE, WS_EX_LAYERED | WS_EX_TOOLWINDOW, 100, 100, 100, 100);
    SetEvent(ready);
    for (i = 0; i < n && !faults; i++)
    {
        DWORD ret = SetWindowPos(L, 0, 0, 0, (i & 1) ? 100 : 300, (i & 1) ? 100 : 300,
                                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        if (ret != TRUE) fault("SetWindowPos", ret);
        InterlockedIncrement(&owner_ops);
        if (!(i & 15) && !getenv("ULW_NOPUMP")) pump();
    }
    stop = 1;
    return 0;
}

/* destroy mode: one thread creates and destroys windows */
static DWORD WINAPI destroy_proc(void *arg)
{
    int i, n = (int)(INT_PTR)arg;
    HDC dc = mem_dc(0x80400000);
    for (i = 0; i < n && !faults; i++)
    {
        HWND hwnd = mk(WS_POPUP | WS_VISIBLE, WS_EX_LAYERED | WS_EX_TOOLWINDOW, 100, 100, 100, 100);
        /* first update here: winex11 re-creates the X window for the ARGB visual, which is fatal when another
         * thread does it while this one handles the window's events (a BadWindow death, see issue 173); "x" as 5th argument skips it */
        if (!first_ulw_other) ulw(hwnd, dc, NULL, NULL);
        L = hwnd;
        SetEvent(ready);
        if (i & 2) Sleep(i % 3); else pump();
        DestroyWindow(hwnd);
        InterlockedIncrement(&owner_ops);
    }
    stop = 1;
    return 0;
}

/* exit mode: one window per thread, left to the thread exit */
static DWORD WINAPI short_proc(void *arg)
{
    int i = (int)(INT_PTR)arg;
    HWND hwnd = mk(WS_POPUP | WS_VISIBLE, WS_EX_LAYERED | WS_EX_TOOLWINDOW, 100, 100, 100, 100);
    if (!first_ulw_other) ulw(hwnd, arg2, NULL, NULL);
    L = hwnd;
    SetEvent(ready);
    if (i & 2) Sleep(i % 3); else pump();
    return 0;
}

static DWORD WINAPI spawn_proc(void *arg)
{
    int i, n = (int)(INT_PTR)arg;
    arg2 = mem_dc(0x80400000);
    for (i = 0; i < n && !faults; i++)
    {
        HANDLE th = CreateThread(NULL, 0, short_proc, (void *)(INT_PTR)i, 0, NULL);
        WaitForSingleObject(th, INFINITE);
        CloseHandle(th);
        InterlockedIncrement(&owner_ops);
    }
    stop = 1;
    return 0;
}

static DWORD WINAPI ulw_proc(void *arg)
{
    HDC dc = mem_dc(0x80004000);
    while (!stop && !faults)
    {
        DWORD ret = ulw(L, dc, NULL, NULL);
        if (ret != TRUE && (ret || !destroy_mode)) fault("UpdateLayeredWindow", ret);
        InterlockedIncrement(&ulw_ops);
    }
    return 0;
}

static DWORD WINAPI flush_proc(void *arg)
{
    HWND hwnd = mk(WS_POPUP | WS_VISIBLE, WS_EX_TOOLWINDOW, 300 + 40 * (int)(INT_PTR)arg, 300, 600, 400);
    HBRUSH br[2] = {CreateSolidBrush(RGB(200, 0, 0)), CreateSolidBrush(RGB(0, 0, 200))};
    RECT rc = {0, 0, 600, 400};
    int i;
    for (i = 0; !stop && !faults; i++)
    {
        HDC dc = GetDC(hwnd);
        FillRect(dc, &rc, br[i & 1]);
        ReleaseDC(hwnd, dc);
        pump();     /* idle: flushes every window surface of the process */
        InterlockedIncrement(&flush_ops);
    }
    return 0;
}

static int race(int n, int flushers)
{
    HANDLE th[2]; LONG last = -1; int i, idle = 0;

    ready = CreateEventA(NULL, TRUE, FALSE, NULL);
    th[0] = CreateThread(NULL, 0, destroy_mode == 2 ? spawn_proc : destroy_mode ? destroy_proc : owner_proc,
                         (void *)(INT_PTR)n, 0, NULL);
    WaitForSingleObject(ready, INFINITE);
    th[1] = CreateThread(NULL, 0, ulw_proc, NULL, 0, NULL);
    for (i = 0; i < flushers; i++) CreateThread(NULL, 0, flush_proc, (void *)(INT_PTR)i, 0, NULL);

    /* watchdog: this thread makes no window calls, so it never waits on a win32u lock */
    while (WaitForMultipleObjects(2, th, TRUE, 500) == WAIT_TIMEOUT)
    {
        LONG now = owner_ops + ulw_ops;
        if (now != last) { last = now; idle = 0; continue; }
        if (++idle < 10) continue;
        printf("HANG after owner %ld, ulw %ld, flush %ld ops, %ld faults\n", owner_ops, ulw_ops, flush_ops, faults);
        fflush(stdout);
        if (hold) Sleep(INFINITE);
        TerminateProcess(GetCurrentProcess(), 2);
    }
    if (faults)
    {
        if (hold) Sleep(INFINITE);
        TerminateProcess(GetCurrentProcess(), 3);    /* the surface list lock is leaked: no clean exit */
    }
    printf("DONE owner %ld, ulw %ld, flush %ld ops\n", owner_ops, ulw_ops, flush_ops);
    fflush(stdout);
    if (hold) Sleep(INFINITE);
    TerminateProcess(GetCurrentProcess(), 0);
    return 0;
}

/* ---- bench ---- */

static double now_us(void)
{
    LARGE_INTEGER c, f;
    QueryPerformanceCounter(&c); QueryPerformanceFrequency(&f);
    return c.QuadPart * 1e6 / f.QuadPart;
}

static int bench(int n, int flushers)
{
    HDC dc = mem_dc(0x80004000);
    double t0, t1, t2, t3; int i;

    for (i = 0; i < flushers; i++) CreateThread(NULL, 0, flush_proc, (void *)(INT_PTR)i, 0, NULL);
    L = mk(WS_POPUP | WS_VISIBLE, WS_EX_LAYERED | WS_EX_TOOLWINDOW, 100, 100, 100, 100);
    ulw(L, dc, NULL, NULL);
    pump();
    t0 = now_us();
    for (i = 0; i < n; i++)
        SetWindowPos(L, 0, 0, 0, (i & 1) ? 100 : 300, (i & 1) ? 100 : 300, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    t1 = now_us();
    for (i = 0; i < n; i++) ulw(L, dc, NULL, NULL);
    t2 = now_us();
    for (i = 0; i < n; i++)
    {
        HWND hwnd = mk(WS_POPUP | WS_VISIBLE, WS_EX_TOOLWINDOW, 500, 100, 200, 200);
        DestroyWindow(hwnd);
    }
    t3 = now_us();
    printf("BENCH n %d flushers %d: resize %.1f us, ulw %.1f us, create+destroy %.1f us\n", n, flushers,
           (t1 - t0) / n, (t2 - t1) / n, (t3 - t2) / n);
    TerminateProcess(GetCurrentProcess(), 0);
    return 0;
}

/* ---- basic ---- */

static HWND W[3];
static HANDLE block, blocked;

static DWORD WINAPI release_proc(void *arg)
{
    Sleep(1000);
    SetEvent(block);
    return 0;
}

static DWORD WINAPI basic_owner(void *arg)
{
    MSG msg;
    owner_tid = GetCurrentThreadId();
    W[0] = mk(WS_POPUP | WS_VISIBLE, WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_TOPMOST, 100, 100, 64, 64);
    W[1] = mk(WS_POPUP | WS_VISIBLE, WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_TOPMOST, 300, 100, 64, 64);
    W[2] = mk(WS_POPUP | WS_VISIBLE, WS_EX_TOOLWINDOW | WS_EX_TOPMOST, 500, 100, 64, 64);
    SetEvent(ready);
    while (GetMessageA(&msg, NULL, 0, 0))
    {
        if (msg.message == WM_APP)  /* stop pumping until told */
        {
            SetEvent(blocked);
            WaitForSingleObject(block, INFINITE);
            printf("    owner pumps again\n");
            continue;
        }
        DispatchMessageA(&msg);
    }
    return 0;
}

static void show(const char *what, BOOL ret, DWORD err, DWORD ms, HWND hwnd)
{
    RECT r; HDC screen = GetDC(0); COLORREF c;
    GetWindowRect(hwnd, &r);
    GdiFlush(); Sleep(200);
    c = GetPixel(screen, r.left + 10, r.top + 10);
    ReleaseDC(0, screen);
    printf("%-58s ret %d err %lu, %lu ms, rect (%ld,%ld)-(%ld,%ld), pixel %06lx\n", what, ret, (unsigned long)err,
           (unsigned long)ms, r.left, r.top, r.right, r.bottom, (unsigned long)c);
    fflush(stdout);
}

static int basic(void)
{
    HDC red = mem_dc(0xffff0000), green = mem_dc(0xff00ff00);
    POINT pos; SIZE size; BOOL ret; DWORD err, t; HANDLE th;

    ready = CreateEventA(NULL, TRUE, FALSE, NULL);
    block = CreateEventA(NULL, FALSE, FALSE, NULL);
    blocked = CreateEventA(NULL, FALSE, FALSE, NULL);
    th = CreateThread(NULL, 0, basic_owner, NULL, 0, NULL);
    WaitForSingleObject(ready, INFINITE);
    Sleep(300);
    log_msgs = 1;

#define RUN(what, hwnd, call) do { SetLastError(0xdeadbeef); t = GetTickCount(); ret = (call); err = GetLastError(); \
                                   show(what, ret, err, GetTickCount() - t, hwnd); } while (0)
    printf("owner thread pumping:\n");
    RUN("UpdateLayeredWindow, no size/pos (red)", W[0], ulw(W[0], red, NULL, NULL));
    size.cx = size.cy = 64;
    RUN("UpdateLayeredWindow, same size (red)", W[0], ulw(W[0], red, NULL, &size));
    pos.x = 120; pos.y = 140; size.cx = 200; size.cy = 150;
    RUN("UpdateLayeredWindow, move + resize (120,140 200x150, green)", W[0], ulw(W[0], green, &pos, &size));
    RUN("SetLayeredWindowAttributes(alpha 255)", W[1], SetLayeredWindowAttributes(W[1], 0, 255, LWA_ALPHA));
    RUN("UpdateLayeredWindow after SetLayeredWindowAttributes", W[1], ulw(W[1], red, NULL, NULL));
    RUN("SetWindowPos move + resize (520,140 200x150)", W[2], SetWindowPos(W[2], 0, 520, 140, 200, 150, SWP_NOZORDER | SWP_NOACTIVATE));
    RUN("ShowWindow(SW_HIDE) layered", W[0], ShowWindow(W[0], SW_HIDE));
    RUN("UpdateLayeredWindow, hidden window (red)", W[0], ulw(W[0], red, NULL, &size));
    RUN("ShowWindow(SW_SHOWNA) layered", W[0], ShowWindow(W[0], SW_SHOWNA));

    printf("owner thread not pumping:\n");
    PostThreadMessageA(owner_tid, WM_APP, 0, 0);
    WaitForSingleObject(blocked, 5000);
    pos.x = 140; pos.y = 180; size.cx = 300; size.cy = 100;
    RUN("UpdateLayeredWindow, move + resize (140,180 300x100, red)", W[0], ulw(W[0], red, &pos, &size));
    RUN("UpdateLayeredWindow, same size (green)", W[0], ulw(W[0], green, NULL, &size));
    RUN("SetLayeredWindowAttributes(alpha 128)", W[1], SetLayeredWindowAttributes(W[1], 0, 128, LWA_ALPHA));
    printf("    (owner is released after 1000 ms)\n");
    CreateThread(NULL, 0, release_proc, NULL, 0, NULL);
    RUN("SetWindowPos move + resize (540,180 300x100)", W[2], SetWindowPos(W[2], 0, 540, 180, 300, 100, SWP_NOZORDER | SWP_NOACTIVATE));
    PostThreadMessageA(owner_tid, WM_QUIT, 0, 0);
    WaitForSingleObject(th, 5000);
    printf("DONE\n");
    return 0;
}

int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    if (getenv("ULW_WAIT")) Sleep(atoi(getenv("ULW_WAIT")));   /* time to attach a debugger */
    if (argc > 1 && !strcmp(argv[1], "basic")) return basic();
    if (argc > 1 && !strcmp(argv[1], "bench")) return bench(argc > 2 ? atoi(argv[2]) : 2000, argc > 3 ? atoi(argv[3]) : 0);
    if (argc > 4 && !strcmp(argv[4], "hold")) hold = 1;
    if (argc > 5 && !strcmp(argv[5], "x")) first_ulw_other = 1;
    if (argc > 1 && !strcmp(argv[1], "destroy")) destroy_mode = 1;
    if (argc > 1 && !strcmp(argv[1], "exit")) destroy_mode = 2;
    return race(argc > 2 ? atoi(argv[2]) : 3000, argc > 3 ? atoi(argv[3]) : 0);
}
