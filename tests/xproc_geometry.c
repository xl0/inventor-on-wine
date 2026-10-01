/* Geometry of a window tree spanning two processes (091): like Chromium's GPU process, a helper process
 * owns a child window C of our toplevel T and asks about it every frame (GetAncestor GA_ROOT,
 * MapWindowPoints to T, GetWindowRect of T, GetDCEx visible region). We change T and its other
 * children; each answer must follow at once. Then the helper times these calls.
 * Usage: xproc_geometry.exe [ITERS]   (exit 0 = all checks passed)
 * Build: x86_64-w64-mingw32-gcc -O2 -o xproc_geometry.exe xproc_geometry.c -luser32 -lgdi32 */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

#ifndef DCX_USESTYLE
#define DCX_USESTYLE 0x00010000 /* undocumented, what Wine's client surfaces use */
#endif

struct shared
{
    HWND top, child;
    int iters;
    HWND root;                  /* GetAncestor(child, GA_ROOT) */
    POINT offset;               /* MapWindowPoints(child, top) of (0,0) */
    RECT top_rect;              /* GetWindowRect(top) */
    DWORD rgn_size;             /* visible region of GetDCEx(child), screen coordinates */
    char rgn[4096];
    double ns[4];
};

static struct shared *sh;
static HANDLE go, done;

static void pump(HANDLE event)
{
    MSG msg;
    while (MsgWaitForMultipleObjects(1, &event, FALSE, INFINITE, QS_ALLINPUT) != WAIT_OBJECT_0)
        while (PeekMessageA(&msg, 0, 0, 0, PM_REMOVE)) DispatchMessageA(&msg);
}

static void query(void)
{
    HRGN rgn = CreateRectRgn(0, 0, 0, 0);
    HDC hdc = GetDCEx(sh->child, 0, DCX_CACHE | DCX_USESTYLE);
    POINT pt = {0, 0};

    sh->root = GetAncestor(sh->child, GA_ROOT);
    MapWindowPoints(sh->child, sh->top, &pt, 1);
    sh->offset = pt;
    GetWindowRect(sh->top, &sh->top_rect);
    GetRandomRgn(hdc, rgn, SYSRGN);
    sh->rgn_size = GetRegionData(rgn, sizeof(sh->rgn), (RGNDATA *)sh->rgn);
    ReleaseDC(sh->child, hdc);
    DeleteObject(rgn);
}

static double ns_per_call(LARGE_INTEGER t0, int n)
{
    LARGE_INTEGER t1, f;
    QueryPerformanceCounter(&t1);
    QueryPerformanceFrequency(&f);
    return (double)(t1.QuadPart - t0.QuadPart) * 1e9 / f.QuadPart / n;
}

static void bench(void)
{
    LARGE_INTEGER t0;
    POINT pt;
    RECT rect;
    HDC hdc;
    int i, n = sh->iters;

    QueryPerformanceCounter(&t0);
    for (i = 0; i < n; i++) GetAncestor(sh->child, GA_ROOT);
    sh->ns[0] = ns_per_call(t0, n);
    QueryPerformanceCounter(&t0);
    for (i = 0; i < n; i++) { pt.x = pt.y = 0; MapWindowPoints(sh->child, sh->top, &pt, 1); }
    sh->ns[1] = ns_per_call(t0, n);
    QueryPerformanceCounter(&t0);
    for (i = 0; i < n; i++) GetWindowRect(sh->top, &rect);
    sh->ns[2] = ns_per_call(t0, n);
    QueryPerformanceCounter(&t0);
    for (i = 0; i < n; i++) { hdc = GetDCEx(sh->child, 0, DCX_CACHE | DCX_USESTYLE); ReleaseDC(sh->child, hdc); }
    sh->ns[3] = ns_per_call(t0, n);
}

static int helper(void)
{
    sh->child = CreateWindowA("static", "C", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS, 10, 20, 200, 100,
                              sh->top, 0, 0, 0);
    SetEvent(done);
    for (;;)
    {
        pump(go);
        if (!sh->top) break;
        if (sh->iters) bench();
        else query();
        SetEvent(done);
    }
    DestroyWindow(sh->child);
    SetEvent(done);
    return 0;
}

static int failures;

