/* 125: rich edit probes (RICHEDIT50W from msftedit.dll and RichEdit20W from riched20.dll):
 *   tom   ITextFont on a range: units of SetSize/SetPosition/..., Reset(tomApplyNow / tomApplyLater / tomApplyTmp)
 *   bind  font binding of CJK text inserted with a Latin font, per insertion method and EM_SETLANGOPTIONS,
 *         per char EM_GETCHARFORMAT, x positions, "boxes" check on the drawn pixels, stream-out RTF; BMPs in cwd
 *   wrap  EM_SETTARGETDEVICE(screen DC, twips): line breaks of CJK / Latin text
 *   sel   selections reaching the final paragraph mark: what is left selected after deleting them
 *   eop   EM_SETCHARFORMAT(SCF_SELECTION) at an insertion point: format of the final paragraph mark
 * re_probe.exe [tom|bind|wrap|sel|eop]...   (default: all)
 * x86_64-w64-mingw32-gcc -O2 -o re_probe.exe re_probe.c -lgdi32 -luser32 -lole32 -loleaut32 -luuid */
#define COBJMACROS
#include <windows.h>
#include <richedit.h>
#include <richole.h>
#include <tom.h>
#include <stdio.h>

static const GUID iid_ITextDocument = {0x8cc497c0,0xa1df,0x11ce,{0x80,0x98,0x00,0xaa,0x00,0x47,0xbe,0x5d}};
static const WCHAR cjk[] = {0x4e2d,0x6587,0x6d4b,0x8bd5,0};
static const WCHAR *cls_name;

static HWND new_edit(void)
{
    HWND hwnd = CreateWindowExW(0, cls_name, L"", WS_POPUP | WS_VISIBLE | WS_BORDER | ES_MULTILINE,
                                20, 20, 600, 120, NULL, NULL, GetModuleHandleW(NULL), NULL);
    if (!hwnd) { printf("no window for class %ls (%lu)\n", cls_name, GetLastError()); exit(1); }
    SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
    return hwnd;
}

static void set_font(HWND hwnd, WPARAM scf, const WCHAR *face, BYTE charset, LONG height)
{
    CHARFORMAT2W cf = { sizeof(cf) };
    cf.dwMask = CFM_FACE | CFM_CHARSET | CFM_SIZE | CFM_BOLD | CFM_ITALIC | CFM_UNDERLINE;
    cf.yHeight = height; cf.bCharSet = charset;
    lstrcpyW(cf.szFaceName, face);
    SendMessageW(hwnd, EM_SETCHARFORMAT, scf, (LPARAM)&cf);
}

static void get_cf(HWND hwnd, int from, int to, CHARFORMAT2W *cf)
{
    memset(cf, 0, sizeof(*cf)); cf->cbSize = sizeof(*cf);
    SendMessageW(hwnd, EM_SETSEL, from, to);
    SendMessageW(hwnd, EM_GETCHARFORMAT, SCF_SELECTION, (LPARAM)cf);
}

static void print_cf(HWND hwnd, const char *what, int from, int to)
{
    CHARFORMAT2W cf;
    get_cf(hwnd, from, to, &cf);
    printf("  %-34s [%d,%d) mask %08lx eff %08lx face '%ls' cs %u height %ld off %ld kern %d spacing %d ul %u\n", what, from, to,
           cf.dwMask, cf.dwEffects, cf.szFaceName, cf.bCharSet, cf.yHeight, cf.yOffset, cf.wKerning, cf.sSpacing, cf.bUnderlineType);
}

static ITextFont *range_font(HWND hwnd, int from, int to, ITextRange **range)
{
    IUnknown *ole = NULL; ITextDocument *doc; ITextFont *font;
    SendMessageW(hwnd, EM_GETOLEINTERFACE, 0, (LPARAM)&ole);
    IUnknown_QueryInterface(ole, &iid_ITextDocument, (void **)&doc);
    ITextDocument_Range(doc, from, to, range);
    ITextRange_GetFont(*range, &font);
    ITextDocument_Release(doc); IUnknown_Release(ole);
    return font;
}

