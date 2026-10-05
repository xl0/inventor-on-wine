/* winex11: X window recreated by another thread (issue 173, BadWindow). A layered window changes between
 * a per-pixel alpha surface (UpdateLayeredWindow: ARGB visual) and constant alpha (SetLayeredWindowAttributes:
 * default visual) from a thread that doesn't own it; the driver destroys and recreates the X window in the
 * calling thread while the owner thread handles PropertyNotify events of the old one.
 * Build: x86_64-w64-mingw32-gcc -O1 -o visual_race.exe visual_race.c -lgdi32 -luser32
 *   visual_race [SECS] [text]   prints "DONE" at the end; the unfixed driver dies of an X BadWindow error
 *                               (X_GetProperty in the owner's PropertyNotify handlers).
 * text: a third thread sets the window text with DefWindowProc(WM_SETTEXT), i.e. the driver's SetWindowText runs in
 * a thread that doesn't own the window (X_ChangeProperty on the destroyed window). */
#include <windows.h>
#include <stdio.h>

static HWND L;
static volatile LONG stop, flips;

static DWORD WINAPI owner_proc(void *arg)
{
    MSG msg; int i;
    L = CreateWindowExA(WS_EX_LAYERED, "static", "L layered", WS_POPUP | WS_VISIBLE, 300, 300, 64, 64, NULL, NULL, NULL, NULL);
    for (i = 0; !stop; i++)
    {
        while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) DispatchMessageA(&msg);
        /* property changes on the X window: the owner gets PropertyNotify events and reads the properties back */
        SetWindowPos(L, 0, 300 + i % 50, 300, 64 + i % 2, 64, SWP_NOZORDER | SWP_NOACTIVATE);
    }
    return 0;
}

static DWORD WINAPI text_proc(void *arg)
{
    MSG msg; int i; char text[32];
    for (i = 0; !stop; i++)
    {
        sprintf(text, "L %d", i);
        DefWindowProcA(L, WM_SETTEXT, 0, (LPARAM)text);
        if (!(i % 16)) while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) DispatchMessageA(&msg);
    }
    return 0;
}

int main(int argc, char **argv)
{
    int secs = argc > 1 ? atoi(argv[1]) : 20, i;
    BITMAPINFO bi = {{sizeof(BITMAPINFOHEADER), 64, 64, 1, 32, BI_RGB}};
    BLENDFUNCTION bf = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    POINT pt = {0, 0}; SIZE sz = {64, 64}; DWORD *bits, t0 = GetTickCount(), last = t0;
    HDC dc = CreateCompatibleDC(0); HBITMAP bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, (void **)&bits, NULL, 0);
    HANDLE th = CreateThread(NULL, 0, owner_proc, NULL, 0, NULL);

    for (i = 0; i < 64 * 64; i++) bits[i] = 0x80004000;
    SelectObject(dc, bmp);
    while (!L) Sleep(1);
    if (argc > 2) CreateThread(NULL, 0, text_proc, NULL, 0, NULL);
    while (GetTickCount() - t0 < secs * 1000)
    {
        /* resetting WS_EX_LAYERED lets the window change between the two kinds */
        if (UpdateLayeredWindow(L, NULL, NULL, &sz, dc, &pt, 0, &bf, ULW_ALPHA)) InterlockedIncrement(&flips);
        SetWindowLongA(L, GWL_EXSTYLE, GetWindowLongA(L, GWL_EXSTYLE) & ~WS_EX_LAYERED);
        SetWindowLongA(L, GWL_EXSTYLE, GetWindowLongA(L, GWL_EXSTYLE) | WS_EX_LAYERED);
        SetLayeredWindowAttributes(L, 0, 200, LWA_ALPHA);
        SetWindowLongA(L, GWL_EXSTYLE, GetWindowLongA(L, GWL_EXSTYLE) & ~WS_EX_LAYERED);
        SetWindowLongA(L, GWL_EXSTYLE, GetWindowLongA(L, GWL_EXSTYLE) | WS_EX_LAYERED);
        if (GetTickCount() - last > 1000) { last = GetTickCount(); printf("%6lu flips %ld\n", (unsigned long)(last - t0), flips); fflush(stdout); }
    }
    stop = 1;
    WaitForSingleObject(th, 5000);
    printf("DONE flips %ld\n", flips); fflush(stdout);
    return 0;
}
