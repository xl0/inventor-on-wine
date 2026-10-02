/* fmm2.exe MODE: review probes for fake WM_MOUSEMOVE after show/hide (fix/120).
   Cursor parked at 400,300.  Modes: loops, cases, menu, perf.
   x86_64-w64-mingw32-gcc -O2 -o fmm2.exe fmm2.c -lgdi32 */
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static HWND A, B, P, Z, H, HC, M, MIN, D, L, OT;
static char got[2048];
static int mm_count[16];
static int mode_loop;
static int toggles;

static const char *name(HWND h)
{
    return h == A ? "A" : h == B ? "B" : h == P ? "P" : h == Z ? "Z" : h == H ? "H" : h == HC ? "HC" :
           h == M ? "M" : h == MIN ? "MIN" : h == D ? "D" : h == L ? "L" : h == OT ? "OT" : "?";
}

static LRESULT CALLBACK wndproc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    if (m == WM_MOUSEMOVE || m == WM_NCMOUSEMOVE)
    {
        char buf[96];
        sprintf(buf, " %s:%s(%d,%d)%s", name(h), m == WM_MOUSEMOVE ? "MM" : "NCMM", (short)LOWORD(l), (short)HIWORD(l),
                GetMessageExtraInfo() ? "x" : "");
        if (strlen(got) + strlen(buf) < sizeof got) strcat(got, buf);
        if (m == WM_MOUSEMOVE)
        {
            if (h == A) mm_count[0]++;
            if (h == P) mm_count[1]++;
            switch (mode_loop)
            {
            case 1: /* toggle B (elsewhere) on every move over A */
                if (h == A) { ShowWindow(B, IsWindowVisible(B) ? SW_HIDE : SW_SHOWNOACTIVATE); toggles++; }
                break;
            case 2: /* hover popup under the cursor: show on A move, hide on P move */
                if (h == A && !IsWindowVisible(P)) { ShowWindow(P, SW_SHOWNOACTIVATE); toggles++; }
                if (h == P) { ShowWindow(P, SW_HIDE); toggles++; }
                break;
            case 3: /* toggle a child of A elsewhere on every move */
                if (h == A) { ShowWindow(HC, IsWindowVisible(HC) ? SW_HIDE : SW_SHOWNOACTIVATE); toggles++; }
                break;
            case 4: /* show a tooltip-like popup next to the cursor (not under it) on every move */
                if (h == A) { SetWindowPos(B, HWND_TOPMOST, 410, 320, 50, 20, SWP_NOACTIVATE | SWP_SHOWWINDOW); toggles++; }
                break;
            case 5: /* hide-then-show tooltip next to the cursor on every move (reposition pattern) */
                if (h == A) { ShowWindow(B, SW_HIDE); SetWindowPos(B, HWND_TOPMOST, 410, 320, 50, 20, SWP_NOACTIVATE | SWP_SHOWWINDOW); toggles++; }
                break;
            case 6: /* recreate a popup elsewhere on every move */
                if (h == A) { DestroyWindow(B); B = CreateWindowExA(WS_EX_NOACTIVATE | WS_EX_TOPMOST, "fmm2", "B", WS_POPUP | WS_VISIBLE, 1000, 100, 100, 100, 0, 0, 0, 0); toggles++; }
                break;
            }
        }
    }
    if (m == WM_TIMER && w == 77)
    {
        static int st;
        static const char *what[] = { "opened under still cursor", "after fake move (show D)", "after real move to 400,345",
                                      "after VK_DOWN", "after fake move (hide D)" };
        HMENU menu = (HMENU)GetPropA(h, "menu");
        int i, n = GetMenuItemCount(menu);
        printf("menu %-28s hilite:", what[st]);
        for (i = 0; i < n; i++) if (GetMenuState(menu, i, MF_BYPOSITION) & MF_HILITE) printf(" %d", i);
        printf("  [%s]\n", got);
        got[0] = 0;
        switch (st++)
        {
        case 0: ShowWindow(D, SW_SHOWNOACTIVATE); break;
        case 1: SetCursorPos(400, 345); break;
        case 2: keybd_event(VK_DOWN, 0, 0, 0); keybd_event(VK_DOWN, 0, KEYEVENTF_KEYUP, 0); break;
        case 3: ShowWindow(D, SW_HIDE); break;
        default: KillTimer(h, 77); SetCursorPos(400, 300); EndMenu(); break;
        }
    }
    if (m == WM_TIMER && w == 55) mm_count[5]++;
    if (m == WM_PAINT && h == A) mm_count[6]++;
    if (m == WM_MENUSELECT)
    {
        char buf[64];
        sprintf(buf, " MENUSELECT(%d,%x)", LOWORD(w), HIWORD(w));
        if (strlen(got) + strlen(buf) < sizeof got) strcat(got, buf);
    }
    return DefWindowProcA(h, m, w, l);
}