static void step(const char *name, HRGN expect)
{
    HRGN got;
    RECT rect;
    BOOL ok;

    SetEvent(go);
    pump(done);
    GetWindowRect(sh->top, &rect);
    got = ExtCreateRegion(NULL, sh->rgn_size, (RGNDATA *)sh->rgn);
    ok = sh->root == sh->top && sh->offset.x == 10 && sh->offset.y == 20 && EqualRect(&rect, &sh->top_rect)
         && got && EqualRgn(got, expect);
    printf("%-28s %s: root %s, offset %ld,%ld, top rect %s, region box ", name, ok ? "ok" : "FAIL",
           sh->root == sh->top ? "ok" : "wrong", sh->offset.x, sh->offset.y,
           EqualRect(&rect, &sh->top_rect) ? "ok" : "stale");
    if (got) { GetRgnBox(got, &rect); printf("%ld,%ld-%ld,%ld (%lu rects)\n", rect.left, rect.top, rect.right,
                                             rect.bottom, ((RGNDATA *)sh->rgn)->rdh.nCount); }
    else printf("none\n");
    if (!ok) failures++;
    if (got) DeleteObject(got);
    DeleteObject(expect);
}

/* child rect C (10,20)-(210,120) in client coordinates of top, top client origin at screen x,y */
static HRGN child_rgn(int x, int y, int clip_w, int clip_h)
{
    HRGN rgn = CreateRectRgn(x + 10, y + 20, x + min(210, clip_w), y + min(120, clip_h));
    return rgn;
}

int main(int argc, char **argv)
{
    HANDLE map = OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, "xproc_geometry_shm");
    PROCESS_INFORMATION pi;
    STARTUPINFOA si = {sizeof(si)};
    char cmd[MAX_PATH + 16];
    HRGN rgn, tmp;
    HWND sibling;
    int iters = argc > 1 ? atoi(argv[1]) : 100000;

    if (map) /* helper */
    {
        sh = MapViewOfFile(map, FILE_MAP_ALL_ACCESS, 0, 0, 0);
        go = OpenEventA(EVENT_ALL_ACCESS, FALSE, "xproc_geometry_go");
        done = OpenEventA(EVENT_ALL_ACCESS, FALSE, "xproc_geometry_done");
        return helper();
    }

    map = CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, sizeof(*sh), "xproc_geometry_shm");
    sh = MapViewOfFile(map, FILE_MAP_ALL_ACCESS, 0, 0, 0);
    go = CreateEventA(NULL, FALSE, FALSE, "xproc_geometry_go");
    done = CreateEventA(NULL, FALSE, FALSE, "xproc_geometry_done");
    sh->top = CreateWindowA("static", "T", WS_POPUP | WS_VISIBLE, 100, 100, 400, 300, 0, 0, 0, 0);
    UpdateWindow(sh->top);

    GetModuleFileNameA(NULL, cmd, MAX_PATH);
    CreateProcessA(NULL, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi);
    pump(done);

    step("initial", child_rgn(100, 100, 400, 300));
    SetWindowPos(sh->top, 0, 150, 130, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    step("top moved", child_rgn(150, 130, 400, 300));
    SetWindowPos(sh->top, 0, 0, 0, 50, 60, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    step("top shrunk", child_rgn(150, 130, 50, 60));
    SetWindowPos(sh->top, 0, 0, 0, 400, 300, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    step("top restored", child_rgn(150, 130, 400, 300));
    sibling = CreateWindowA("static", "S", WS_CHILD | WS_VISIBLE, 60, 40, 300, 200, sh->top, 0, 0, 0);
    SetWindowPos(sibling, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    rgn = child_rgn(150, 130, 400, 300);
    tmp = CreateRectRgn(150 + 60, 130 + 40, 150 + 360, 130 + 240);
    CombineRgn(rgn, rgn, tmp, RGN_DIFF);
    DeleteObject(tmp);
    step("sibling above child", rgn);
    SetWindowPos(sibling, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    step("sibling below child", child_rgn(150, 130, 400, 300));
    ShowWindow(sh->top, SW_HIDE);
    step("top hidden", CreateRectRgn(0, 0, 0, 0));
    ShowWindow(sh->top, SW_SHOWNOACTIVATE);
    step("top shown", child_rgn(150, 130, 400, 300));

    sh->iters = iters;
    SetEvent(go);
    pump(done);
    printf("helper, ns/call: GetAncestor(GA_ROOT) %.0f, MapWindowPoints %.0f, GetWindowRect(top) %.0f, "
           "GetDCEx+ReleaseDC %.0f\n", sh->ns[0], sh->ns[1], sh->ns[2], sh->ns[3]);

    sh->top = 0;
    SetEvent(go);
    pump(done);
    WaitForSingleObject(pi.hProcess, 5000);
    printf("%d failures\n", failures);
    return failures != 0;
}
