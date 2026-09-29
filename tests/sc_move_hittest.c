/* WM_SYSCOMMAND SC_MOVE with different low-nibble "hittest" values sent from a
 * WM_LBUTTONDOWN handler (Inventor sends 0xf011 from its app-drawn caption),
 * while another thread drags the mouse with SendInput: does the window move?
 *   sc_move_hittest.exe [wparam...]   default: f010 f011 f012 f013 f014 f01f
 * Build: x86_64-w64-mingw32-gcc -O2 -o sc_move_hittest.exe sc_move_hittest.c */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

static WPARAM cmd;
static HWND hwnd;

static LRESULT CALLBACK proc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    if (m == WM_LBUTTONDOWN)
    {
        ReleaseCapture();
        SendMessageA(h, WM_SYSCOMMAND, cmd, 0);
        return 0;
    }
    return DefWindowProcA(h, m, w, l);
}

static void mouse(DWORD flags, int x, int y)
{
    INPUT in = {INPUT_MOUSE};
    in.mi.dx = x * 65535 / (GetSystemMetrics(SM_CXSCREEN) - 1);
    in.mi.dy = y * 65535 / (GetSystemMetrics(SM_CYSCREEN) - 1);
    in.mi.dwFlags = flags | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_MOVE;
    SendInput(1, &in, sizeof(in));
}

static DWORD WINAPI dragger(void *arg)
{
    int i;
    Sleep(300);
    mouse(0, 250, 250); Sleep(100);
    mouse(MOUSEEVENTF_LEFTDOWN, 250, 250); Sleep(200);
    for (i = 1; i <= 10; i++) { mouse(0, 250 + 10 * i, 250 + 5 * i); Sleep(30); }
    Sleep(200);
    mouse(MOUSEEVENTF_LEFTUP, 350, 300);
    Sleep(300);
    PostMessageA(hwnd, WM_USER, 0, 0);  /* ends the message loop */
    return 0;
}

int main(int argc, char **argv)
{
    static const WPARAM defaults[] = {0xf010, 0xf011, 0xf012, 0xf013, 0xf014, 0xf01f};
    WNDCLASSA c = {0, proc, 0, 0, GetModuleHandleA(0), 0, LoadCursorA(0, (LPCSTR)IDC_ARROW), (HBRUSH)(COLOR_WINDOW + 1), 0, "scmove"};
    int i, n = argc > 1 ? argc - 1 : ARRAYSIZE(defaults), fails = 0;

    RegisterClassA(&c);
    for (i = 0; i < n; i++)
    {
        RECT r;
        MSG msg;
        cmd = argc > 1 ? strtoul(argv[i + 1], NULL, 16) : defaults[i];
        hwnd = CreateWindowExA(WS_EX_TOPMOST, "scmove", "scmove", WS_POPUP | WS_VISIBLE, 100, 100, 300, 300, 0, 0, 0, 0);
        SetForegroundWindow(hwnd);
        CloseHandle(CreateThread(NULL, 0, dragger, NULL, 0, NULL));
        while (GetMessageA(&msg, 0, 0, 0) && msg.message != WM_USER) DispatchMessageA(&msg);
        GetWindowRect(hwnd, &r);
        printf("wparam %04Ix: window at %ld,%ld (%s)\n", cmd, r.left, r.top,
               r.left == 200 && r.top == 150 ? "moved by the drag" : r.left == 100 && r.top == 100 ? "not moved" : "other");
        DestroyWindow(hwnd);
    }
    return fails;
}
