/* WPF-free stand-in for Inventor's pane splitter (062): a two-pane window (left green, right blue)
 * with an owned 5 px wide WS_POPUP|WS_SYSMENU, WS_EX_LAYERED|WS_EX_TOOLWINDOW popup over the pane
 * border, UpdateLayeredWindow(ULW_ALPHA) with alpha 3-6 (top 40 rows alpha 0). Dragging the popup
 * moves the border; like Inventor the popup itself only follows when a drag or window move ends.
 * A second popup mimics a WPF tooltip: opaque body, 8 px black shadow fading from alpha 113 to 0.
 *   layered_splitter.exe [SECS]   log to stdout: popup rects, then per event "down/up/split/moved"
 *                                 and once a second what the screen shows (GDI screen grab);
 *                                 drive it with real input (xdotool)
 *   layered_splitter.exe auto     drive it with SendInput and check (the VM; on Wine SendInput
 *                                 doesn't go through the X server), exit 0 = ok
 * Build: x86_64-w64-mingw32-gcc -O2 -o layered_splitter.exe layered_splitter.c -lgdi32 */
#include <windows.h>
#include <stdio.h>

BOOL WINAPI SetProcessDPIAware(void);

#define BAR 5
#define HOLE 40
#define TIP_W 160
#define TIP_H 60
#define SHADOW 8
static HWND main_hwnd, bar, tip;
static int split = 200, dragging, bar_down, bar_up, bar_move, main_down;

static void place_bar(void)
{
    BLENDFUNCTION blend = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    RECT rc;
    POINT pos = {0, 0}, src = {0, 0};
    SIZE size;
    BITMAPINFO bi = {{sizeof(bi.bmiHeader)}};
    HDC hdc = CreateCompatibleDC(0);
    HBITMAP bmp;
    DWORD *bits;
    int i;

    GetClientRect(main_hwnd, &rc);
    pos.x = split - BAR;
    ClientToScreen(main_hwnd, &pos);
    size.cx = BAR; size.cy = rc.bottom;
    bi.bmiHeader.biWidth = size.cx; bi.bmiHeader.biHeight = -size.cy;
    bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32;
    bmp = CreateDIBSection(hdc, &bi, DIB_RGB_COLORS, (void **)&bits, NULL, 0);
    for (i = 0; i < size.cx * size.cy; i++)  /* what WPF draws: premultiplied 03030303 / 06030303 */
        bits[i] = i / size.cx < HOLE ? 0 : (i % size.cx) & 1 ? 0x06030303 : 0x03030303;
    SelectObject(hdc, bmp);
    if (!UpdateLayeredWindow(bar, 0, &pos, &size, hdc, &src, 0, &blend, ULW_ALPHA)) printf("ULW failed %lu\n", GetLastError());
    DeleteDC(hdc);
    DeleteObject(bmp);
}

static void place_tip(void)
{
    BLENDFUNCTION blend = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    POINT pos = {split + 60, 100}, src = {0, 0};
    SIZE size = {TIP_W + 2 * SHADOW, TIP_H + 2 * SHADOW};
    BITMAPINFO bi = {{sizeof(bi.bmiHeader), size.cx, -size.cy, 1, 32, BI_RGB}};
    HDC hdc = CreateCompatibleDC(0);
    DWORD *bits;
    HBITMAP bmp = CreateDIBSection(hdc, &bi, DIB_RGB_COLORS, (void **)&bits, NULL, 0);
    int x, y;

    ClientToScreen(main_hwnd, &pos);
    for (y = 0; y < size.cy; y++)
        for (x = 0; x < size.cx; x++)
        {
            int d = min(min(x, size.cx - 1 - x), min(y, size.cy - 1 - y));  /* distance from the edge */
            bits[y * size.cx + x] = d >= SHADOW ? 0xfff0f0f0 : (DWORD)(113 * d / SHADOW) << 24;
        }
    SelectObject(hdc, bmp);
    if (!UpdateLayeredWindow(tip, 0, &pos, &size, hdc, &src, 0, &blend, ULW_ALPHA)) printf("ULW failed %lu\n", GetLastError());
    DeleteDC(hdc);
    DeleteObject(bmp);
}

