/* Control characters in GDI / user32 / Uniscribe text output (issue 039).
 * Inventor's combo box strings end in "\r"; Windows draws nothing for it, Wine drew a
 * .notdef box.
 *   ctrlchar_text.exe [FONT...]  probe table: per font and char, what each API does
 *   ctrlchar_text.exe show       window with "Pan\r|" via ExtTextOutW, DrawTextW and a
 *                                combobox/listbox; exits after 15 s (screenshot it)
 * Probe columns (c = the char, drawn/measured as "|c|" vs "||", 20px non-antialiased):
 *   gi      GetGlyphIndicesW flags 0 / GGI_MARK_NONEXISTING_GLYPHS (sp = same as space)
 *   ext     GetTextExtentPoint32W advance of c; cw GetCharWidth32W; abc GetCharABCWidthsW
 *   ETO TO  ExtTextOutW / TextOutW: advance of c in pixels, + extra ink pixels
 *   GCP     GetCharacterPlacementW glyph / dx, drawn with ETO_GLYPH_INDEX: adv+ink
 *   DS DN DE DrawTextW DT_SINGLELINE / |DT_NOPREFIX / |DT_EXPANDTABS: adv+ink;
 *   D0      DrawTextW flags 0 adv+ink (L = second bar on a new line)
 *   SS      ScriptStringAnalyse(SSA_GLYPHS)+ScriptStringOut adv+ink
 *   US      ScriptItemize/Shape/Place: script, glyph, fZeroWidth, advance
 * Build: x86_64-w64-mingw32-gcc -O2 -o ctrlchar_text.exe ctrlchar_text.c -lgdi32 -lusp10 -lshell32 */
#include <windows.h>
#include <usp10.h>
#include <stdio.h>

#define W 300
#define H 80
static DWORD *bits;
static HDC mdc;

static void clear(void) { memset(bits, 0xff, W * H * 4); }

/* first column of the rightmost ink run, ink pixel count; *line2 if ink spans two lines */
static void scan(int *bar2, int *ink, int *line2)
{
    int x, y, prev = 0, miny = H, maxy = -1;
    *bar2 = -1; *ink = 0;
    for (y = 0; y < H; y++)
        for (x = 0; x < W; x++)
            if ((bits[y * W + x] & 0xffffff) != 0xffffff) { ++*ink; if (y < miny) miny = y; maxy = y; }
    for (x = W - 1; x >= 0 && *bar2 < 0; x--)
    {
        int col = 0;
        for (y = 0; y < H; y++) if ((bits[y * W + x] & 0xffffff) != 0xffffff) col++;
        if (col) prev = 1;
        else if (prev) *bar2 = x + 1;
    }
    *line2 = maxy - miny > 30;
}

enum { M_ETO, M_TO, M_GCP, M_DS, M_DN, M_DE, M_D0, M_SS, M_COUNT };
static const char *mname[M_COUNT] = {"ETO", "TO", "GCP", "DS", "DN", "DE", "D0", "SS"};

static void draw(int m, const WCHAR *s, int n, char *info)
{
    RECT r = {5, 5, W, H};
    switch (m)
    {
    case M_ETO: ExtTextOutW(mdc, 5, 5, 0, NULL, s, n, NULL); break;
    case M_TO: TextOutW(mdc, 5, 5, s, n); break;
    case M_GCP:
    {
        WCHAR glyphs[8] = {0}; int dx[8] = {0}; GCP_RESULTSW res = {sizeof(res)};
        res.lpGlyphs = glyphs; res.lpDx = dx; res.nGlyphs = n;
        GetCharacterPlacementW(mdc, s, n, 0, &res, 0);
        if (info && n == 3) sprintf(info, "%04x/%d", glyphs[1], dx[1]);
        ExtTextOutW(mdc, 5, 5, ETO_GLYPH_INDEX, NULL, glyphs, res.nGlyphs, dx);
        break;
    }
    case M_DS: DrawTextW(mdc, s, n, &r, DT_SINGLELINE); break;
    case M_DN: DrawTextW(mdc, s, n, &r, DT_SINGLELINE | DT_NOPREFIX); break;
    case M_DE: DrawTextW(mdc, s, n, &r, DT_SINGLELINE | DT_EXPANDTABS); break;
    case M_D0: DrawTextW(mdc, s, n, &r, 0); break;
    case M_SS:
    {
        SCRIPT_STRING_ANALYSIS ssa;
        if (ScriptStringAnalyse(mdc, s, n, n * 2 + 16, -1, SSA_GLYPHS, 0, NULL, NULL, NULL, NULL, NULL, &ssa) == S_OK)
        {
            ScriptStringOut(ssa, 5, 5, 0, NULL, 0, 0, FALSE);
            ScriptStringFree(&ssa);
        }
        break;
    }
    }
}

