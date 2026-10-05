/* winex11: force the surface flush's fallback (issue 173 / 171 / 062). The main thread alternates a layered window
 * between a faint per-pixel alpha image (all alpha < 16: the driver marks the X window hidden) and a visible one;
 * every flush then wants the window data. Another thread keeps the window data locked most of the time by making
 * the driver recreate the X window of its own layered window (X round trips under the lock), so the flush's
 * trylock fails and it posts WM_X11DRV_SET_HIDDEN with the window surface locked.
 * Build: x86_64-w64-mingw32-gcc -O1 -o flushpost.exe flushpost.c -lgdi32 -luser32
 *   flushpost [SECS]   prints "DONE"; run on the 173 debug build (lock reports, FLUSHPOST lines). */
#include <windows.h>
#include <stdio.h>

static volatile LONG stop, flips, holds;

static void pump(void)
{
    MSG msg;
    while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) DispatchMessageA(&msg);
}

static HBITMAP mkbmp(HDC dc, DWORD pixel)
{
    BITMAPINFO bi = {{sizeof(BITMAPINFOHEADER), 64, 64, 1, 32, BI_RGB}};
    DWORD *bits; int i; HBITMAP bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, (void **)&bits, NULL, 0);
    for (i = 0; i < 64 * 64; i++) bits[i] = pixel;
    return bmp;
}

static DWORD WINAPI hold_proc(void *arg)
{
    BLENDFUNCTION bf = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    POINT pt = {0, 0}; SIZE sz = {64, 64};
    HDC dc = CreateCompatibleDC(0);
    HWND w = CreateWindowExA(WS_EX_LAYERED, "static", "holder", WS_POPUP | WS_VISIBLE, 500, 300, 64, 64, NULL, NULL, NULL, NULL);
    SelectObject(dc, mkbmp(dc, 0x80004000));
    while (!stop)
    {
        UpdateLayeredWindow(w, NULL, NULL, &sz, dc, &pt, 0, &bf, ULW_ALPHA);           /* ARGB visual: new X window */
        SetWindowLongA(w, GWL_EXSTYLE, GetWindowLongA(w, GWL_EXSTYLE) & ~WS_EX_LAYERED); /* default visual: new X window */
        SetWindowLongA(w, GWL_EXSTYLE, GetWindowLongA(w, GWL_EXSTYLE) | WS_EX_LAYERED);
        InterlockedIncrement(&holds);
        pump();
    }
    return 0;
}

int main(int argc, char **argv)
{
    int secs = argc > 1 ? atoi(argv[1]) : 15;
    BLENDFUNCTION bf = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    POINT pt = {0, 0}; SIZE sz = {64, 64}; DWORD t0 = GetTickCount(), last = t0;
    HDC faint = CreateCompatibleDC(0), solid = CreateCompatibleDC(0);
    HWND L = CreateWindowExA(WS_EX_LAYERED, "static", "L", WS_POPUP | WS_VISIBLE, 300, 300, 64, 64, NULL, NULL, NULL, NULL);
    HANDLE th;

    SelectObject(faint, mkbmp(faint, 0x03000000));
    SelectObject(solid, mkbmp(solid, 0x80004000));
    UpdateLayeredWindow(L, NULL, NULL, &sz, solid, &pt, 0, &bf, ULW_ALPHA);
    pump();
    th = CreateThread(NULL, 0, hold_proc, NULL, 0, NULL);
    while (GetTickCount() - t0 < secs * 1000)
    {
        UpdateLayeredWindow(L, NULL, NULL, &sz, (flips & 1) ? solid : faint, &pt, 0, &bf, ULW_ALPHA);
        InterlockedIncrement(&flips);
        pump();
        if (GetTickCount() - last > 1000) { last = GetTickCount(); printf("%6lu flips %ld holds %ld\n", (unsigned long)(last - t0), flips, holds); fflush(stdout); }
    }
    stop = 1;
    WaitForSingleObject(th, 5000);
    printf("DONE flips %ld holds %ld\n", flips, holds); fflush(stdout);
    return 0;
}