static void pump_ms(DWORD ms)
{
    DWORD end = GetTickCount() + ms;
    MSG msg;
    while (GetTickCount() < end)
    {
        while (GetTickCount() < end && PeekMessageA(&msg, 0, 0, 0, PM_REMOVE)) DispatchMessageA(&msg);
        Sleep(5);
    }
}

static void step(const char *what)
{
    POINT cur;
    pump_ms(400);
    GetCursorPos(&cur);
    printf("%-58s:%s  [cur %ld,%ld]\n", what, got[0] ? got : " none", cur.x, cur.y);
    got[0] = 0;
}

static HWND mk(DWORD ex, const char *n, DWORD style, int x, int y, int w, int h, HWND parent)
{
    return CreateWindowExA(ex, "fmm2", n, style, x, y, w, h, parent, 0, 0, 0);
}

static HANDLE ot_ready, ot_cmd, ot_done, ot_quit;
static DWORD WINAPI ot_thread(void *arg)
{
    MSG msg;
    HANDLE h[2];
    OT = mk(WS_EX_NOACTIVATE | WS_EX_TOPMOST, "OT", WS_POPUP, 1000, 400, 100, 100, 0);
    SetEvent(ot_ready);
    h[0] = ot_quit; h[1] = ot_cmd;
    for (;;)
    {
        DWORD r = MsgWaitForMultipleObjects(2, h, FALSE, INFINITE, QS_ALLINPUT);
        if (r == WAIT_OBJECT_0) break;
        if (r == WAIT_OBJECT_0 + 1) { ShowWindow(OT, IsWindowVisible(OT) ? SW_HIDE : SW_SHOWNOACTIVATE); SetEvent(ot_done); }
        while (PeekMessageA(&msg, 0, 0, 0, PM_REMOVE)) DispatchMessageA(&msg);
    }
    return 0;
}

static void loops(void)
{
    static const char *names[] = { "", "toggle B elsewhere on A move", "hover popup P under cursor (show on A, hide on P)",
                                   "toggle child HC (elsewhere) on A move", "SWP_SHOWWINDOW B next to cursor on A move",
                                   "hide+show B next to cursor on A move", "destroy+create visible B on A move" };
    int i;
    for (i = 1; i <= 6; i++)
    {
        A = mk(WS_EX_TOPMOST, "A", WS_POPUP | WS_VISIBLE, 100, 100, 600, 400, 0);
        HC = mk(0, "HC", WS_CHILD, 500, 10, 50, 50, A);
        B = mk(WS_EX_NOACTIVATE | WS_EX_TOPMOST, "B", WS_POPUP, 1000, 100, 100, 100, 0);
        P = mk(WS_EX_NOACTIVATE | WS_EX_TOPMOST, "P", WS_POPUP, 380, 280, 40, 40, 0);
        pump_ms(500);
        memset(mm_count, 0, sizeof mm_count); toggles = 0; got[0] = 0;
        mode_loop = i;
        SetTimer(A, 55, 50, NULL);
        /* kick: one show elsewhere */
        ShowWindow(D, SW_SHOWNOACTIVATE); ShowWindow(D, SW_HIDE);
        pump_ms(2000);
        mode_loop = 0;
        printf("loop %d %-52s: A moves %d, P moves %d, toggles %d, 50ms timers %d\n", i, names[i], mm_count[0], mm_count[1], toggles, mm_count[5]);
        DestroyWindow(A); DestroyWindow(B); DestroyWindow(P);
        pump_ms(300);
    }
}

