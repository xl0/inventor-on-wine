/* Probe the per-pixel-alpha popups of a process (Inventor's WPF splitter, 062).
 *   layered_popup_probe.exe [TITLE]        list visible WS_EX_LAYERED popups of the process owning the
 *                                          top-level window whose title contains TITLE (default "Autodesk Inventor"),
 *                                          with owner, rects and what the screen shows there (GDI screen grab)
 *   layered_popup_probe.exe [TITLE] drag [X]  also restore the main window, drag it by an HTCAPTION point with
 *                                          SendInput (X: caption point, window-relative; 30 steps x 8 px, 16 ms) and print the popups' rects per step;
 *                                          re-maximizes if it was maximized
 *   layered_popup_probe.exe TITLE mdrag X Y DX   left-button drag at screen X,Y by DX, then list
 *   layered_popup_probe.exe TITLE hit            WindowFromPoint every 16 px down each popup ('#' = popup, '.' = main window tree, '?' = other)
 *   layered_popup_probe.exe TITLE alpha          each popup's composited alpha/colour (grabbed over own white and black windows)
 *   layered_popup_probe.exe TITLE max            maximize the main window
 * Build: x86_64-w64-mingw32-gcc -O2 -o layered_popup_probe.exe layered_popup_probe.c -lgdi32 */
#include <windows.h>
#include <stdio.h>

static HWND main_hwnd, popups[16];
static int npopups;
static const char *title = "Autodesk Inventor";

static BOOL CALLBACK find_main(HWND h, LPARAM unused)
{
    char text[256];
    if (!IsWindowVisible(h) || GetWindow(h, GW_OWNER)) return TRUE;
    GetWindowTextA(h, text, sizeof(text));
    if (!strstr(text, title)) return TRUE;
    main_hwnd = h;
    return FALSE;
}

static BOOL CALLBACK find_popups(HWND h, LPARAM pid)
{
    DWORD p;
    GetWindowThreadProcessId(h, &p);
    if (p == pid && IsWindowVisible(h) && (GetWindowLongA(h, GWL_EXSTYLE) & WS_EX_LAYERED) && npopups < 16)
        popups[npopups++] = h;
    return TRUE;
}

/* mean colour of a screen rect */
static void screen_mean(const RECT *r, char *buf)
{
    int w = r->right - r->left, h = r->bottom - r->top, i;
    BITMAPINFO bi = {{sizeof(bi.bmiHeader), w, -h, 1, 32, BI_RGB}};
    double s[3] = {0};
    DWORD *bits;
    HDC screen = GetDC(0), mem = CreateCompatibleDC(screen);
    HBITMAP bmp = CreateDIBSection(mem, &bi, DIB_RGB_COLORS, (void **)&bits, NULL, 0);
    if (w <= 0 || h <= 0) { strcpy(buf, "empty"); return; }
    SelectObject(mem, bmp);
    BitBlt(mem, 0, 0, w, h, screen, r->left, r->top, SRCCOPY | CAPTUREBLT);
    for (i = 0; i < w * h; i++)
    {
        s[0] += (bits[i] >> 16) & 0xff;
        s[1] += (bits[i] >> 8) & 0xff;
        s[2] += bits[i] & 0xff;
    }
    sprintf(buf, "rgb(%.0f,%.0f,%.0f)", s[0] / (w * h), s[1] / (w * h), s[2] / (w * h));
    DeleteDC(mem);
    DeleteObject(bmp);
    ReleaseDC(0, screen);
}

static void list(void)
{
    char cls[128], mean[64], left[64], right[64];
    BYTE alpha;
    COLORREF key;
    DWORD flags;
    RECT r, l, rr;
    int i;
    for (i = 0; i < npopups; i++)
    {
        HWND h = popups[i];
        GetClassNameA(h, cls, sizeof(cls));
        GetWindowRect(h, &r);
        l = r; l.left -= 4; l.right = r.left;
        rr = r; rr.left = r.right; rr.right = r.right + 4;
        screen_mean(&r, mean);
        screen_mean(&l, left);
        screen_mean(&rr, right);
        if (!GetLayeredWindowAttributes(h, &key, &alpha, &flags)) flags = 0xdead;
        printf("popup %p %s %ldx%ld+%ld+%ld style %08lx ex %08lx owner %p lwa %lx | screen %s, left %s, right %s\n",
               h, cls, r.right - r.left, r.bottom - r.top, r.left, r.top, GetWindowLongA(h, GWL_STYLE),
               GetWindowLongA(h, GWL_EXSTYLE), GetWindow(h, GW_OWNER), flags, mean, left, right);
    }
}

