/* Cursor set by another process on its child window inside our toplevel (085).
 * Like Chromium/WebView2: the browser process owns a WS_CHILD window parented into the
 * host app's toplevel and answers WM_SETCURSOR with its own cursor.
 * xproc_cursor.exe [arrow|bitmap|none] [secs]: host toplevel at 100,100 400x300 (white),
 * child process fills a 300x200 child at 50,50 (green) and sets the cursor there.
 * The host moves the pointer into the child and prints GetCursorInfo. Look at the screen
 * cursor meanwhile (Wine: x/xcur.c). Exit 0 = GetCursorInfo shows the child's cursor.
 * Build: x86_64-w64-mingw32-gcc -O2 -o xproc_cursor.exe xproc_cursor.c -lgdi32 */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static HCURSOR child_cursor;

static HCURSOR make_bitmap_cursor(void)
{
    /* 32x32 32bpp red square with an alpha channel, like Chromium's custom cursors */
    BITMAPV5HEADER bi = {sizeof(bi), 32, -32, 1, 32, BI_BITFIELDS};
    ICONINFO ii = {FALSE, 4, 4};
    DWORD *bits;
    HDC hdc = GetDC(0);
    int i;

    bi.bV5RedMask = 0xff0000; bi.bV5GreenMask = 0xff00; bi.bV5BlueMask = 0xff; bi.bV5AlphaMask = 0xff000000;
    ii.hbmColor = CreateDIBSection(hdc, (BITMAPINFO *)&bi, DIB_RGB_COLORS, (void **)&bits, NULL, 0);
    for (i = 0; i < 32 * 32; i++) bits[i] = (i % 32 < 16 && i / 32 < 16) ? 0xffff0000 : 0;
    ii.hbmMask = CreateBitmap(32, 32, 1, 1, NULL);
    ReleaseDC(0, hdc);
    return CreateIconIndirect(&ii);
}

static LRESULT CALLBACK child_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg)
    {
    case WM_SETCURSOR:
        SetCursor(child_cursor);
        return TRUE;
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        HBRUSH br = CreateSolidBrush(RGB(0, 255, 0));
        FillRect(hdc, &ps.rcPaint, br);
        DeleteObject(br);
        EndPaint(hwnd, &ps);
        return 0;
    }
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

static int run_child(HWND parent, const char *mode)
{
    WNDCLASSA wc = {0, child_proc, 0, 0, GetModuleHandleA(0), 0, 0, 0, 0, "xpc_child"};
    MSG msg;

    if (!strcmp(mode, "arrow")) child_cursor = LoadCursorA(0, (LPCSTR)IDC_HAND);
    else if (!strcmp(mode, "bitmap")) child_cursor = make_bitmap_cursor();
    else child_cursor = NULL;
    printf("child: cursor %p\n", child_cursor);
    fflush(stdout);
    RegisterClassA(&wc);
    CreateWindowA("xpc_child", "", WS_CHILD | WS_VISIBLE, 50, 50, 300, 200, parent, 0, 0, 0);
    while (GetMessageA(&msg, 0, 0, 0) > 0) DispatchMessageA(&msg);
    return 0;
}

int main(int argc, char **argv)
{
    const char *mode = argc > 1 ? argv[1] : "bitmap";
    int secs = argc > 2 ? atoi(argv[2]) : 5, i;
    WNDCLASSA wc = {0, DefWindowProcA, 0, 0, GetModuleHandleA(0), 0, LoadCursorA(0, (LPCSTR)IDC_ARROW),
                    GetStockObject(WHITE_BRUSH), 0, "xpc_host"};
    STARTUPINFOA si = {sizeof(si)};
    PROCESS_INFORMATION pi;
    CURSORINFO ci = {sizeof(ci)};
    char cmd[MAX_PATH + 64], exe[MAX_PATH];
    HWND host, child = 0;
    INPUT in = {INPUT_MOUSE};
    DWORD end;
    MSG msg;

    if (argc > 2 && !strcmp(argv[1], "child")) return run_child((HWND)(ULONG_PTR)strtoull(argv[2], 0, 16), argc > 3 ? argv[3] : "bitmap");

    RegisterClassA(&wc);
    host = CreateWindowExA(WS_EX_TOPMOST, "xpc_host", "xproc_cursor", WS_POPUP | WS_VISIBLE, 100, 100, 400, 300, 0, 0, 0, 0);
    GetModuleFileNameA(0, exe, sizeof(exe));
    sprintf(cmd, "\"%s\" child %p %s", exe, host, mode);
    if (!CreateProcessA(exe, cmd, 0, 0, FALSE, 0, 0, 0, &si, &pi)) return 2;
    for (i = 0; i < 100 && !((child = GetWindow(host, GW_CHILD)) && IsWindowVisible(child)); i++)
    {
        while (PeekMessageA(&msg, 0, 0, 0, PM_REMOVE)) DispatchMessageA(&msg);
        Sleep(50);
    }
    printf("host %p child %p (pid %lu, host pid %lu)\n", host, child, pi.dwProcessId, GetCurrentProcessId());
    SetForegroundWindow(host);
    SetCursorPos(10, 10);
    Sleep(200);
    SetCursorPos(300, 250);  /* inside the child */
    in.mi.dwFlags = MOUSEEVENTF_MOVE;  /* a real input event, so WM_SETCURSOR is sent */
    in.mi.dx = 1;
    SendInput(1, &in, sizeof(in));
    end = GetTickCount() + secs * 1000;
    while ((int)(end - GetTickCount()) > 0)
    {
        while (PeekMessageA(&msg, 0, 0, 0, PM_REMOVE)) DispatchMessageA(&msg);
        Sleep(20);
    }
    GetCursorInfo(&ci);
    printf("GetCursorInfo: flags %lx cursor %p pos %ld,%ld\n", ci.flags, ci.hCursor, ci.ptScreenPos.x, ci.ptScreenPos.y);
    TerminateProcess(pi.hProcess, 0);
    return ci.hCursor && ci.flags == CURSOR_SHOWING ? 0 : 1;
}