static void cases(void)
{
    HANDLE thread;
    Z = mk(0, "Z", WS_POPUP | WS_VISIBLE, 50, 50, 800, 600, 0);  /* background under everything */
    A = mk(WS_EX_TOPMOST, "A", WS_POPUP | WS_VISIBLE, 100, 100, 600, 400, 0);
    step("setup");
    H = mk(0, "H", WS_POPUP, 1000, 100, 100, 100, 0);
    step("create hidden popup H elsewhere");
    HC = mk(0, "HC", WS_CHILD, 10, 10, 50, 50, H);
    step("create hidden child HC of hidden H");
    ShowWindow(HC, SW_SHOWNOACTIVATE);
    step("show child HC of hidden H (no visible change)");
    ShowWindow(HC, SW_HIDE);
    step("hide child HC of hidden H");
    mk(0, "HC2", WS_CHILD | WS_VISIBLE, 60, 10, 20, 20, H);
    step("create WS_VISIBLE child in hidden H");
    DestroyWindow(H);
    step("destroy hidden H");
    H = mk(WS_EX_TOPMOST, "H", WS_POPUP, 300, 200, 200, 200, 0);
    HC = mk(0, "HC", WS_CHILD, 10, 10, 50, 50, H);
    step("create hidden H under cursor + hidden child");
    ShowWindow(HC, SW_SHOWNOACTIVATE);
    step("show child of hidden H (H spans the cursor)");
    SetWindowPos(HC, 0, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_HIDEWINDOW);
    step("SWP_HIDEWINDOW child of hidden H");
    SetWindowPos(HC, 0, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    step("SWP_SHOWWINDOW child of hidden H");
    SetWindowPos(HC, 0, 20, 20, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    step("move visible child of hidden H");
    SetWindowPos(H, 0, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    step("noop SWP hidden H");
    SetWindowPos(H, 0, 310, 200, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    step("move hidden H");
    DestroyWindow(H);
    step("destroy hidden H");
    M = CreateWindowExA(0, "fmm2", "M", 0, 0, 0, 100, 100, HWND_MESSAGE, 0, 0, 0);
    step("create message-only M");
    ShowWindow(M, SW_SHOWNOACTIVATE);
    step("show message-only M");
    ShowWindow(M, SW_HIDE);
    step("hide message-only M");
    MIN = mk(0, "MIN", WS_OVERLAPPEDWINDOW, 1000, 400, 200, 200, 0);
    step("create hidden overlapped MIN");
    ShowWindow(MIN, SW_SHOWMINNOACTIVE);
    step("show MIN minimized");
    ShowWindow(MIN, SW_HIDE);
    step("hide minimized MIN");
    DestroyWindow(MIN);
    step("destroy MIN");
    B = mk(WS_EX_NOACTIVATE | WS_EX_TOPMOST, "B", WS_POPUP | WS_VISIBLE, 1000, 100, 100, 100, 0);
    step("create visible B elsewhere");
    SetWindowPos(B, 0, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    step("SWP_SHOWWINDOW on already visible B");
    SetWindowPos(B, 0, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    step("SWP z-order only (B to top)");
    ShowWindow(B, SW_HIDE);
    step("hide B");
    SetWindowPos(B, 0, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_HIDEWINDOW);
    step("SWP_HIDEWINDOW on already hidden B");
    L = mk(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_NOACTIVATE, "L", WS_POPUP, 350, 250, 100, 100, 0);
    SetLayeredWindowAttributes(L, 0, 128, LWA_ALPHA);
    step("create layered transparent L under cursor (hidden)");
    ShowWindow(L, SW_SHOWNOACTIVATE);
    step("show layered transparent L under cursor");
    ShowWindow(L, SW_HIDE);
    step("hide L");
    SetCapture(B);
    step("SetCapture(B) (B hidden, elsewhere)");
    ShowWindow(D, SW_SHOWNOACTIVATE);
    step("show D elsewhere while B has capture");
    ShowWindow(D, SW_HIDE);
    step("hide D while B has capture");
    ReleaseCapture();
    step("ReleaseCapture");
    ot_ready = CreateEventA(NULL, FALSE, FALSE, NULL);
    ot_cmd = CreateEventA(NULL, FALSE, FALSE, NULL);
    ot_done = CreateEventA(NULL, FALSE, FALSE, NULL);
    ot_quit = CreateEventA(NULL, FALSE, FALSE, NULL);
    thread = CreateThread(NULL, 0, ot_thread, NULL, 0, NULL);
    WaitForSingleObject(ot_ready, INFINITE);
    step("other thread created hidden OT");
    SetEvent(ot_cmd); WaitForSingleObject(ot_done, INFINITE);
    step("other thread shows OT elsewhere");
    SetEvent(ot_cmd); WaitForSingleObject(ot_done, INFINITE);
    step("other thread hides OT elsewhere");
    ShowWindow(A, SW_HIDE);
    step("hide A (cursor now over Z)");
    ShowWindow(A, SW_SHOWNOACTIVATE);
    step("show A again");
    EnableWindow(A, FALSE);
    step("disable A");
    ShowWindow(D, SW_SHOWNOACTIVATE); ShowWindow(D, SW_HIDE);
    step("show+hide D while A disabled");
    EnableWindow(A, TRUE);
    step("enable A");
    SetEvent(ot_quit);
    WaitForSingleObject(thread, INFINITE);
}

static LRESULT CALLBACK filter(int code, WPARAM w, LPARAM l)
{
    MSG *msg = (MSG *)l;
    if (code == MSGF_MENU && msg->message == WM_MOUSEMOVE)
    {
        char buf[64];
        sprintf(buf, " filterMM(%d,%d)", (short)LOWORD(msg->lParam), (short)HIWORD(msg->lParam));
        if (strlen(got) + strlen(buf) < sizeof got) strcat(got, buf);
    }
    return CallNextHookEx(0, code, w, l);
}

static void combo(void)
{
    HWND cb;
    int i, sel;
    char s[16];
    A = mk(WS_EX_TOPMOST, "A", WS_POPUP | WS_VISIBLE, 100, 100, 600, 400, 0);
    cb = CreateWindowExA(0, "ComboBox", "", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL, 250, 120, 150, 300, A, (HMENU)1, 0, 0);
    for (i = 0; i < 15; i++) { sprintf(s, "item %d", i); SendMessageA(cb, CB_ADDSTRING, 0, (LPARAM)s); }
    SendMessageA(cb, CB_SETCURSEL, 0, 0);
    SetForegroundWindow(A);
    SetFocus(cb);
    pump_ms(300);
    SendMessageA(cb, CB_SHOWDROPDOWN, TRUE, 0);
    pump_ms(500);
    sel = SendMessageA(cb, CB_GETCURSEL, 0, 0);
    {
        COMBOBOXINFO ci = { sizeof ci };
        GetComboBoxInfo(cb, &ci);
        printf("combo: dropped %d, cursel %d, list caret %d, capture %s\n", (int)SendMessageA(cb, CB_GETDROPPEDSTATE, 0, 0), sel,
               (int)SendMessageA(ci.hwndList, LB_GETCARETINDEX, 0, 0), GetCapture() == ci.hwndList ? "list" : GetCapture() ? "other" : "none");
        ShowWindow(D, SW_SHOWNOACTIVATE);
        pump_ms(300);
        printf("combo after fake move: cursel %d, list caret %d, curselLB %d\n", (int)SendMessageA(cb, CB_GETCURSEL, 0, 0),
               (int)SendMessageA(ci.hwndList, LB_GETCARETINDEX, 0, 0), (int)SendMessageA(ci.hwndList, LB_GETCURSEL, 0, 0));
    }
    SendMessageA(cb, CB_SHOWDROPDOWN, FALSE, 0);
}

static void menu(void)
{
    SetWindowsHookExA(WH_MSGFILTER, filter, 0, GetCurrentThreadId());
    {
    HMENU m = CreatePopupMenu();
    int i;
    char s[16];
    A = mk(WS_EX_TOPMOST, "A", WS_POPUP | WS_VISIBLE, 100, 100, 600, 400, 0);
    SetForegroundWindow(A);
    for (i = 0; i < 8; i++) { sprintf(s, "item %d", i); AppendMenuA(m, MF_STRING, 100 + i, s); }
    SetPropA(A, "menu", m);
    pump_ms(300); got[0] = 0;
    /* menu origin above-left of the cursor so it lands on an item */
    SetTimer(A, 77, 400, NULL);
    TrackPopupMenu(m, TPM_LEFTALIGN | TPM_TOPALIGN, getenv("AT") ? 400 : 380, getenv("AT") ? 300 : 240, 0, A, NULL);
    printf("menu done [%s]\n", got);
    got[0] = 0;
    }
}

static double now_ms(void)
{
    LARGE_INTEGER c, f;
    QueryPerformanceCounter(&c); QueryPerformanceFrequency(&f);
    return c.QuadPart * 1000.0 / f.QuadPart;
}

static double wait_mm(double tmo)
{
    double t0 = now_ms();
    MSG msg;
    int before = mm_count[0];
    while (now_ms() - t0 < tmo)
    {
        while (PeekMessageA(&msg, 0, 0, 0, PM_REMOVE)) DispatchMessageA(&msg);
        if (mm_count[0] != before) return now_ms() - t0;
    }
    return -1;
}

static void lat(void)
{
    int i, j;
    double t, prev;
    A = mk(WS_EX_TOPMOST, "A", WS_POPUP | WS_VISIBLE, 100, 100, 600, 400, 0);
    B = mk(WS_EX_NOACTIVATE | WS_EX_TOPMOST, "B", WS_POPUP, 1000, 100, 20, 20, 0);
    pump_ms(500);
    printf("latency show/hide B -> MM (ms):");
    for (i = 0; i < 12; i++)
    {
        pump_ms(100 + i * 7);
        ShowWindow(B, (i & 1) ? SW_HIDE : SW_SHOWNOACTIVATE);
        printf(" %.1f", wait_mm(500));
    }
    printf("\n");
    pump_ms(300);
    memset(mm_count, 0, sizeof mm_count);
    for (j = 0; j < 20; j++) ShowWindow(B, (j & 1) ? SW_HIDE : SW_SHOWNOACTIVATE);
    pump_ms(300);
    printf("burst of 20 show/hide: %d MM\n", mm_count[0]);
    /* toggle as soon as the MM arrives; print intervals */
    printf("loop intervals (ms):");
    prev = now_ms();
    ShowWindow(B, SW_SHOWNOACTIVATE);
    for (i = 0; i < 15; i++)
    {
        if (wait_mm(500) < 0) { printf(" timeout"); break; }
        t = now_ms(); printf(" %.1f", t - prev); prev = t;
        ShowWindow(B, IsWindowVisible(B) ? SW_HIDE : SW_SHOWNOACTIVATE);
    }
    printf("\n");
    /* same with a move of a visible window */
    ShowWindow(B, SW_SHOWNOACTIVATE); pump_ms(200);
    printf("move loop intervals (ms):");
    prev = now_ms();
    SetWindowPos(B, 0, 1000, 100, 0, 0, SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE);
    for (i = 0; i < 15; i++)
    {
        if (wait_mm(500) < 0) { printf(" timeout"); break; }
        t = now_ms(); printf(" %.1f", t - prev); prev = t;
        SetWindowPos(B, 0, 1000 + (i & 1), 100, 0, 0, SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE);
    }
    printf("\n");
}

static void perf(void)
{
    int i, n = 3000;
    DWORD t;
    HWND parent;
    A = mk(WS_EX_TOPMOST, "A", WS_POPUP | WS_VISIBLE, 100, 100, 600, 400, 0);
    pump_ms(300);
    parent = mk(0, "H", WS_POPUP, 1000, 100, 500, 500, 0);
    t = GetTickCount();
    for (i = 0; i < n; i++) mk(0, "c", WS_CHILD | WS_VISIBLE, i % 400, i % 300, 30, 30, parent);
    printf("perf: create %d visible children in hidden parent: %lu ms\n", n, GetTickCount() - t);
    memset(mm_count, 0, sizeof mm_count);
    pump_ms(300);
    printf("perf: A moves after that: %d\n", mm_count[0]);
    t = GetTickCount();
    ShowWindow(parent, SW_SHOWNOACTIVATE);
    printf("perf: show parent: %lu ms\n", GetTickCount() - t);
    pump_ms(300);
    B = mk(WS_EX_NOACTIVATE | WS_EX_TOPMOST, "B", WS_POPUP, 1000, 100, 50, 50, 0);
    t = GetTickCount();
    for (i = 0; i < n; i++) ShowWindow(B, (i & 1) ? SW_HIDE : SW_SHOWNOACTIVATE);
    printf("perf: %d show/hide of popup elsewhere (with %d windows): %lu ms\n", n, n, GetTickCount() - t);
    memset(mm_count, 0, sizeof mm_count);
    pump_ms(300);
    printf("perf: A moves after that: %d\n", mm_count[0]);
    DestroyWindow(parent);
}

int main(int argc, char **argv)
{
    WNDCLASSA wc = {0};
    const char *mode = argc > 1 ? argv[1] : "cases";

    setvbuf(stdout, NULL, _IONBF, 0);
    wc.lpfnWndProc = wndproc; wc.lpszClassName = "fmm2"; wc.hCursor = LoadCursorA(0, (LPCSTR)IDC_ARROW);
    wc.hbrBackground = GetStockObject(WHITE_BRUSH);
    RegisterClassA(&wc);
    SetCursorPos(400, 300);
    D = mk(WS_EX_NOACTIVATE | WS_EX_TOPMOST, "D", WS_POPUP, 900, 650, 50, 50, 0);
    pump_ms(300);
    if (!strcmp(mode, "loops")) loops();
    else if (!strcmp(mode, "menu")) menu();
    else if (!strcmp(mode, "perf")) perf();
    else if (!strcmp(mode, "lat")) lat();
    else if (!strcmp(mode, "combo")) combo();
    else cases();
    return 0;
}
