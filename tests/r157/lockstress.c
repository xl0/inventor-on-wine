/* winewayland lock-order stress (issue 157): several threads change window state at once.
 * Build: x86_64-w64-mingw32-gcc -O1 -o lockstress.exe lockstress.c -lopengl32 -lgdi32 -luser32
 *   lockstress N [SEED] [nogl] [noxulw]   N operations per thread; prints "DONE" when every thread finished
 *                                  noxulw: no UpdateLayeredWindow on a window of another thread (win32u race, issue 171)
 *   lockstress glhide N [HIDE_MS]  one thread hides/shows a toplevel N times, another swaps on its GL child;
 *                                  the window stays hidden HIDE_MS (default: 0, 1 or 2 ms) (issue 170)
 * Threads: main (A main window, D owned popup, F popup flipping managed/unmanaged with a GL child G,
 * L layered popup, C child <-> toplevel), two (B main, E popup owned by A), gl (SwapBuffers on G),
 * poke (style/text/layered/position changes and painting on the other threads' windows),
 * spawn (short-lived threads with a popup owned by A: destroyed, or left to the thread exit).
 * A hang is a result: run it under a watchdog. */
#include <windows.h>
#include <GL/gl.h>
#include <stdio.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#define NOZ (SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE)

static HWND A, D, F, L, C, G, B, E;
static int N, gl_swaps, use_gl = 1, cross_ulw = 1;
static volatile LONG running, ops, stop;
static DWORD t0;

static void lg(const char *fmt, ...)
{
    va_list ap; va_start(ap, fmt);
    printf("%6lu ", (unsigned long)(GetTickCount() - t0)); vprintf(fmt, ap); printf("\n"); fflush(stdout);
    va_end(ap);
}

static unsigned rnd(unsigned *seed) { *seed = *seed * 1103515245 + 12345; return (*seed >> 16) & 0x7fff; }

static LRESULT CALLBACK wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_CLOSE) return 0;  /* the compositor's close button must not end the run */
    /* injected clicks and keys (jitter.py) must not start a modal menu or move / size loop */
    if (msg == WM_NCLBUTTONDOWN || msg == WM_NCRBUTTONDOWN || msg == WM_NCLBUTTONDBLCLK) return 0;
    if (msg == WM_SYSCOMMAND && ((wp & 0xfff0) == SC_MOUSEMENU || (wp & 0xfff0) == SC_KEYMENU ||
                                 (wp & 0xfff0) == SC_MOVE || (wp & 0xfff0) == SC_SIZE)) return 0;
    return DefWindowProcA(hwnd, msg, wp, lp);
}

static HWND mk(const char *title, COLORREF colour, DWORD style, DWORD ex, int x, int y, int w, int h, HWND parent)
{
    WNDCLASSA wc = {0}; char cls[32];
    sprintf(cls, "ls_%06lx", (unsigned long)colour);
    wc.lpfnWndProc = wndproc; wc.lpszClassName = cls; wc.hbrBackground = CreateSolidBrush(colour);
    wc.hCursor = LoadCursorA(NULL, (const char *)IDC_ARROW); wc.style = CS_OWNDC;
    RegisterClassA(&wc);
    return CreateWindowExA(ex, cls, title, style, x, y, w, h, parent, NULL, NULL, NULL);
}

static void pump(DWORD ms)
{
    MSG msg; DWORD end = GetTickCount() + ms;
    for (;;)
    {
        while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageA(&msg); }
        if ((int)(end - GetTickCount()) <= 0) break;
        Sleep(1);
    }
}

static void paint(HWND hwnd, unsigned r)
{
    HDC dc = GetDC(hwnd); RECT rc = {r % 50, r % 40, 60 + r % 90, 50 + r % 70};
    HBRUSH br = CreateSolidBrush(RGB(r & 255, (r >> 3) & 255, (r >> 6) & 255));
    FillRect(dc, &rc, br); DeleteObject(br); ReleaseDC(hwnd, dc);
}

static void update_layered(HWND hwnd, unsigned r)
{
    BITMAPINFO bi = {{sizeof(BITMAPINFOHEADER), 64, 64, 1, 32, BI_RGB}};
    BLENDFUNCTION bf = {AC_SRC_OVER, 0, 128 + r % 100, AC_SRC_ALPHA};
    POINT pt = {0, 0}; SIZE sz = {64, 64}; DWORD *bits; int i;
    HDC dc = CreateCompatibleDC(0); HBITMAP bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, (void **)&bits, NULL, 0);
    for (i = 0; i < 64 * 64; i++) bits[i] = 0x80004000 | (r & 0x7f);
    SelectObject(dc, bmp);
    UpdateLayeredWindow(hwnd, NULL, NULL, &sz, dc, &pt, 0, &bf, ULW_ALPHA);
    DeleteDC(dc); DeleteObject(bmp);
}