static void test_tom(void)
{
    HWND hwnd = new_edit();
    ITextRange *range; ITextFont *font;
    CHARFORMAT2W cf = { sizeof(cf) };
    HRESULT hr; float f; LONG l;

    printf("== tom (%ls)\n", cls_name);
    SendMessageW(hwnd, WM_SETTEXT, 0, (LPARAM)L"abcdefgh");
    set_font(hwnd, SCF_ALL, L"Tahoma", DEFAULT_CHARSET, 240);
    print_cf(hwnd, "initial", 0, 4);

    font = range_font(hwnd, 0, 4, &range);
    hr = ITextFont_GetSize(font, &f); printf("  GetSize %08lx %g\n", hr, f);
    hr = ITextFont_SetSize(font, 20.0f); printf("  SetSize(20) %08lx\n", hr);
    print_cf(hwnd, "after SetSize(20)", 0, 4);
    hr = ITextFont_SetPosition(font, 3.0f); printf("  SetPosition(3) %08lx\n", hr);
    hr = ITextFont_SetKerning(font, 2.0f); printf("  SetKerning(2) %08lx\n", hr);
    hr = ITextFont_SetSpacing(font, 1.5f); printf("  SetSpacing(1.5) %08lx\n", hr);
    print_cf(hwnd, "after SetPosition/Kerning/Spacing", 0, 4);
    hr = ITextFont_GetPosition(font, &f); printf("  GetPosition %08lx %g\n", hr, f);
    hr = ITextFont_GetKerning(font, &f); printf("  GetKerning %08lx %g\n", hr, f);
    hr = ITextFont_GetSpacing(font, &f); printf("  GetSpacing %08lx %g\n", hr, f);
    ITextFont_Release(font); ITextRange_Release(range);

    /* Reset(tomApplyNow) without tomApplyLater on a range with mixed formats */
    SendMessageW(hwnd, WM_SETTEXT, 0, (LPARAM)L"abcdefgh");
    set_font(hwnd, SCF_ALL, L"Tahoma", DEFAULT_CHARSET, 240);
    SendMessageW(hwnd, EM_SETSEL, 0, 2);
    cf.dwMask = CFM_BOLD | CFM_SIZE | CFM_FACE; cf.dwEffects = CFE_BOLD; cf.yHeight = 360; lstrcpyW(cf.szFaceName, L"Arial");
    SendMessageW(hwnd, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
    font = range_font(hwnd, 0, 4, &range);
    hr = ITextFont_GetSize(font, &f); printf("  mixed: GetSize %08lx %g\n", hr, f);
    hr = ITextFont_GetBold(font, &l); printf("  mixed: GetBold %08lx %ld\n", hr, l);
    hr = ITextFont_Reset(font, tomApplyNow); printf("  mixed: Reset(tomApplyNow) %08lx\n", hr);
    print_cf(hwnd, "mixed after Reset(ApplyNow)", 0, 1);
    print_cf(hwnd, "mixed after Reset(ApplyNow)", 3, 4);

    /* tomApplyLater: only the properties set since are applied? */
    hr = ITextFont_Reset(font, tomApplyLater); printf("  Reset(tomApplyLater) %08lx\n", hr);
    hr = ITextFont_SetItalic(font, tomTrue); printf("  SetItalic %08lx\n", hr);
    print_cf(hwnd, "later, before apply", 0, 1);
    hr = ITextFont_GetItalic(font, &l); printf("  later: GetItalic %08lx %ld\n", hr, l);
    hr = ITextFont_Reset(font, tomApplyNow); printf("  Reset(tomApplyNow) %08lx\n", hr);
    print_cf(hwnd, "later, applied", 0, 1);
    print_cf(hwnd, "later, applied", 3, 4);
    hr = ITextFont_SetItalic(font, tomFalse);

    /* tomApplyTmp (Inventor: Reset(4), SetUnderline(0), Reset(0)) */
    hr = ITextFont_Reset(font, 4 /* tomApplyTmp */); printf("  Reset(tomApplyTmp) %08lx\n", hr);
    hr = ITextFont_SetUnderline(font, tomSingle); printf("  tmp SetUnderline(tomSingle) %08lx\n", hr);
    hr = ITextFont_GetUnderline(font, &l); printf("  tmp GetUnderline %08lx %ld\n", hr, l);
    print_cf(hwnd, "tmp underline set", 0, 1);
    print_cf(hwnd, "tmp underline set", 3, 4);
    hr = ITextFont_SetUnderline(font, tomNone); printf("  tmp SetUnderline(tomNone) %08lx\n", hr);
    hr = ITextFont_Reset(font, tomApplyNow); printf("  Reset(tomApplyNow) %08lx\n", hr);
    print_cf(hwnd, "after tmp + ApplyNow", 0, 1);
    print_cf(hwnd, "after tmp + ApplyNow", 3, 4);
    hr = ITextFont_SetUnderline(font, tomSingle); printf("  SetUnderline(tomSingle) %08lx\n", hr);
    print_cf(hwnd, "after SetUnderline", 0, 1);
    /* real underline, then the Inventor sequence */
    hr = ITextFont_Reset(font, 4); hr = ITextFont_SetUnderline(font, tomNone); hr = ITextFont_Reset(font, tomApplyNow);
    print_cf(hwnd, "underlined + tmp none + ApplyNow", 0, 1);
    hr = ITextFont_GetUnderline(font, &l); printf("  GetUnderline %08lx %ld\n", hr, l);
    ITextFont_Release(font); ITextRange_Release(range);
    DestroyWindow(hwnd);
}

static DWORD CALLBACK stream_in_cb(DWORD_PTR cookie, BYTE *buf, LONG cb, LONG *pcb)
{
    struct { const BYTE *p; LONG len; } *s = (void *)cookie;
    *pcb = min(cb, s->len);
    memcpy(buf, s->p, *pcb); s->p += *pcb; s->len -= *pcb;
    return 0;
}
static DWORD CALLBACK stream_out_cb(DWORD_PTR cookie, BYTE *buf, LONG cb, LONG *pcb)
{
    char *out = (char *)cookie; size_t n = strlen(out);
    if (n + cb < 4000) { memcpy(out + n, buf, cb); out[n + cb] = 0; }
    *pcb = cb;
    return 0;
}

static void save_bmp(HWND hwnd, const char *name, BYTE **bits_ret, int *w, int *h)
{
    RECT rc; HDC hdc = GetDC(hwnd), mem = CreateCompatibleDC(hdc);
    BITMAPINFO bi = {{ sizeof(BITMAPINFOHEADER) }}; BITMAPFILEHEADER bf = { 0x4d42 };
    void *bits; HBITMAP bmp; FILE *f;
    GetClientRect(hwnd, &rc);
    bi.bmiHeader.biWidth = rc.right; bi.bmiHeader.biHeight = -rc.bottom; bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32;
    bmp = CreateDIBSection(hdc, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    SelectObject(mem, bmp);
    BitBlt(mem, 0, 0, rc.right, rc.bottom, hdc, 0, 0, SRCCOPY);
    GdiFlush();
    bf.bfOffBits = sizeof(bf) + sizeof(bi.bmiHeader); bf.bfSize = bf.bfOffBits + rc.right * rc.bottom * 4;
    if ((f = fopen(name, "wb"))) { fwrite(&bf, sizeof(bf), 1, f); fwrite(&bi.bmiHeader, sizeof(bi.bmiHeader), 1, f); fwrite(bits, rc.right * rc.bottom * 4, 1, f); fclose(f); }
    *bits_ret = malloc(rc.right * rc.bottom * 4); memcpy(*bits_ret, bits, rc.right * rc.bottom * 4);
    *w = rc.right; *h = rc.bottom;
    DeleteDC(mem); DeleteObject(bmp); ReleaseDC(hwnd, hdc);
}

static void pump(void)
{
    MSG msg; DWORD end = GetTickCount() + 300;
    while (GetTickCount() < end) { while (PeekMessageW(&msg, 0, 0, 0, PM_REMOVE)) DispatchMessageW(&msg); Sleep(10); }
}

/* method: 0 EM_REPLACESEL, 1 stream in unicode text, 2 stream in RTF with \u, 3 EM_SETTEXTEX unicode, 4 WM_PASTE, 5 WM_CHAR */
static const char *methods[] = { "EM_REPLACESEL", "EM_STREAMIN text", "EM_STREAMIN rtf", "EM_SETTEXTEX", "WM_PASTE", "WM_CHAR" };
static void test_bind_one(const WCHAR *face, BYTE charset, int langopt, int method)
{
    static const char rtf_fmt[] = "{\\rtf1\\ansi\\ansicpg1252\\uc1\\deff0\\deflang1033\\deflangfe1033{\\fonttbl{\\f0 %ls;}}\r\n"
        "\\pard \\lang1033\\fs24\\f0 ab\\u20013?\\u25991?\\u27979?\\u-29739?cd\\par\r\n}";
    static const WCHAR text[] = {'a','b',0x4e2d,0x6587,0x6d4b,0x8bd5,'c','d',0};
    HWND hwnd = new_edit();
    char rtf[512], out[4096] = "", name[64];
    struct { const BYTE *p; LONG len; } si;
    EDITSTREAM es = { 0 };
    SETTEXTEX st = { ST_SELECTION, 1200 };
    DWORD opts = SendMessageW(hwnd, EM_GETLANGOPTIONS, 0, 0);
    BYTE *bits; int w, h, i, x[10], y, k, same = 0, ink = 0;
    POINTL pt;

    if (langopt == 1) SendMessageW(hwnd, EM_SETLANGOPTIONS, 0, 0);
    if (langopt == 2) SendMessageW(hwnd, EM_SETLANGOPTIONS, 0, opts & ~IMF_AUTOFONT);
    printf("-- %ls face '%ls' cs %u, langopts %#lx -> %#lx, %s\n", cls_name, face, charset, opts,
           (DWORD)SendMessageW(hwnd, EM_GETLANGOPTIONS, 0, 0), methods[method]);
    set_font(hwnd, SCF_ALL, face, charset, 240);
    set_font(hwnd, SCF_SELECTION, face, charset, 240);
    switch (method)
    {
    case 0: SendMessageW(hwnd, EM_REPLACESEL, FALSE, (LPARAM)text); break;
    case 1: si.p = (const BYTE *)text; si.len = sizeof(text) - sizeof(WCHAR); es.dwCookie = (DWORD_PTR)&si; es.pfnCallback = stream_in_cb;
            SendMessageW(hwnd, EM_STREAMIN, SF_TEXT | SF_UNICODE | SFF_SELECTION, (LPARAM)&es); break;
    case 2: sprintf(rtf, rtf_fmt, face); si.p = (const BYTE *)rtf; si.len = strlen(rtf); es.dwCookie = (DWORD_PTR)&si; es.pfnCallback = stream_in_cb;
            SendMessageW(hwnd, EM_STREAMIN, SF_RTF, (LPARAM)&es); break;
    case 3: SendMessageW(hwnd, EM_SETTEXTEX, (WPARAM)&st, (LPARAM)text); break;
    case 4:
    {
        HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE, sizeof(text));
        memcpy(GlobalLock(g), text, sizeof(text)); GlobalUnlock(g);
        OpenClipboard(hwnd); EmptyClipboard(); SetClipboardData(CF_UNICODETEXT, g); CloseClipboard();
        SendMessageW(hwnd, WM_PASTE, 0, 0); break;
    }
    case 5: for (i = 0; text[i]; i++) SendMessageW(hwnd, WM_CHAR, text[i], 1); break;
    }
    for (i = 0; i < 8; i++)
    {
        CHARFORMAT2W cf; get_cf(hwnd, i, i + 1, &cf);
        SendMessageW(hwnd, EM_POSFROMCHAR, (WPARAM)&pt, i); x[i] = pt.x; y = pt.y;
        printf("  char %d: face '%ls' cs %u height %ld lcid %#lx x %ld\n", i, cf.szFaceName, cf.bCharSet, cf.yHeight, cf.lcid, pt.x);
    }
    SendMessageW(hwnd, EM_POSFROMCHAR, (WPARAM)&pt, 8); x[8] = pt.x;
    SendMessageW(hwnd, EM_SETSEL, 8, 8);
    SendMessageW(hwnd, EM_HIDESELECTION, TRUE, 0);
    RedrawWindow(hwnd, NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ERASE);
    pump();
    sprintf(name, "bind-%ls-%ls-cs%u-lo%d-m%d.bmp", cls_name, face, charset, langopt, method);
    save_bmp(hwnd, name, &bits, &w, &h);
    /* boxes: the cells of the 4 CJK chars look the same */
    for (i = 2; i < 5; i++)
    {
        int cw = min(x[i + 1] - x[i], x[i + 2] - x[i + 1]), diff = 0;
        for (y = 0; y < min(h, 40); y++) for (k = 0; k < cw; k++)
        {
            DWORD a = ((DWORD *)bits)[y * w + x[i] + k] & 0xffffff, b = ((DWORD *)bits)[y * w + x[i + 1] + k] & 0xffffff;
            if (a != b) diff++;
            if (a != 0xffffff) ink++;
        }
        if (!diff) same++;
    }
    printf("  cjk cells: %d of 3 neighbour pairs identical, ink %d -> %s\n", same, ink, !ink ? "EMPTY" : same == 3 ? "BOXES" : "glyphs");
    es.dwCookie = (DWORD_PTR)out; es.pfnCallback = stream_out_cb;
    SendMessageW(hwnd, EM_STREAMOUT, SF_RTF, (LPARAM)&es);
    for (i = 0; out[i]; i++) if (out[i] == '\r' || out[i] == '\n') out[i] = ' ';
    printf("  rtf: %s\n", out);
    free(bits);
    DestroyWindow(hwnd);
}

static void test_bind(void)
{
    static const struct { const WCHAR *face; BYTE cs; } fonts[] = { { L"Tahoma", DEFAULT_CHARSET }, { L"Tahoma", ANSI_CHARSET }, { L"Arial", DEFAULT_CHARSET } };
    int f, lo, m;
    printf("== bind (%ls)\n", cls_name);
    for (f = 0; f < 3; f++) for (lo = 0; lo < 3; lo++) for (m = 0; m < 6; m++)
    {
        if (f && (m == 1 || m == 3 || m == 5)) continue;
        test_bind_one(fonts[f].face, fonts[f].cs, lo, m);
    }
}

static void test_wrap(void)
{
    static const WCHAR cjk8[] = {0x4e2d,0x6587,0x6d4b,0x8bd5,0x4e2d,0x6587,0x6d4b,0x8bd5,0};
    static const struct { const WCHAR *face; BYTE cs; const WCHAR *text; } t[] =
    {
        { L"SimSun", GB2312_CHARSET, cjk8 }, { L"Tahoma", DEFAULT_CHARSET, cjk8 },
        { L"Tahoma", DEFAULT_CHARSET, L"aaaa bbbb cccc dddd" }, { L"Tahoma", DEFAULT_CHARSET, L"abcdefghijklmnopqrstuvwxyz" },
    };
    static const int widths[] = { 0, 300, 600, 840, 1168, 3000 };
    HDC screen = GetDC(0);
    int i, j, k;

    printf("== wrap (%ls), screen dpi %d\n", cls_name, GetDeviceCaps(screen, LOGPIXELSX));
    for (i = 0; i < ARRAYSIZE(t); i++) for (j = 0; j < ARRAYSIZE(widths); j++)
    {
        HWND hwnd = new_edit();
        POINTL pt; int lines;
        set_font(hwnd, SCF_ALL, t[i].face, t[i].cs, 240);
        if (j) printf("  SETTARGETDEVICE(screen, %d) -> %ld;", widths[j], (long)SendMessageW(hwnd, EM_SETTARGETDEVICE, (WPARAM)screen, widths[j]));
        else printf("  no target device;");
        SendMessageW(hwnd, EM_REPLACESEL, FALSE, (LPARAM)t[i].text);
        lines = SendMessageW(hwnd, EM_GETLINECOUNT, 0, 0);
        printf(" '%ls' %s: %d lines, starts", t[i].face, i < 2 ? "cjk8" : i == 2 ? "words" : "alphabet", lines);
        for (k = 0; k < lines; k++) printf(" %ld", (long)SendMessageW(hwnd, EM_LINEINDEX, k, 0));
        SendMessageW(hwnd, EM_POSFROMCHAR, (WPARAM)&pt, 1); printf(", x(1) %ld", pt.x);
        SendMessageW(hwnd, EM_POSFROMCHAR, (WPARAM)&pt, 2); printf(" x(2) %ld\n", pt.x);
        DestroyWindow(hwnd);
    }
    ReleaseDC(0, screen);
}

static void print_sel(HWND hwnd, const char *what)
{
    CHARRANGE cr = { -2, -2 };
    GETTEXTLENGTHEX gtl = { GTL_NUMCHARS, 1200 };
    SendMessageW(hwnd, EM_EXGETSEL, 0, (LPARAM)&cr);
    printf("  %-28s sel (%ld,%ld) type %#lx len %ld\n", what, cr.cpMin, cr.cpMax,
           (long)SendMessageW(hwnd, EM_SELECTIONTYPE, 0, 0), (long)SendMessageW(hwnd, EM_GETTEXTLENGTHEX, (WPARAM)&gtl, 0));
}

/* selection through the final paragraph mark, then delete it in several ways */
static void test_sel(void)
{
    static const WCHAR *texts[] = { L"", L"abc", L"abc\rdef" };
    static const char *ops[] = { "WM_CLEAR", "EM_REPLACESEL ''", "WM_CHAR x", "VK_DELETE", "WM_CUT", "EM_SETCHARFORMAT" };
    CHARRANGE all = { 0, -1 }, part = { 1, -1 }, one = { 0, 1 };
    int t, op, r;

    printf("== sel (%ls)\n", cls_name);
    for (t = 0; t < 3; t++) for (r = 0; r < 3; r++) for (op = 0; op < 6; op++)
    {
        HWND hwnd = new_edit();
        CHARFORMAT2W cf = { sizeof(cf) };
        LRESULT ret;
        if (r == 2 && t) { DestroyWindow(hwnd); continue; }
        SendMessageW(hwnd, WM_SETTEXT, 0, (LPARAM)texts[t]);
        ret = SendMessageW(hwnd, EM_EXSETSEL, 0, (LPARAM)(r == 0 ? &all : r == 1 ? &part : &one));
        printf("-- text %d, EM_EXSETSEL(%d,%d) -> %ld, %s\n", t, r == 1, r == 2 ? 1 : -1, (long)ret, ops[op]);
        print_sel(hwnd, "selected");
        switch (op)
        {
        case 0: SendMessageW(hwnd, WM_CLEAR, 0, 0); break;
        case 1: SendMessageW(hwnd, EM_REPLACESEL, TRUE, (LPARAM)L""); break;
        case 2: SendMessageW(hwnd, WM_CHAR, 'x', 1); break;
        case 3: SendMessageW(hwnd, WM_KEYDOWN, VK_DELETE, 1); SendMessageW(hwnd, WM_KEYUP, VK_DELETE, 0xc0000001); break;
        case 4: SendMessageW(hwnd, WM_CUT, 0, 0); break;
        case 5: cf.dwMask = CFM_BOLD; cf.dwEffects = CFE_BOLD; SendMessageW(hwnd, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf); break;
        }
        print_sel(hwnd, "after");
        DestroyWindow(hwnd);
    }
}

/* EM_SETCHARFORMAT(SCF_SELECTION) at an insertion point: does the final paragraph mark take the format? */
static void test_eop(void)
{
    static const WCHAR *texts[] = { L"", L"abc" };
    int t, pos, ins;

    printf("== eop (%ls)\n", cls_name);
    for (t = 0; t < 2; t++) for (pos = 0; pos < 2; pos++) for (ins = 0; ins < 3; ins++)
    {
        HWND hwnd = new_edit();
        CHARRANGE all = { 0, -1 };
        int len = lstrlenW(texts[t]), at = pos ? len : 0;
        if (!t && pos) { DestroyWindow(hwnd); continue; }
        set_font(hwnd, SCF_ALL, L"Arial", ANSI_CHARSET, 360);
        SendMessageW(hwnd, WM_SETTEXT, 0, (LPARAM)texts[t]);
        if (!t) { SendMessageW(hwnd, EM_EXSETSEL, 0, (LPARAM)&all); SendMessageW(hwnd, WM_CLEAR, 0, 0); }
        else SendMessageW(hwnd, EM_SETSEL, at, at);
        printf("-- text '%ls', caret %d, then %s\n", texts[t], at, ins == 0 ? "nothing" : ins == 1 ? "EM_REPLACESEL xy" : "EM_SETSEL elsewhere and back, EM_REPLACESEL xy");
        set_font(hwnd, SCF_SELECTION, L"Tahoma", ANSI_CHARSET, 240);
        print_cf(hwnd, "caret after SETCF", at, at);
        if (ins == 2) { SendMessageW(hwnd, EM_SETSEL, 0, 1); SendMessageW(hwnd, EM_SETSEL, at, at); }
        if (ins) { SendMessageW(hwnd, EM_REPLACESEL, FALSE, (LPARAM)L"xy"); print_cf(hwnd, "inserted", at, at + 2); len += 2; }
        print_cf(hwnd, "final paragraph mark", len, len + 1);
        if (len) print_cf(hwnd, "first char", 0, 1);
        DestroyWindow(hwnd);
    }
}

int main(int argc, char **argv)
{
    static const struct { const WCHAR *dll, *cls; } impl[] = { { L"msftedit.dll", L"RICHEDIT50W" }, { L"riched20.dll", L"RichEdit20W" } };
    int i, j;
    setvbuf(stdout, NULL, _IONBF, 0);
    for (i = 0; i < 2; i++)
    {
        if (!LoadLibraryW(impl[i].dll)) { printf("no %ls\n", impl[i].dll); continue; }
        cls_name = impl[i].cls;
        for (j = 1; j < argc || j == 1; j++)
        {
            const char *a = argc > 1 ? argv[j] : "all";
            if (!strcmp(a, "tom") || !strcmp(a, "all")) test_tom();
            if (!strcmp(a, "bind") || !strcmp(a, "all")) test_bind();
            if (!strcmp(a, "wrap") || !strcmp(a, "all")) test_wrap();
            if (!strcmp(a, "sel") || !strcmp(a, "all")) test_sel();
            if (!strcmp(a, "eop") || !strcmp(a, "all")) test_eop();
        }
    }
    return 0;
}