static void mouse(DWORD flags, int x, int y)
{
    INPUT in = {INPUT_MOUSE};
    in.mi.dx = x * 65535 / (GetSystemMetrics(SM_CXSCREEN) - 1);
    in.mi.dy = y * 65535 / (GetSystemMetrics(SM_CYSCREEN) - 1);
    in.mi.dwFlags = flags | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_MOVE;
    SendInput(1, &in, sizeof(in));
}

int main(int argc, char **argv)
{
    DWORD pid;
    RECT r, pr;
    BOOL maxed;
    int i, j, x = -1, y;

    if (argc > 1) title = argv[1];
    ShowWindow(GetConsoleWindow(), SW_HIDE);  /* winrun's console can cover the probed windows */
    EnumWindows(find_main, 0);
    if (!main_hwnd) { printf("no window '%s'\n", title); return 1; }
    GetWindowThreadProcessId(main_hwnd, &pid);
    GetWindowRect(main_hwnd, &r);
    printf("main %p %ldx%ld+%ld+%ld%s\n", main_hwnd, r.right - r.left, r.bottom - r.top, r.left, r.top,
           IsZoomed(main_hwnd) ? " maximized" : "");
    EnumWindows(find_popups, pid);
    list();
    if (argc > 5 && !strcmp(argv[2], "mdrag"))  /* mdrag X Y DX: drag with the left button at screen X,Y by DX */
    {
        x = atoi(argv[3]); y = atoi(argv[4]);
        CURSORINFO ci = {sizeof(ci)};
        mouse(0, x, y); Sleep(300);
        GetCursorInfo(&ci);
        printf("cursor at %d,%d: %p (arrow %p, sizewe %p), window %p\n", x, y, ci.hCursor, LoadCursorA(0, (LPCSTR)IDC_ARROW),
               LoadCursorA(0, (LPCSTR)IDC_SIZEWE), WindowFromPoint((POINT){x, y}));
        mouse(MOUSEEVENTF_LEFTDOWN, x, y); Sleep(200);
        for (i = 1; i <= 10; i++) { mouse(0, x + atoi(argv[5]) * i / 10, y); Sleep(30); }
        mouse(MOUSEEVENTF_LEFTUP, x + atoi(argv[5]), y); Sleep(1000);
        npopups = 0;
        EnumWindows(find_popups, pid);
        list();
        return 0;
    }
    if (argc > 2 && !strcmp(argv[2], "hit"))  /* WindowFromPoint along the middle column of each popup */
    {
        for (i = 0; i < npopups; i++)
        {
            GetWindowRect(popups[i], &pr);
            printf("popup %p:", popups[i]);
            for (y = pr.top; y < pr.bottom; y += 16)
            {
                HWND h = WindowFromPoint((POINT){(pr.left + pr.right) / 2, y});
                printf("%c", h == popups[i] ? '#' : GetAncestor(h, GA_ROOT) == main_hwnd ? '.' : '?');
            }
            printf("\n");
        }
        return 0;
    }
    if (argc > 2 && !strcmp(argv[2], "alpha"))  /* composited alpha: grab each popup over a white and a black window */
    {
        for (i = 0; i < npopups; i++)
        {
            static const DWORD fill[2] = {WHITE_BRUSH, BLACK_BRUSH};
            int w, h, k;
            DWORD *bits[2];
            HWND under;
            GetWindowRect(popups[i], &pr);
            w = pr.right - pr.left; h = pr.bottom - pr.top;
            WNDCLASSA cls = {0, DefWindowProcA, 0, 0, GetModuleHandleA(0), 0, 0, 0, 0, "under"};
            RegisterClassA(&cls);
            under = CreateWindowExA(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, "under", "", WS_POPUP, pr.left, pr.top, w, h,
                                   GetWindow(popups[i], GW_OWNER), 0, 0, 0);  /* owned: stays above the owner, under the popup */
            SetWindowPos(under, popups[i], 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
            for (k = 0; k < 2; k++)
            {
                BITMAPINFO bi = {{sizeof(bi.bmiHeader), w, -h, 1, 32, BI_RGB}};
                HDC screen, mem, dc = GetDC(under);
                RECT all = {0, 0, w, h};
                MSG msg;
                FillRect(dc, &all, GetStockObject(fill[k]));
                ReleaseDC(under, dc);
                GdiFlush(); Sleep(300);
                while (PeekMessageA(&msg, 0, 0, 0, PM_REMOVE)) DispatchMessageA(&msg);
                screen = GetDC(0); mem = CreateCompatibleDC(screen);
                SelectObject(mem, CreateDIBSection(mem, &bi, DIB_RGB_COLORS, (void **)&bits[k], NULL, 0));
                BitBlt(mem, 0, 0, w, h, screen, pr.left, pr.top, SRCCOPY | CAPTUREBLT);  /* CAPTUREBLT: include layered windows */
                ReleaseDC(0, screen);
            }
            DestroyWindow(under);
            printf("popup %p %dx%d, per 16-row band: alpha min-max, premultiplied colour of the middle column\n", popups[i], w, h);
            for (y = 0; y < h; y += 16)
            {
                int amin = 255, amax = 0, yy, xx, mid = (y + min(y + 16, h)) / 2 * w + w / 2;
                for (yy = y; yy < min(y + 16, h); yy++)
                    for (xx = 0; xx < w; xx++)
                    {
                        int a = 255 - (int)(((bits[0][yy * w + xx] >> 8) & 0xff) - ((bits[1][yy * w + xx] >> 8) & 0xff));
                        amin = min(amin, a); amax = max(amax, a);
                    }
                printf("  y %3d: a %3d-%3d c %06lx\n", y, amin, amax, bits[1][mid] & 0xffffff);
            }
        }
        return 0;
    }
    if (argc > 2 && !strcmp(argv[2], "max")) { ShowWindow(main_hwnd, SW_MAXIMIZE); return 0; }
    if (argc < 3 || strcmp(argv[2], "drag")) return 0;

    if ((maxed = IsZoomed(main_hwnd)))
    {
        ShowWindow(main_hwnd, SW_RESTORE);
        Sleep(1500);
    }
    SetForegroundWindow(main_hwnd);
    Sleep(500);
    GetWindowRect(main_hwnd, &r);
    npopups = 0;
    EnumWindows(find_popups, pid);
    printf("restored main %ldx%ld+%ld+%ld\n", r.right - r.left, r.bottom - r.top, r.left, r.top);
    list();
    /* rightmost caption point left of the caption buttons */
    y = r.top + 12;
    for (i = r.right - 200; i > r.left; i -= 4)
        if (SendMessageA(main_hwnd, WM_NCHITTEST, 0, MAKELPARAM(i, y)) == HTCAPTION) { x = i; break; }
    if (argc > 3) x = r.left + atoi(argv[3]);
    if (x < 0) x = r.left + (r.right - r.left) * 62 / 100;  /* app-drawn caption: empty strip like tools/uilat */
    printf("drag from %d,%d (hittest %Id)\n", x, y, SendMessageA(main_hwnd, WM_NCHITTEST, 0, MAKELPARAM(x, y)));
    mouse(0, x, y); Sleep(200);
    mouse(MOUSEEVENTF_LEFTDOWN, x, y); Sleep(200);
    for (i = 1; i <= 30; i++)
    {
        mouse(0, x + 8 * i, y);
        Sleep(16);
        GetWindowRect(main_hwnd, &r);
        printf("step %2d main x %ld", i, r.left);
        for (j = 0; j < npopups; j++)
        {
            GetWindowRect(popups[j], &pr);
            printf(" | popup %d x %ld (rel %ld)", j, pr.left, pr.left - r.left);
        }
        printf("\n");
    }
    mouse(MOUSEEVENTF_LEFTUP, x + 240, y);
    Sleep(500);
    GetWindowRect(main_hwnd, &r);
    printf("after main %ldx%ld+%ld+%ld\n", r.right - r.left, r.bottom - r.top, r.left, r.top);
    list();
    /* drag back */
    mouse(0, x + 240, y); mouse(MOUSEEVENTF_LEFTDOWN, x + 240, y); Sleep(100);
    for (i = 1; i <= 10; i++) { mouse(0, x + 240 - 24 * i, y); Sleep(16); }
    mouse(MOUSEEVENTF_LEFTUP, x, y);
    Sleep(300);
    if (maxed) ShowWindow(main_hwnd, SW_MAXIMIZE);
    return 0;
}
