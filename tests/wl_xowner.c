/* Wayland: an owned window must stay above its owner (Win32 semantics), same process (134) or another one (135).
 * Windows are solid colours so a screenshot can be checked: A red, A2 yellow, B green, C blue; A covers most of
 * the screen so the compositor has to overlap them. STEP = ms between steps (default 4000); "step N: ..." lines go to stdout.
 * wl_xowner.exe [owner [hide]]  process A: main window, prints "owner=HWND"; `hide`: hides, then re-shows it
 * wl_xowner.exe HWND [STEP]     process B: WS_POPUP window owned by A's window
 * wl_xowner.exe self [STEP]     A plus a modal-style owned popup B (A disabled)
 * wl_xowner.exe chain [STEP]    A owns B owns C
 * wl_xowner.exe late [STEP]     B (owner A still hidden) is shown first, then A
 * wl_xowner.exe hide [STEP]     A + B; A hidden, shown again, then destroyed (takes B with it)
 * wl_xowner.exe reowner [STEP]  A, B owned by A; new window A2, B's owner := A2 (SetWindowLongPtr + SetWindowPos); then
 *                               B unowned without SetWindowPos and A2 owned by B (stale parent, loop attempt)
 * Then click into A's client area: B must stay on top.
 * Build: x86_64-w64-mingw32-gcc -o tests/wl_xowner.exe tests/wl_xowner.c -luser32 -lgdi32 */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static DWORD step_ms = 4000;
static int step_no;

static void pump(DWORD ms)
{
    MSG msg; DWORD end = GetTickCount() + ms;
    while ((int)(end - GetTickCount()) > 0) { Sleep(10);
        while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) DispatchMessageA(&msg); }
}
static void step(const char *what) { printf("step %d: %s\n", ++step_no, what); fflush(stdout); pump(step_ms); }

static HWND mk(const char *title, COLORREF colour, DWORD style, int x, int y, int w, int h, HWND owner)
{
    WNDCLASSA wc = {0}; char cls[32];
    sprintf(cls, "wlx_%06lx", (unsigned long)colour);
    wc.lpfnWndProc = DefWindowProcA; wc.lpszClassName = cls; wc.hbrBackground = CreateSolidBrush(colour);
    wc.hCursor = LoadCursorA(NULL, (const char *)IDC_ARROW);
    RegisterClassA(&wc);
    return CreateWindowExA(0, cls, title, style, x, y, w, h, owner, NULL, NULL, NULL);
}
#define RED RGB(255,0,0)
#define YELLOW RGB(255,255,0)
#define GREEN RGB(0,255,0)
#define BLUE RGB(0,0,255)
#define MAIN (WS_OVERLAPPEDWINDOW | WS_VISIBLE)
#define POPUP (WS_POPUP | WS_CAPTION | WS_VISIBLE)

int main(int argc, char **argv)
{
    const char *mode = argc > 1 ? argv[1] : "owner";
    int aw = GetSystemMetrics(SM_CXSCREEN) - 220, ah = GetSystemMetrics(SM_CYSCREEN) - 180;
    HWND a, a2, b;

    if (argc > 2 && atoi(argv[2])) step_ms = atoi(argv[2]);

    if (!strcmp(mode, "owner"))
    {
        a = mk("A main (owner)", RED, MAIN, 100, 100, aw, ah, NULL);
        printf("owner=%p\n", a); fflush(stdout);
        if (argc > 2 && !strcmp(argv[2], "hide"))
        {
            pump(3 * step_ms);
            ShowWindow(a, SW_HIDE); step("A hidden");
            ShowWindow(a, SW_SHOW); step("A shown again");
        }
        pump(60000);
    }
    else if (!strcmp(mode, "self"))
    {
        a = mk("A main (owner)", RED, MAIN, 100, 100, aw, ah, NULL);
        b = mk("B modal owned popup (must stay above A)", GREEN, POPUP, 300, 300, 400, 300, a);
        EnableWindow(a, FALSE);
        pump(60000);
    }
    else if (!strcmp(mode, "chain"))
    {
        a = mk("A main (owner)", RED, MAIN, 100, 100, aw, ah, NULL);
        b = mk("B owned by A", GREEN, POPUP, 300, 300, 600, 500, a);
        mk("C owned by B", BLUE, POPUP, 400, 400, 300, 200, b);
        pump(60000);
    }
    else if (!strcmp(mode, "late"))
    {
        a = mk("A main (owner)", RED, WS_OVERLAPPEDWINDOW, 100, 100, aw, ah, NULL);
        b = mk("B owned by A, shown first", GREEN, POPUP, 300, 300, 400, 300, a);
        step("B shown, A hidden");
        ShowWindow(a, SW_SHOW); step("A shown");
        pump(60000);
    }
    else if (!strcmp(mode, "hide"))
    {
        a = mk("A main (owner)", RED, MAIN, 100, 100, aw, ah, NULL);
        b = mk("B owned by A", GREEN, POPUP, 300, 300, 400, 300, a);
        step("A and B shown");
        ShowWindow(a, SW_HIDE); step("A hidden");
        ShowWindow(a, SW_SHOW); step("A shown again");
        step("click A now");
        DestroyWindow(a); printf("B is %s\n", IsWindow(b) ? "alive" : "destroyed"); step("A destroyed");
    }
    else if (!strcmp(mode, "reowner"))
    {
        a = mk("A main (owner)", RED, MAIN, 100, 100, aw, ah, NULL);
        b = mk("B owned by A", GREEN, POPUP, 300, 300, 400, 300, a);
        step("B owned by A");
        a2 = mk("A2 second main", YELLOW, MAIN, 200, 200, aw / 2, ah / 2, NULL);
        SetWindowLongPtrA(b, GWLP_HWNDPARENT, (LONG_PTR)a2);
        SetWindowPos(b, 0, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        printf("GW_OWNER(B)=%p a2=%p\n", GetWindow(b, GW_OWNER), a2);
        step("B owned by A2");
        SetWindowLongPtrA(b, GWLP_HWNDPARENT, 0);
        SetWindowLongPtrA(a2, GWLP_HWNDPARENT, (LONG_PTR)b);
        SetWindowPos(a2, 0, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        printf("GW_OWNER(B)=%p GW_OWNER(A2)=%p b=%p\n", GetWindow(b, GW_OWNER), GetWindow(a2, GW_OWNER), b);
        step("B unowned (not updated), A2 owned by B");
        SetWindowPos(b, 0, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        step("B updated");
        pump(60000);
    }
    else
    {
        HWND owner = (HWND)(ULONG_PTR)strtoull(mode, NULL, 16);
        b = mk("B owned popup (must be above A)", GREEN, POPUP, 300, 300, 400, 300, owner);
        printf("popup=%p owner=%p GW_OWNER=%p\n", b, owner, GetWindow(b, GW_OWNER)); fflush(stdout);
        pump(60000);
    }
    return 0;
}