/* operations on a window of the calling thread */
static void own_op(HWND hwnd, HWND owner_a, HWND owner_b, unsigned r)
{
    char text[32];
    switch (r % 12)
    {
    case 0: ShowWindow(hwnd, SW_HIDE); break;
    case 1: ShowWindow(hwnd, SW_SHOWNA); break;
    case 2: ShowWindow(hwnd, SW_SHOW); break;
    case 3: ShowWindow(hwnd, SW_MINIMIZE); break;
    case 4: ShowWindow(hwnd, SW_RESTORE); break;
    case 5: sprintf(text, "title %u", r); SetWindowTextA(hwnd, text); break;
    case 6: SetWindowPos(hwnd, 0, 60 + r % 200, 60 + r % 150, 200 + r % 200, 150 + r % 100, SWP_NOZORDER | SWP_NOACTIVATE); break;
    case 7: SetWindowLongA(hwnd, GWL_STYLE, GetWindowLongA(hwnd, GWL_STYLE) ^ WS_THICKFRAME);
            SetWindowPos(hwnd, 0, 0, 0, 0, 0, NOZ | SWP_FRAMECHANGED); break;
    case 8: if (owner_a) { SetWindowLongPtrA(hwnd, GWLP_HWNDPARENT, (LONG_PTR)((r & 16) ? owner_a : owner_b));
                           SetWindowPos(hwnd, 0, 0, 0, 0, 0, NOZ); } break;
    case 9: InvalidateRect(hwnd, NULL, TRUE); UpdateWindow(hwnd); break;
    case 10: paint(hwnd, r); break;
    case 11: ShowWindow(hwnd, (r & 16) ? SW_MAXIMIZE : SW_RESTORE); break;
    }
}

static DWORD WINAPI two_proc(void *arg)
{
    unsigned seed = (unsigned)(ULONG_PTR)arg; int i;
    B = mk("B main (thread two)", RGB(255, 255, 0), WS_OVERLAPPEDWINDOW | WS_VISIBLE, 0, 500, 100, 400, 300, NULL);
    E = mk("E owned by A (thread two)", RGB(0, 255, 255), WS_POPUP | WS_CAPTION | WS_VISIBLE, 0, 550, 200, 300, 200, A);
    for (i = 0; i < N; i++)
    {
        unsigned r = rnd(&seed);
        own_op((r & 32) ? B : E, (r & 32) ? NULL : A, B, r >> 6);
        InterlockedIncrement(&ops);
        if (!(i % 5)) pump(i % 20 ? 0 : 5);
    }
    InterlockedDecrement(&running);
    while (!stop) pump(10);
    return 0;
}