static LRESULT CALLBACK proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    POINT pt = {(short)LOWORD(lp), (short)HIWORD(lp)};
    switch (msg)
    {
    case WM_PAINT:
        if (hwnd == main_hwnd)
        {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            RECT rc;
            GetClientRect(hwnd, &rc);
            rc.right = split;
            SetDCBrushColor(hdc, RGB(0, 200, 0));
            FillRect(hdc, &rc, GetStockObject(DC_BRUSH));
            GetClientRect(hwnd, &rc);
            rc.left = split;
            SetDCBrushColor(hdc, RGB(0, 0, 200));
            FillRect(hdc, &rc, GetStockObject(DC_BRUSH));
            EndPaint(hwnd, &ps);
            return 0;
        }
        break;
    case WM_LBUTTONDOWN:
        if (hwnd == bar) { bar_down++; dragging = 1; SetCapture(hwnd); printf("bar down %ld,%ld\n", pt.x, pt.y); }
        else if (hwnd == main_hwnd) { main_down++; printf("main down %ld,%ld\n", pt.x, pt.y); }
        else printf("tip down %ld,%ld\n", pt.x, pt.y);
        fflush(stdout);
        break;
    case WM_MOUSEMOVE:
        if (hwnd == bar) bar_move++;
        if (hwnd == bar && dragging)
        {
            ClientToScreen(hwnd, &pt);
            ScreenToClient(main_hwnd, &pt);
            split = pt.x;
            InvalidateRect(main_hwnd, NULL, FALSE);
            UpdateWindow(main_hwnd);
        }
        break;
    case WM_LBUTTONUP:
        if (hwnd == bar && dragging)
        {
            dragging = 0; bar_up++;
            ReleaseCapture();
            place_bar();
            printf("bar up, split %d\n", split);
            fflush(stdout);
        }
        break;
    case WM_RBUTTONDOWN:  /* hide and show the bar again */
        if (hwnd != main_hwnd) break;
        ShowWindow(bar, SW_HIDE);
        Sleep(300);
        ShowWindow(bar, SW_SHOWNOACTIVATE);
        printf("bar shown again\n"); fflush(stdout);
        break;
    case WM_SIZE:
        if (hwnd == main_hwnd && IsWindowVisible(bar)) { place_bar(); printf("resized\n"); fflush(stdout); }
        break;
    case WM_EXITSIZEMOVE:
        if (hwnd == main_hwnd) { place_bar(); place_tip(); printf("moved\n"); fflush(stdout); }
        break;
    case WM_SETCURSOR:
        if (hwnd == bar) { SetCursor(LoadCursorA(0, (LPCSTR)IDC_SIZEWE)); return TRUE; }
        break;
    case WM_DESTROY:
        if (hwnd == main_hwnd) PostQuitMessage(0);
        break;
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

static void pump(int ms)
{
    DWORD end = GetTickCount() + ms;
    MSG msg;
    while ((int)(end - GetTickCount()) > 0)
    {
        MsgWaitForMultipleObjects(0, NULL, FALSE, 10, QS_ALLINPUT);
        while (PeekMessageA(&msg, 0, 0, 0, PM_REMOVE)) DispatchMessageA(&msg);
    }
}

static COLORREF screen_pixel(int x, int y)
{
    HDC screen = GetDC(0), mem = CreateCompatibleDC(screen);
    HBITMAP bmp = CreateCompatibleBitmap(screen, 1, 1);
    COLORREF c;
    SelectObject(mem, bmp);
    BitBlt(mem, 0, 0, 1, 1, screen, x, y, SRCCOPY | CAPTUREBLT);
    c = GetPixel(mem, 0, 0);
    DeleteDC(mem);
    DeleteObject(bmp);
    ReleaseDC(0, screen);
    return RGB(GetBValue(c), GetGValue(c), GetRValue(c));  /* print as rrggbb */
}

static void mouse(DWORD flags, int x, int y)
{
    INPUT in = {INPUT_MOUSE};
    in.mi.dx = x * 65535 / (GetSystemMetrics(SM_CXSCREEN) - 1);
    in.mi.dy = y * 65535 / (GetSystemMetrics(SM_CYSCREEN) - 1);
    in.mi.dwFlags = flags | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_MOVE;
    SendInput(1, &in, sizeof(in));
    pump(50);
}

/* screen colours on the bar (below the alpha 0 rows), in the tooltip's shadow and body, and the bar's place */
static void report(void)
{
    RECT br, tr;
    GetWindowRect(bar, &br);
    GetWindowRect(tip, &tr);
    printf("bar %ld,%ld %ldx%ld screen %06lx hole %06lx | tip %ld,%ld shadow %06lx body %06lx | split %d\n", br.left, br.top,
           br.right - br.left, br.bottom - br.top, screen_pixel(br.left + 2, br.top + HOLE + 60), screen_pixel(br.left + 2, br.top + 10),
           tr.left, tr.top, screen_pixel(tr.left + SHADOW / 2, tr.top + 40), screen_pixel(tr.left + 40, tr.top + 40), split);
    fflush(stdout);
}

int main(int argc, char **argv)
{
    WNDCLASSA cls = {0, proc, 0, 0, GetModuleHandleA(0), 0, LoadCursorA(0, (LPCSTR)IDC_ARROW), 0, 0, "ls"};
    int i, secs = argc > 1 ? atoi(argv[1]) : 0, fails = 0;
    RECT br, tr;

    SetProcessDPIAware();
    RegisterClassA(&cls);
    main_hwnd = CreateWindowExA(0, "ls", "layered splitter", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, 200, 150, 600, 400, 0, 0, 0, 0);
    bar = CreateWindowExA(WS_EX_LAYERED | WS_EX_TOOLWINDOW, "ls", "", WS_POPUP | WS_CLIPSIBLINGS | WS_CLIPCHILDREN | WS_SYSMENU,
                          0, 0, 0, 0, main_hwnd, 0, 0, 0);
    tip = CreateWindowExA(WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE, "ls", "", WS_POPUP | WS_CLIPSIBLINGS,
                          0, 0, 0, 0, main_hwnd, 0, 0, 0);
    ShowWindow(main_hwnd, SW_SHOW);
    pump(1000);  /* the WM may place the window */
    place_bar();
    place_tip();
    ShowWindow(bar, SW_SHOWNOACTIVATE);
    ShowWindow(tip, SW_SHOWNOACTIVATE);
    pump(1000);
    place_bar();  /* awesome's placement rule moves new managed windows to free screen space */
    pump(500);
    report();

    if (argc > 1 && !strcmp(argv[1], "auto"))
    {
        COLORREF c;
        int x, y, old = split;
        GetWindowRect(bar, &br);
        GetWindowRect(tip, &tr);
        x = br.left + 2; y = br.top + HOLE + 60;
        c = screen_pixel(x, y);  /* green, at most 2% darker */
        if ((c >> 16) > 8 || ((c >> 8) & 0xff) < 192 || (c & 0xff) > 8) { printf("FAIL: bar visible, %06lx\n", c); fails++; }
        if (WindowFromPoint((POINT){x, y}) != bar) { printf("FAIL: WindowFromPoint on the bar\n"); fails++; }
        if (WindowFromPoint((POINT){x, br.top + 10}) != main_hwnd) { printf("FAIL: WindowFromPoint on its alpha 0 part\n"); fails++; }
        if (WindowFromPoint((POINT){tr.left + SHADOW / 2, tr.top + 40}) != tip) { printf("FAIL: WindowFromPoint on the shadow\n"); fails++; }
        mouse(0, x, y); pump(200);
        mouse(MOUSEEVENTF_LEFTDOWN, x, y);
        for (i = 1; i <= 6; i++) mouse(0, x + 10 * i, y);
        mouse(MOUSEEVENTF_LEFTUP, x + 60, y); pump(300);
        if (bar_down != 1 || bar_up != 1 || abs(split - old - 60) > 4) { printf("FAIL: drag: down %d up %d split %d -> %d\n", bar_down, bar_up, old, split); fails++; }
        report();
        GetWindowRect(bar, &br);
        mouse(0, br.left + 2, br.top + 10); mouse(MOUSEEVENTF_LEFTDOWN, br.left + 2, br.top + 10); mouse(MOUSEEVENTF_LEFTUP, br.left + 2, br.top + 10);
        pump(300);
        if (main_down != 1 || bar_down != 1) { printf("FAIL: click on the alpha 0 part: main %d bar %d\n", main_down, bar_down); fails++; }
        c = screen_pixel(tr.left + SHADOW / 2, tr.top + 40);  /* over the blue pane (left of the old place: now green?) */
        if (c == 0) { printf("FAIL: shadow drawn black\n"); fails++; }
        printf("%d failures\n", fails);
        return fails != 0;
    }
    for (i = 0; i < secs; i++) { pump(1000); report(); }
    printf("bar down %d up %d moves %d, main down %d\n", bar_down, bar_up, bar_move, main_down);
    return 0;
}