static void probe_font(const WCHAR *face)
{
    static const WCHAR base[] = L"||";
    int m, base_bar[M_COUNT], base_ink[M_COUNT], l2, c;
    HFONT f = CreateFontW(-20, 0, 0, 0, 400, 0, 0, 0, DEFAULT_CHARSET, 0, 0, NONANTIALIASED_QUALITY, 0, face);
    WCHAR real[LF_FACESIZE]; TEXTMETRICW tm; SIZE sz0; WORD spgi;
    SelectObject(mdc, f);
    GetTextFaceW(mdc, LF_FACESIZE, real);
    GetTextMetricsW(mdc, &tm);
    GetGlyphIndicesW(mdc, L" ", 1, &spgi, 0);
    printf("\n=== %ls (got %ls) charset %d default char %04x break %04x, space gi %04x\n",
           face, real, tm.tmCharSet, tm.tmDefaultChar, tm.tmBreakChar, spgi);
    printf("ch   gi0  giM   ext cw  abc      | ");
    for (m = 0; m < M_COUNT; m++) printf("%-7s ", mname[m]);
    printf("| US\n");
    GetTextExtentPoint32W(mdc, base, 2, &sz0);
    for (m = 0; m < M_COUNT; m++) { clear(); draw(m, base, 2, NULL); scan(&base_bar[m], &base_ink[m], &l2); }
    for (c = 0; c <= 0xa0; c++)
    {
        WCHAR s[3] = {'|', c, '|'}, ch = c;
        WORD gi0, giM; SIZE sz; INT cw = -1; ABC abc = {0};
        char line[512], *p = line, gcp[32] = "";
        if (c > 0x20 && c < 0x7f) continue;
        GetGlyphIndicesW(mdc, &ch, 1, &gi0, 0);
        GetGlyphIndicesW(mdc, &ch, 1, &giM, GGI_MARK_NONEXISTING_GLYPHS);
        GetTextExtentPoint32W(mdc, s, 3, &sz);
        GetCharWidth32W(mdc, c, c, &cw);
        GetCharABCWidthsW(mdc, c, c, &abc);
        p += sprintf(p, "%02x ", c);
        if (gi0 == spgi) p += sprintf(p, "  sp "); else p += sprintf(p, "%4x ", gi0);
        if (giM == spgi) p += sprintf(p, "  sp "); else p += sprintf(p, "%4x ", giM);
        p += sprintf(p, "%4ld %2d %2d,%2u,%2d | ", sz.cx - sz0.cx, cw, abc.abcA, abc.abcB, abc.abcC);
        for (m = 0; m < M_COUNT; m++)
        {
            int bar, ink; char t[16];
            clear(); draw(m, s, 3, m == M_GCP ? gcp : NULL); scan(&bar, &ink, &l2);
            if (l2) sprintf(t, "L%+d", ink - base_ink[m]);
            else sprintf(t, "%d%+d", bar - base_bar[m], ink - base_ink[m]);
            p += sprintf(p, "%-7s ", t);
        }
        {
            SCRIPT_ITEM items[8]; int ni, ng, adv[8]; WORD glyphs[8], clust[8];
            SCRIPT_VISATTR va[8]; GOFFSET go[8]; ABC sabc; SCRIPT_CACHE sc = NULL;
            p += sprintf(p, "| ");
            if (ScriptItemize(&ch, 1, 8, NULL, NULL, items, &ni) == S_OK &&
                ScriptShape(mdc, &sc, &ch, 1, 8, &items[0].a, glyphs, clust, va, &ng) == S_OK &&
                ScriptPlace(mdc, &sc, glyphs, ng, va, &items[0].a, adv, go, &sabc) == S_OK)
                p += sprintf(p, "s%d %04x z%d %d", items[0].a.eScript, glyphs[0], va[0].fZeroWidth, adv[0]);
            else p += sprintf(p, "fail");
            ScriptFreeCache(&sc);
        }
        {
            GLYPHMETRICS gm; static const MAT2 id = {{0,1},{0,0},{0,0},{0,1}};
            int dx[3] = {10, 10, 10}, fit, bar, ink, pos[3]; RECT r = {0, 0, 0, 0}; DWORD ret;
            ret = GetGlyphOutlineW(mdc, c, GGO_METRICS, &gm, 0, NULL, &id);
            p += sprintf(p, "  gcp %s ggo %ld/%u,%u/%d", gcp, (long)ret, gm.gmBlackBoxX, gm.gmBlackBoxY, gm.gmCellIncX);
            clear(); ExtTextOutW(mdc, 5, 5, 0, NULL, s, 3, dx); scan(&bar, &ink, &l2);
            p += sprintf(p, " etodx ink%+d", ink - base_ink[M_ETO]);
            { SIZE sz2; GetTextExtentExPointW(mdc, s, 3, 1000, &fit, pos, &sz2); }
            p += sprintf(p, " exdx %d,%d,%d", pos[0], pos[1], pos[2]);
            DrawTextW(mdc, s, 3, &r, DT_CALCRECT | DT_SINGLELINE | DT_NOPREFIX);
            p += sprintf(p, " calc %ld", r.right);
        }
        printf("%s\n", line);
    }
    SelectObject(mdc, GetStockObject(SYSTEM_FONT));
    DeleteObject(f);
}

