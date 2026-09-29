/* UpdateLayeredWindow(ULW_ALPHA) popup over an opaque red window (062): per column band a
 * different per-pixel alpha (premultiplied black), then per band: what the screen shows
 * (GDI screen grab), WindowFromPoint, and which window gets a SendInput click.
 *   layered_alpha.exe [secs]   keep the windows up for secs after the checks (screenshots)
 * Build: x86_64-w64-mingw32-gcc -O2 -o layered_alpha.exe layered_alpha.c -lgdi32 */
#include <windows.h>
#include <stdio.h>

static const BYTE alphas[] = {0, 1, 16, 64, 128, 255};
#define NB (sizeof(alphas))
#define BW 40
#define H 120
static HWND back, popup, clicked;

static LRESULT CALLBACK proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_LBUTTONDOWN) clicked = hwnd;
    if (msg == WM_ERASEBKGND)
    {
        RECT r;
        GetClientRect(hwnd, &r);
        FillRect((HDC)wp, &r, (HBRUSH)GetStockObject(DC_BRUSH));  /* DC brush defaults to white */
        SetDCBrushColor((HDC)wp, RGB(255, 0, 0));
        FillRect((HDC)wp, &r, (HBRUSH)GetStockObject(DC_BRUSH));
        return 1;
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
    HDC screen = GetDC(0);
    COLORREF c = GetPixel(screen, x, y);
    ReleaseDC(0, screen);
    return c;
}

static void click(int x, int y)
{
    INPUT in[2] = {{INPUT_MOUSE}, {INPUT_MOUSE}};
    in[0].mi.dx = in[1].mi.dx = x * 65535 / (GetSystemMetrics(SM_CXSCREEN) - 1);
    in[0].mi.dy = in[1].mi.dy = y * 65535 / (GetSystemMetrics(SM_CYSCREEN) - 1);
    in[0].mi.dwFlags = MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_MOVE | MOUSEEVENTF_LEFTDOWN;
    in[1].mi.dwFlags = MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_MOVE | MOUSEEVENTF_LEFTUP;
    clicked = 0;
    SendInput(2, in, sizeof(INPUT));
    pump(300);
}

int main(int argc, char **argv)
{
    WNDCLASSA cls = {0, proc, 0, 0, GetModuleHandleA(0), 0, LoadCursorA(0, (LPCSTR)IDC_ARROW), 0, 0, "la"};
    BITMAPINFO bi = {{sizeof(bi.bmiHeader), BW * NB, -H, 1, 32, BI_RGB}};
    BLENDFUNCTION blend = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    POINT src = {0, 0}, pos = {140, 140};
    SIZE size = {BW * NB, H};
    HDC hdc = CreateCompatibleDC(0);
    DWORD *bits;
    HBITMAP bmp = CreateDIBSection(hdc, &bi, DIB_RGB_COLORS, (void **)&bits, NULL, 0);
    int x, y, i, fails = 0;

    SetProcessDPIAware();
    RegisterClassA(&cls);
    back = CreateWindowExA(WS_EX_TOPMOST, "la", "back", WS_POPUP | WS_VISIBLE, 100, 100, BW * NB + 80, H + 80, 0, 0, 0, 0);
    popup = CreateWindowExA(WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW, "la", "popup", WS_POPUP, 0, 0, 10, 10, 0, 0, 0, 0);
    for (y = 0; y < H; y++)
        for (x = 0; x < BW * NB; x++) bits[y * BW * NB + x] = (DWORD)alphas[x / BW] << 24;  /* premultiplied black */
    SelectObject(hdc, bmp);
    if (!UpdateLayeredWindow(popup, 0, &pos, &size, hdc, &src, 0, &blend, ULW_ALPHA)) printf("ULW failed %lu\n", GetLastError());
    ShowWindow(popup, SW_SHOWNOACTIVATE);
    SetForegroundWindow(back);
    pump(1000);

    for (i = 0; i < NB; i++)
    {
        int px = pos.x + i * BW + BW / 2, py = pos.y + H / 2;
        COLORREF c = screen_pixel(px, py);
        HWND hit = WindowFromPoint((POINT){px, py});
        click(px, py);
        printf("alpha %3u: screen %02x%02x%02x WindowFromPoint %s click %s\n", alphas[i], GetRValue(c), GetGValue(c),
               GetBValue(c), hit == popup ? "popup" : hit == back ? "back" : "other",
               clicked == popup ? "popup" : clicked == back ? "back" : "none");
        /* expected: red blended with black at alpha, the popup hit and clicked unless alpha is 0 */
        if (abs(GetRValue(c) - (255 - alphas[i])) > 8 || GetGValue(c) > 8 || GetBValue(c) > 8) fails++;
        if ((hit == popup) != (alphas[i] != 0) || (clicked == popup) != (alphas[i] != 0)) fails++;
    }
    if (argc > 1) pump(atoi(argv[1]) * 1000);
    printf("%d failures\n", fails);
    return fails != 0;
}
