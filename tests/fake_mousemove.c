/* fake_mousemove.exe: which window changes make the system post a WM_MOUSEMOVE while the cursor
   stays still (120: Wine posted one for any visible window's move/resize, so Chromium saw "mouse
   moved" after the page loaded and showed an HTML tooltip that Windows never shows).
   Cursor parked at 400,300; A = popup 100,100 600x400, C = its child under the cursor, T = its
   WS_EX_TRANSPARENT child under the cursor, X = child of A owned by another thread,
   B = popup elsewhere (1000,100). Prints the mouse messages each step produced.
   x86_64-w64-mingw32-gcc -O2 -o fake_mousemove.exe fake_mousemove.c */
#include <windows.h>
#include <stdio.h>

static HWND A, B, C, T, X;
static char got[1024];

static const char *name(HWND h)
{
    return h == A ? "A" : h == B ? "B" : h == C ? "C" : h == T ? "T" : h == X ? "X" : "?";
}

static LRESULT CALLBACK wndproc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    if (m == WM_MOUSEMOVE || m == WM_NCMOUSEMOVE)
    {
        char buf[64];
        sprintf(buf, " %s:%s(%d,%d)", name(h), m == WM_MOUSEMOVE ? "MM" : "NCMM", (short)LOWORD(l), (short)HIWORD(l));
        if (strlen(got) + strlen(buf) < sizeof got) strcat(got, buf);
    }
    return DefWindowProcA(h, m, w, l);
}

static void pump(void)
{
    DWORD end = GetTickCount() + 500;
    MSG msg;
    while (GetTickCount() < end)
    {
        while (PeekMessageA(&msg, 0, 0, 0, PM_REMOVE)) DispatchMessageA(&msg);
        Sleep(10);
    }
}

static void step(const char *what)
{
    pump();
    POINT pt = {400, 300}, cur;
    HWND under = WindowFromPoint(pt);
    char cls[32] = "";
    GetCursorPos(&cur);
    GetClassNameA(under, cls, sizeof cls);
    printf("%-44s:%s   [cursor %ld,%ld over %s %s]\n", what, got[0] ? got : " none", cur.x, cur.y, name(under), cls);
    got[0] = 0;
}

static HANDLE x_ready, x_quit;
static DWORD WINAPI x_thread(void *arg)
{
    MSG msg;
    X = CreateWindowExA(0, "fmm", "X", WS_CHILD | WS_VISIBLE, 200, 50, 50, 50, A, 0, 0, 0);
    SetEvent(x_ready);
    while (MsgWaitForMultipleObjects(1, &x_quit, FALSE, INFINITE, QS_ALLINPUT) != WAIT_OBJECT_0)
        while (PeekMessageA(&msg, 0, 0, 0, PM_REMOVE)) DispatchMessageA(&msg);
    return 0;
}

int main(void)
{
    WNDCLASSA wc = {0};
    HANDLE thread;

    setvbuf(stdout, NULL, _IONBF, 0);
    wc.lpfnWndProc = wndproc; wc.lpszClassName = "fmm"; wc.hCursor = LoadCursorA(0, (LPCSTR)IDC_ARROW);
    RegisterClassA(&wc);
    SetCursorPos(400, 300);
    B = CreateWindowExA(WS_EX_NOACTIVATE | WS_EX_TOPMOST, "fmm", "B", WS_POPUP | WS_VISIBLE, 1000, 100, 100, 100, 0, 0, 0, 0);
    A = CreateWindowExA(WS_EX_TOPMOST, "fmm", "A", WS_POPUP, 100, 100, 600, 400, 0, 0, 0, 0);
    C = CreateWindowExA(0, "fmm", "C", WS_CHILD | WS_VISIBLE, 250, 150, 100, 100, A, 0, 0, 0);
    step("setup (B shown elsewhere)");

    ShowWindow(A, SW_SHOWNOACTIVATE);
    step("show A under the cursor");
    SetWindowPos(C, 0, 0, 0, 120, 120, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    step("resize C, still under the cursor");
    SetWindowPos(C, 0, 251, 150, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    step("move C by 1 px, still under the cursor");
    SetWindowPos(C, 0, 0, 0, 10, 10, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    step("shrink C, cursor now over A");
    SetWindowPos(C, 0, 0, 0, 100, 100, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    step("grow C, cursor over C again");
    SetWindowPos(B, 0, 1010, 100, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    step("move B (elsewhere)");
    SetWindowPos(B, 0, 0, 0, 150, 150, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    step("resize B (elsewhere)");
    ShowWindow(B, SW_HIDE);
    step("hide B (elsewhere)");
    ShowWindow(B, SW_SHOWNOACTIVATE);
    step("show B (elsewhere)");
    SetWindowPos(A, 0, 0, 0, 650, 400, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    step("resize A, cursor still over C");
    SetWindowPos(A, 0, 101, 100, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    step("move A by 1 px, cursor still over C");
    T = CreateWindowExA(WS_EX_TRANSPARENT, "fmm", "T", WS_CHILD | WS_VISIBLE, 240, 140, 50, 50, A, 0, 0, 0);
    step("create transparent child T under the cursor");
    SetWindowPos(T, 0, 0, 0, 150, 150, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    step("resize T");
    x_ready = CreateEventA(NULL, FALSE, FALSE, NULL);
    x_quit = CreateEventA(NULL, FALSE, FALSE, NULL);
    thread = CreateThread(NULL, 0, x_thread, NULL, 0, NULL);
    while (MsgWaitForMultipleObjects(1, &x_ready, FALSE, INFINITE, QS_ALLINPUT) != WAIT_OBJECT_0) pump();
    step("other thread's child X created (not under it)");
    SetWindowPos(X, 0, 0, 0, 60, 60, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_ASYNCWINDOWPOS);
    step("resize X (not under the cursor)");
    SetWindowPos(C, 0, 0, 0, 10, 10, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    step("shrink C, cursor now over A");
    SetWindowPos(A, 0, 0, 0, 600, 400, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    step("resize A, cursor over A");
    SetCursorPos(400, 300);
    step("SetCursorPos to the same position");
    SetCursorPos(401, 300);
    step("SetCursorPos 1 px");
    SetEvent(x_quit);
    while (MsgWaitForMultipleObjects(1, &thread, FALSE, INFINITE, QS_ALLINPUT) != WAIT_OBJECT_0) pump();
    return 0;
}