static const WCHAR s[] = L"Pan\r|";
static LRESULT CALLBACK proc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    if (m == WM_PAINT) {
        PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps); RECT r = {10, 40, 300, 60};
        HFONT f = CreateFontW(-13, 0, 0, 0, 400, 0, 0, 0, 0, 0, 0, 0, 0, L"Tahoma");
        SelectObject(dc, f);
        TextOutW(dc, 10, 10, L"ExtTextOutW:", 12); ExtTextOutW(dc, 120, 10, 0, NULL, s, 5, NULL);
        TextOutW(dc, 10, 40, L"DrawTextW SL:", 13); r.left = 120; DrawTextW(dc, s, 5, &r, DT_SINGLELINE);
        r.top = 70; r.bottom = 90; TextOutW(dc, 10, 70, L"DrawTextW:", 10); DrawTextW(dc, s, 5, &r, 0);
        EndPaint(h, &ps); DeleteObject(f); return 0;
    }
    if (m == WM_TIMER) PostQuitMessage(0);
    if (m == WM_DESTROY) PostQuitMessage(0);
    return DefWindowProcW(h, m, w, l);
}

static void show(void)
{
    HINSTANCE hi = GetModuleHandleW(NULL);
    WNDCLASSW wc = {0, proc, 0, 0, hi, 0, LoadCursor(0, IDC_ARROW), (HBRUSH)(COLOR_WINDOW + 1), 0, L"crtext"};
    HWND h, cb, lb; MSG msg;
    HFONT f = CreateFontW(-13, 0, 0, 0, 400, 0, 0, 0, 0, 0, 0, 0, 0, L"Tahoma");
    RegisterClassW(&wc);
    h = CreateWindowW(L"crtext", L"crtext", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 0, 0, 400, 260, 0, 0, hi, 0);
    cb = CreateWindowW(L"ComboBox", 0, WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST, 10, 100, 150, 200, h, 0, hi, 0);
    lb = CreateWindowW(L"ListBox", 0, WS_CHILD | WS_VISIBLE | WS_BORDER, 180, 100, 150, 60, h, 0, hi, 0);
    SendMessageW(cb, WM_SETFONT, (WPARAM)f, 0); SendMessageW(lb, WM_SETFONT, (WPARAM)f, 0);
    SendMessageW(cb, CB_ADDSTRING, 0, (LPARAM)L"Pan\r"); SendMessageW(cb, CB_SETCURSEL, 0, 0);
    SendMessageW(lb, LB_ADDSTRING, 0, (LPARAM)L"Pan\r"); SendMessageW(lb, LB_ADDSTRING, 0, (LPARAM)L"Orbit\r");
    SetTimer(h, 1, 15000, 0);
    while (GetMessageW(&msg, 0, 0, 0)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
}

int main(void)
{
    static const WCHAR *fonts[] = {L"Tahoma", L"Segoe UI", L"Arial", L"Courier New",
                                   L"Microsoft Sans Serif", L"MS Sans Serif", L"Wingdings", L"Marlett"};
    BITMAPINFO bi = {{sizeof(bi.bmiHeader), W, -H, 1, 32, BI_RGB}};
    int argc, i; WCHAR **argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argc > 1 && !wcscmp(argv[1], L"show")) { show(); return 0; }
    mdc = CreateCompatibleDC(0);
    SelectObject(mdc, CreateDIBSection(mdc, &bi, DIB_RGB_COLORS, (void **)&bits, NULL, 0));
    SetBkMode(mdc, TRANSPARENT);
    if (argc > 1) for (i = 1; i < argc; i++) probe_font(argv[i]);
    else for (i = 0; i < ARRAYSIZE(fonts); i++) probe_font(fonts[i]);
    return 0;
}