static DWORD WINAPI gl_proc(void *arg)
{
    PIXELFORMATDESCRIPTOR pfd = {sizeof(pfd), 1, PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER, PFD_TYPE_RGBA, 32};
    HDC dc = GetDC(G); HGLRC rc; int i, fmt = ChoosePixelFormat(dc, &pfd); BOOL (WINAPI *swap_interval)(int);
    if (!fmt || !SetPixelFormat(dc, fmt, &pfd) || !(rc = wglCreateContext(dc)) || !wglMakeCurrent(dc, rc))
    {
        lg("gl: no context (err %lu), thread idle", GetLastError());
        InterlockedDecrement(&running);
        return 0;
    }
    /* with an interval the swap waits for a frame callback, forever while F is hidden or minimized (issue 163) */
    if ((swap_interval = (void *)wglGetProcAddress("wglSwapIntervalEXT"))) swap_interval(0);
    for (i = 0; i < gl_swaps && !stop; i++)
    {
        glClearColor((i & 1) ? 1.0f : 0.0f, 0.5f, (i & 2) ? 1.0f : 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        SwapBuffers(dc);
        InterlockedIncrement(&ops);
        if (!(i % 16)) Sleep(1);
    }
    wglMakeCurrent(NULL, NULL); wglDeleteContext(rc);
    InterlockedDecrement(&running);
    return 0;
}

/* operations on windows of other threads */
static DWORD WINAPI poke_proc(void *arg)
{
    unsigned seed = (unsigned)(ULONG_PTR)arg; int i; char text[32]; DWORD_PTR res;
    for (i = 0; i < N; i++)
    {
        HWND all[] = {A, D, F, L, C, B, E};
        unsigned r = rnd(&seed); HWND hwnd = all[(r >> 4) % 7];
        switch (r % 12)
        {
        case 0: SetWindowLongA(hwnd, GWL_EXSTYLE, GetWindowLongA(hwnd, GWL_EXSTYLE) ^ WS_EX_APPWINDOW); break;
        case 1: sprintf(text, "poked %u", r); SendMessageTimeoutA(hwnd, WM_SETTEXT, 0, (LPARAM)text, SMTO_NORMAL, 200, &res); break;
        case 2: sprintf(text, "direct %u", r); DefWindowProcA(hwnd, WM_SETTEXT, 0, (LPARAM)text); break;
        case 3: ShowWindowAsync(hwnd, (r & 512) ? SW_HIDE : SW_SHOWNA); break;
        case 4: SetLayeredWindowAttributes(L, 0, 100 + r % 150, LWA_ALPHA); break;
        case 5: if (cross_ulw) update_layered(L, r); break;
        case 6: PostMessageA(hwnd, WM_SYSCOMMAND, (r & 512) ? SC_MINIMIZE : SC_RESTORE, 0); break;
        case 7: paint(hwnd, r); break;
        case 8: RedrawWindow(hwnd, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN); break;
        case 9: SetWindowLongA(L, GWL_EXSTYLE, GetWindowLongA(L, GWL_EXSTYLE) ^ WS_EX_TRANSPARENT); break;
        case 10: SetWindowLongA(L, GWL_EXSTYLE, GetWindowLongA(L, GWL_EXSTYLE) ^ WS_EX_LAYERED); break;
        case 11: GetWindowTextA(hwnd, text, sizeof(text)); IsWindowVisible(hwnd); GetWindow(hwnd, GW_OWNER); break;
        }
        InterlockedIncrement(&ops);
        if (!(i % 8)) Sleep(1);
    }
    InterlockedDecrement(&running);
    return 0;
}

static DWORD WINAPI short_proc(void *arg)
{
    unsigned r = (unsigned)(ULONG_PTR)arg;
    HWND p = mk("P short-lived", RGB(255, 0, 255), WS_POPUP | WS_CAPTION | WS_VISIBLE, 0, 200 + r % 300, 150 + r % 200, 200, 120, A);
    HWND q = mk("Q owned by P", RGB(128, 0, 255), WS_POPUP | WS_CAPTION | WS_VISIBLE, 0, 220 + r % 300, 170 + r % 200, 120, 80, p);
    pump(r % 12);
    if (r & 1) DestroyWindow(q);
    if (r & 2) DestroyWindow(p);
    return 0;   /* the rest goes with the thread */
}

static DWORD WINAPI spawn_proc(void *arg)
{
    unsigned seed = (unsigned)(ULONG_PTR)arg; int i;
    for (i = 0; i < N / 10; i++)
    {
        HANDLE th = CreateThread(NULL, 0, short_proc, (void *)(ULONG_PTR)rnd(&seed), 0, NULL);
        WaitForSingleObject(th, INFINITE); CloseHandle(th);
        InterlockedIncrement(&ops);
    }
    InterlockedDecrement(&running);
    return 0;
}

int main(int argc, char **argv)
{
    unsigned seed = argc > 2 ? atoi(argv[2]) : 1, s; int i; HANDLE th[4]; DWORD last = 0;

    t0 = GetTickCount();
    if (argc > 2 && !strcmp(argv[1], "glhide"))
    {
        /* one UI thread hiding and showing a toplevel while another thread swaps on its GL child (issue 170) */
        int hide_ms = argc > 3 ? atoi(argv[3]) : -1;   /* time the window stays hidden; default 0..2 ms */
        N = atoi(argv[2]);
        gl_swaps = INT_MAX;   /* until the main thread is done */
        F = mk("F hide/show", RGB(0, 0, 255), WS_OVERLAPPEDWINDOW | WS_VISIBLE | WS_CLIPCHILDREN, 0, 160, 180, 260, 180, NULL);
        G = mk("G gl child", RGB(64, 64, 64), WS_CHILD | WS_VISIBLE, 0, 20, 40, 120, 90, F);
        pump(1000);
        running = 1;
        th[0] = CreateThread(NULL, 0, gl_proc, NULL, 0, NULL);
        for (i = 0; i < N && running; i++)
        {
            ShowWindow(F, SW_HIDE); pump(hide_ms < 0 ? i % 3 : hide_ms);
            ShowWindow(F, SW_SHOWNA); pump(i % 5);
        }
        stop = 1;
        WaitForSingleObject(th[0], 5000);   /* a thread killed in a swap can hang the process exit */
        lg("DONE %d hide/show, %ld swaps", i, ops);
        return 0;
    }
    gl_swaps = N = argc > 1 ? atoi(argv[1]) : 300;
    for (i = 3; i < argc; i++)
    {
        if (!strcmp(argv[i], "nogl")) use_gl = 0;
        if (!strcmp(argv[i], "noxulw")) cross_ulw = 0;
    }
    A = mk("A main", RGB(255, 0, 0), WS_OVERLAPPEDWINDOW | WS_VISIBLE, 0, 60, 60, 420, 320, NULL);
    D = mk("D owned by A", RGB(0, 255, 0), WS_POPUP | WS_CAPTION | WS_VISIBLE, 0, 120, 140, 300, 200, A);
    F = mk("F flips", RGB(0, 0, 255), WS_POPUP | WS_VISIBLE | WS_CLIPCHILDREN, 0, 160, 180, 260, 180, A);
    G = mk("G gl child", RGB(64, 64, 64), WS_CHILD | WS_VISIBLE, 0, 20, 40, 120, 90, F);
    L = mk("L layered", RGB(255, 128, 0), WS_POPUP | WS_VISIBLE, WS_EX_LAYERED, 400, 380, 64, 64, A);
    C = mk("C child or toplevel", RGB(128, 128, 255), WS_CHILD | WS_VISIBLE, 0, 10, 10, 150, 100, A);
    SetLayeredWindowAttributes(L, 0, 200, LWA_ALPHA);
    pump(1500);
    lg("A=%p D=%p F=%p G=%p L=%p C=%p, %d ops per thread, seed %u", A, D, F, G, L, C, N, seed);

    running = use_gl ? 4 : 3;
    th[0] = CreateThread(NULL, 0, two_proc, (void *)(ULONG_PTR)(seed * 7 + 1), 0, NULL);
    while (!E) Sleep(1);
    th[1] = CreateThread(NULL, 0, poke_proc, (void *)(ULONG_PTR)(seed * 7 + 2), 0, NULL);
    th[2] = CreateThread(NULL, 0, spawn_proc, (void *)(ULONG_PTR)(seed * 7 + 3), 0, NULL);
    th[3] = use_gl ? CreateThread(NULL, 0, gl_proc, NULL, 0, NULL) : NULL;

    s = seed * 7;
    for (i = 0; i < N; i++)
    {
        unsigned r = rnd(&s);
        switch (r % 8)
        {
        case 0: case 1: own_op((r & 64) ? A : D, (r & 64) ? NULL : A, B, r >> 7); break;
        case 2: /* managed <-> unmanaged: the surface changes its role, the GL child has to follow */
            SetWindowLongA(F, GWL_STYLE, GetWindowLongA(F, GWL_STYLE) ^ WS_CAPTION);
            SetWindowPos(F, 0, 0, 0, 0, 0, NOZ | SWP_FRAMECHANGED); break;
        case 3: own_op(F, A, D, r >> 7); break;
        case 4: /* child <-> toplevel */
            if (GetWindowLongA(C, GWL_STYLE) & WS_CHILD)
            {
                SetParent(C, NULL);
                SetWindowLongA(C, GWL_STYLE, (GetWindowLongA(C, GWL_STYLE) & ~WS_CHILD) | WS_POPUP | WS_CAPTION);
                SetWindowPos(C, 0, 300, 300, 200, 150, SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
            }
            else
            {
                SetWindowLongA(C, GWL_STYLE, (GetWindowLongA(C, GWL_STYLE) & ~(WS_POPUP | WS_CAPTION)) | WS_CHILD);
                SetParent(C, (r & 64) ? A : F);
                SetWindowPos(C, 0, 10, 10, 150, 100, SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
            }
            break;
        case 5: own_op(L, A, D, r >> 7); break;
        case 6: if (r & 64) update_layered(L, r); else SetLayeredWindowAttributes(L, 0, 80 + (r >> 7) % 170, LWA_ALPHA); break;
        case 7: own_op(C, NULL, NULL, r >> 7); break;
        }
        InterlockedIncrement(&ops);
        if (!(i % 5)) pump(i % 20 ? 0 : 5);
        if (GetTickCount() - last > 2000) { last = GetTickCount(); lg("main %d/%d, %ld ops, %ld threads running", i, N, ops, running); }
    }
    lg("main loop done");
    while (running > 0) pump(10);
    stop = 1;
    WaitForSingleObject(th[0], 5000);
    pump(500);
    lg("DONE %ld ops", ops);
    return 0;
}
