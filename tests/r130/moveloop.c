/* A WM managed window (moveloop.exe N: captioned; moveloop.exe N popup: activated WS_POPUP, fixed
 * size hints) moved and resized N times by SetWindowPos, pumping messages in between: what
 * Inventor's dynamic input popup does per mouse move (130). Prints ms per step.
 * Count the X requests it causes with tests/r130/xreq.py.
 * Build: x86_64-w64-mingw32-gcc -O2 -o moveloop.exe moveloop.c */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
    int i, n = argc > 1 ? atoi(argv[1]) : 300;
    DWORD start, end;
    HWND hwnd;
    MSG msg;

    hwnd = CreateWindowA("static", "moveloop", (argc > 2 ? WS_POPUP : WS_OVERLAPPEDWINDOW) | WS_VISIBLE,
                         100, 100, 200, 150, 0, 0, 0, NULL);
    for (end = GetTickCount() + 1000; GetTickCount() < end; Sleep(10))
        while (PeekMessageA(&msg, 0, 0, 0, PM_REMOVE)) DispatchMessageA(&msg);

    start = GetTickCount();
    for (i = 0; i < n; i++)
    {
        SetWindowPos(hwnd, 0, 100 + i % 200, 100, 200 + i % 100, 150, SWP_NOZORDER | SWP_NOACTIVATE);
        /* let the WM answer, as an app that moves a window per input message does */
        for (end = GetTickCount() + 8; GetTickCount() < end; Sleep(1))
            while (PeekMessageA(&msg, 0, 0, 0, PM_REMOVE)) DispatchMessageA(&msg);
    }
    printf("%d steps, %.2f ms per step\n", n, (GetTickCount() - start) / (double)n);
    return 0;
}
