/* winex11 lock-order repro (issue 173): the main thread draws on window B of another thread while
 * that thread moves B. Every move makes B's DCs dirty; win32u functions that draw with the user lock
 * held (NtUserDrawIconEx: icon object) then ask the driver for the drawable (window data lock), while
 * the driver's WindowPosChanged holds the window data lock and reads window styles (user lock).
 * Build: x86_64-w64-mingw32-gcc -O1 -o iconlock.exe iconlock.c -lgdi32 -luser32
 *   iconlock [SECS] [icon|caption|menu|text]   prints "DONE" at the end; a hang is a result (watchdog).
 * A third thread moves its child window inside B (dirty DC without B moving).
 * icon: DrawIconEx on GetDC(B); caption: DefWindowProc(WM_NCPAINT) (system menu icon); menu: DrawMenuBar;
 * text: DefWindowProc(WM_SETTEXT), which repaints the caption. */
#include <windows.h>
#include <stdio.h>
#include <string.h>

static HWND A, B, C;
static volatile LONG stop, draws, moves;

static LRESULT CALLBACK wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_CLOSE) return 0;
    return DefWindowProcA(hwnd, msg, wp, lp);
}

static void pump(void)
{
    MSG msg;
    while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageA(&msg); }
}

static DWORD WINAPI move_proc(void *arg)
{
    int i;
    B = CreateWindowExA(0, "iconlock", "B moves", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN | WS_VISIBLE, 150, 150, 300, 200, NULL, arg, NULL, NULL);
    for (i = 0; !stop; i++)
    {
        SetWindowPos(B, 0, 100 + i % 97, 100 + i % 61, 300 + i % 3, 200, SWP_NOZORDER | SWP_NOACTIVATE);
        if (!(i % 16)) ShowWindow(B, (i & 16) ? SW_HIDE : SW_SHOWNA);
        InterlockedIncrement(&moves);
        if (!(i % 8)) pump();
    }
    return 0;
}

static DWORD WINAPI child_proc(void *arg)
{
    int i;
    C = CreateWindowExA(0, "iconlock", "C", WS_CHILD | WS_VISIBLE, 5, 5, 40, 40, B, NULL, NULL, NULL);
    for (i = 0; !stop; i++)
    {
        SetWindowPos(C, 0, 5 + i % 31, 5 + i % 17, 40, 40, SWP_NOZORDER | SWP_NOACTIVATE);
        if (!(i % 8)) pump();
    }
    return 0;
}

int main(int argc, char **argv)
{
    int secs = argc > 1 ? atoi(argv[1]) : 20, mode = 0, i;
    WNDCLASSA wc = {0}; DWORD t0 = GetTickCount(), last = t0; HANDLE th;
    HICON icon = LoadIconA(NULL, (const char *)IDI_APPLICATION);
    HMENU menu;

    if (argc > 2 && !strcmp(argv[2], "caption")) mode = 1;
    if (argc > 2 && !strcmp(argv[2], "menu")) mode = 2;
    if (argc > 2 && !strcmp(argv[2], "text")) mode = 3;
    wc.lpfnWndProc = wndproc; wc.lpszClassName = "iconlock"; wc.hbrBackground = GetStockObject(GRAY_BRUSH);
    wc.hIcon = icon;
    RegisterClassA(&wc);
    A = CreateWindowExA(0, "iconlock", "A draws", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 60, 60, 420, 320, NULL, NULL, NULL, NULL);
    menu = CreateMenu(); AppendMenuA(menu, MF_STRING, 1, "One"); AppendMenuA(menu, MF_STRING, 2, "Two");
    th = CreateThread(NULL, 0, move_proc, mode == 2 ? menu : NULL, 0, NULL);
    for (i = 0; i < 50 || !B; i++) { pump(); Sleep(10); }
    CreateThread(NULL, 0, child_proc, NULL, 0, NULL);
    while (GetTickCount() - t0 < secs * 1000)
    {
        if (mode == 0)
        {
            HDC dc = GetDC(B);
            for (i = 0; i < 64; i++) DrawIconEx(dc, 10 + i, 10, icon, 32, 32, 0, NULL, DI_NORMAL);
            ReleaseDC(B, dc);
        }
        else if (mode == 1) DefWindowProcA(B, WM_NCPAINT, 1, 0);
        else if (mode == 2) DrawMenuBar(B);
        else DefWindowProcA(B, WM_SETTEXT, 0, (LPARAM)"B moves");
        InterlockedIncrement(&draws);
        pump();
        if (GetTickCount() - last > 1000)
        {
            last = GetTickCount();
            printf("%6lu draws %ld moves %ld\n", (unsigned long)(last - t0), draws, moves); fflush(stdout);
        }
    }
    stop = 1;
    WaitForSingleObject(th, 5000);
    printf("DONE draws %ld moves %ld\n", draws, moves); fflush(stdout);
    return 0;
}
