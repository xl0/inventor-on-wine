/* mip.exe [mip]: extra info of fake moves / SetCursorPos moves, and WM_POINTERUPDATE with EnableMouseInPointer */
#define _WIN32_WINNT 0x0602
#include <windows.h>
#include <stdio.h>
static HWND A, B;
static char got[2048];
static LRESULT CALLBACK wndproc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    char buf[96] = "";
    if (m == WM_MOUSEMOVE) sprintf(buf, " MM(%d,%d)ei=%Ix", (short)LOWORD(l), (short)HIWORD(l), (ULONG_PTR)GetMessageExtraInfo());
    if (m == WM_POINTERUPDATE) sprintf(buf, " PTRUPD(%d,%d)fl=%x", (short)LOWORD(l), (short)HIWORD(l), HIWORD(w));
    if (m == 0x0249 /* WM_POINTERENTER */) sprintf(buf, " PTRENTER");
    if (h == A && strlen(got) + strlen(buf) < sizeof got) strcat(got, buf);
    return DefWindowProcA(h, m, w, l);
}
static void step(const char *what)
{
    DWORD end = GetTickCount() + 400; MSG msg;
    while (GetTickCount() < end) { while (PeekMessageA(&msg, 0, 0, 0, PM_REMOVE)) DispatchMessageA(&msg); Sleep(5); }
    printf("%-34s:%s\n", what, got[0] ? got : " none"); got[0] = 0;
}
int main(int argc, char **argv)
{
    WNDCLASSA wc = {0};
    setvbuf(stdout, NULL, _IONBF, 0);
    if (argc > 1) printf("EnableMouseInPointer: %d\n", EnableMouseInPointer(TRUE));
    wc.lpfnWndProc = wndproc; wc.lpszClassName = "mip"; wc.hCursor = LoadCursorA(0, (LPCSTR)IDC_ARROW);
    RegisterClassA(&wc);
    SetCursorPos(400, 300);
    A = CreateWindowExA(WS_EX_TOPMOST, "mip", "A", WS_POPUP | WS_VISIBLE, 100, 100, 600, 400, 0, 0, 0, 0);
    B = CreateWindowExA(WS_EX_TOPMOST | WS_EX_NOACTIVATE, "mip", "B", WS_POPUP, 900, 100, 50, 50, 0, 0, 0, 0);
    step("setup");
    ShowWindow(B, SW_SHOWNOACTIVATE); step("show B elsewhere");
    SetWindowPos(B, 0, 910, 100, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE); step("move B elsewhere");
    ShowWindow(B, SW_HIDE); step("hide B");
    SetCursorPos(401, 300); step("SetCursorPos(401,300)");
    SetCursorPos(401, 300); step("SetCursorPos same");
    mouse_event(MOUSEEVENTF_MOVE, 1, 0, 0, 0x1234); step("mouse_event +1 (info 0x1234)");
    return 0;
}
