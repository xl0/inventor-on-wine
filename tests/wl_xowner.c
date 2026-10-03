/* Wayland: window owned by a window of ANOTHER process must stay above its owner (Win32 semantics).
 * wl_xowner.exe            -> process A: opens a main window, prints "owner=HWND", pumps messages 60 s
 * wl_xowner.exe HWND       -> process B: opens a WS_POPUP window owned by A's window at A.pos+(100,100)
 * wl_xowner.exe self      -> one process: main window A plus a modal-style owned popup B (EnableWindow(A, FALSE)), 60 s
 * Then click into A's client area: on Windows B stays on top. Build: x86_64-w64-mingw32-gcc -o tests/wl_xowner.exe tests/wl_xowner.c -luser32 */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(int argc, char **argv)
{
    MSG msg; DWORD end = GetTickCount() + 60000; HWND h;
    if (argc > 1 && !strcmp(argv[1], "self"))
    {
        HWND a = CreateWindowA("static", "A main (owner)", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 200, 200, 800, 600, NULL, NULL, NULL, NULL);
        h = CreateWindowExA(0, "static", "B modal owned popup (must stay above A)", WS_POPUP | WS_CAPTION | WS_VISIBLE,
                            300, 300, 400, 300, a, NULL, NULL, NULL);
        EnableWindow(a, FALSE);
    }
    else if (argc < 2)
    {
        h = CreateWindowA("static", "A main (owner)", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 200, 200, 800, 600, NULL, NULL, NULL, NULL);
        printf("owner=%p\n", h); fflush(stdout);
    }
    else
    {
        HWND owner = (HWND)(ULONG_PTR)strtoull(argv[1], NULL, 16); RECT r; GetWindowRect(owner, &r);
        h = CreateWindowExA(0, "static", "B owned popup (must be above A)", WS_POPUP | WS_CAPTION | WS_VISIBLE,
                            r.left + 100, r.top + 100, 400, 300, owner, NULL, NULL, NULL);
        printf("popup=%p owner=%p GW_OWNER=%p\n", h, owner, GetWindow(h, GW_OWNER)); fflush(stdout);
    }
    while (GetTickCount() < end) { Sleep(10);
        while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) DispatchMessageA(&msg); }
    return 0;
}
