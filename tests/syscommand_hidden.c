/* DefWindowProc WM_SYSCOMMAND SC_MAXIMIZE/SC_MINIMIZE/SC_RESTORE on hidden/visible windows:
 * resulting visibility / zoomed / iconic state and whether WM_SHOWWINDOW/WM_WINDOWPOSCHANGED were sent.
 * Build: x86_64-w64-mingw32-gcc -O2 -o syscommand_hidden.exe syscommand_hidden.c */
#include <windows.h>
#include <stdio.h>
static int posch, showw;
static LRESULT CALLBACK proc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    if (m == WM_WINDOWPOSCHANGED) posch++;
    if (m == WM_SHOWWINDOW) showw++;
    return DefWindowProcA(h, m, w, l);
}
static void run(const char *desc, DWORD style, WPARAM cmd)
{
    HWND h = CreateWindowExA(0, "sccases", "t", style, 10, 10, 300, 200, 0, 0, 0, 0);
    posch = showw = 0;
    if (cmd >= 0xf000) SendMessageA(h, WM_SYSCOMMAND, cmd, 0);
    else ShowWindow(h, cmd);
    printf("%-40s -> vis %d zoomed %d iconic %d showwindow %d poschanged %d\n", desc,
           IsWindowVisible(h), IsZoomed(h), IsIconic(h), showw, posch);
    DestroyWindow(h);
}
int main(void)
{
    WNDCLASSA wc = {0}; wc.lpfnWndProc = proc; wc.lpszClassName = "sccases"; RegisterClassA(&wc);
    run("hidden SC_MAXIMIZE", WS_OVERLAPPEDWINDOW, SC_MAXIMIZE);
    run("hidden+WS_MAXIMIZE SC_MAXIMIZE", WS_OVERLAPPEDWINDOW | WS_MAXIMIZE, SC_MAXIMIZE);
    run("visible SC_MAXIMIZE", WS_OVERLAPPEDWINDOW | WS_VISIBLE, SC_MAXIMIZE);
    run("visible+WS_MAXIMIZE SC_MAXIMIZE", WS_OVERLAPPEDWINDOW | WS_VISIBLE | WS_MAXIMIZE, SC_MAXIMIZE);
    run("hidden SC_MINIMIZE", WS_OVERLAPPEDWINDOW, SC_MINIMIZE);
    run("hidden+WS_MINIMIZE SC_MINIMIZE", WS_OVERLAPPEDWINDOW | WS_MINIMIZE, SC_MINIMIZE);
    run("hidden SC_RESTORE", WS_OVERLAPPEDWINDOW, SC_RESTORE);
    run("hidden+WS_MAXIMIZE SC_RESTORE", WS_OVERLAPPEDWINDOW | WS_MAXIMIZE, SC_RESTORE);
    run("visible+WS_MAXIMIZE SC_RESTORE", WS_OVERLAPPEDWINDOW | WS_VISIBLE | WS_MAXIMIZE, SC_RESTORE);
    run("hidden+WS_MAXIMIZE ShowWindow(SW_MAXIMIZE)", WS_OVERLAPPEDWINDOW | WS_MAXIMIZE, SW_MAXIMIZE);
    run("hidden+WS_MINIMIZE ShowWindow(SW_MINIMIZE)", WS_OVERLAPPEDWINDOW | WS_MINIMIZE, SW_MINIMIZE);
    run("hidden ShowWindow(SW_MAXIMIZE)", WS_OVERLAPPEDWINDOW, SW_MAXIMIZE);
    return 0;
}
