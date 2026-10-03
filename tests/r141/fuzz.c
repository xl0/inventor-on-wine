/* 141 review: random formatting/selection/focus messages to a rich edit; after every action the
 * layout invariant is checked with ITextRange::ScrollIntoView (cursor_coords() asserts on a paragraph
 * that is still marked MEPF_REWRAP) on every paragraph start, and a focus round trip (create_caret).
 * Every action is printed before it runs, so the last line before "Assertion failed" is the culprit.
 * Build: x86_64-w64-mingw32-gcc -O1 -o fuzz.exe fuzz.c -lgdi32 -luser32 -lole32 -loleaut32 -luuid
 * Run:   fuzz.exe SEED STEPS CLASS(0=RICHEDIT50W 1=RichEdit20W 2=RichEdit20A 3=RICHEDIT) STYLEHEX [-x act,act] [-q] */
#define COBJMACROS
#include <windows.h>
#include <richedit.h>
#include <richole.h>
#include <tom.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static const GUID my_IID_ITextDocument = {0x8cc497c0, 0xa1df, 0x11ce, {0x80, 0x98, 0x00, 0xaa, 0x00, 0x47, 0xbe, 0x5d}};

static HWND parent, other, e;
static ITextDocument *doc;
static unsigned int rnd_state;
static int quiet, dump, noword, tables;
static unsigned char keep[100000];
static int have_keep;
static const char *excluded = "";

static unsigned int rnd(void)
{
    rnd_state = rnd_state * 1103515245 + 12345;
    return (rnd_state >> 16) & 0x7fff;
}
static int rn(int n) { return n > 0 ? rnd() % n : 0; }

static int text_len(void)
{
    GETTEXTLENGTHEX gtl = { GTL_NUMCHARS | GTL_PRECISE, 1200 };
    return SendMessageW(e, EM_GETTEXTLENGTHEX, (WPARAM)&gtl, 0);
}

static const WCHAR *texts[] =
{
    L"", L"one two  three", L"<<>>", L"ab, cd.", L"x\ryz", L"  lead", L"trail  ", L"a\r\rb\r", L"\r",
    L"\x4e2d\x6587\x6d4b\x8bd5 abc", L"word\tword\ttab", L"a b c d e f g h i j k l m n o p q r s t u v w x y z a b c d e f g h i j k l m n o p q r s t u v w x y z",
    L"line1\rline2\rline3\rline4\rline5\rline6\rline7\rline8\rline9\rline10\rline11\rline12",
    L"\x05d0\x05d1\x05d2 abc \x05d3\x05d4", L"x\xd83d\xde00y z",
};

static DWORD CALLBACK stream_in(DWORD_PTR cookie, BYTE *buf, LONG cb, LONG *pcb)
{
    const char **p = (const char **)cookie;
    int len = strlen(*p);
    if (len > cb) len = cb;
    memcpy(buf, *p, len);
    *p += len;
    *pcb = len;
    return 0;
}

static const char *rtfs[] =
{
    "{\\rtf1 plain \\b bold\\b0  text\\par second}",
    "{\\rtf1\\trowd\\cellx1000\\cellx2000 a\\cell b c\\cell\\row after table\\par}",
    "{\\rtf1 x\\par\\trowd\\cellx1500 cell one\\cell\\row\\trowd\\cellx1500 two\\cell\\row y}",
    "{\\rtf1 {\\v hidden} shown {\\fs48 big} small\\par\\pard\\qc centered\\par}",
    "{\\rtf1{\\pntext\\'B7\\tab}{\\*\\pn\\pnlvlblt\\pnf1\\pnindent0{\\pntxtb\\'B7}}\\fi-720\\li720 bullet one\\par bullet two\\par}",
};

static void check(const char *what)
{
    int len = text_len(), cp, n = 0;
    WCHAR buf[512];
    TEXTRANGEW tr;

    if (!doc) return;
    for (cp = 0; cp <= len && n < 40; n++)
    {
        ITextRange *range;
        int i, got;
        if (ITextDocument_Range(doc, cp, cp, &range) == S_OK)
        {
            ITextRange_ScrollIntoView(range, tomStart);
            ITextRange_Release(range);
        }
        /* next paragraph start */
        tr.chrg.cpMin = cp; tr.chrg.cpMax = min(len, cp + 500); tr.lpstrText = buf;
        if (tr.chrg.cpMax <= tr.chrg.cpMin) break;
        got = SendMessageW(e, EM_GETTEXTRANGE, 0, (LPARAM)&tr);
        for (i = 0; i < got && buf[i] != '\r'; i++) ;
        if (i >= got) { cp += got ? got : 1; if (got == 500) continue; break; }
        cp += i + 1;
    }
    (void)what;
}

static int excl(const char *name)
{
    const char *p = strstr(excluded, name);
    int l = strlen(name);
    return p && (p == excluded || p[-1] == ',') && (p[l] == 0 || p[l] == ',');
}

#define LOG(...) do { if (!quiet) { printf(__VA_ARGS__); printf("\n"); } } while (0)

static void rand_cf(CHARFORMAT2W *cf)
{
    static const DWORD masks[] = { CFM_BOLD, CFM_ITALIC, CFM_SIZE, CFM_FACE, CFM_HIDDEN, CFM_LINK, CFM_PROTECTED, CFM_OFFSET,
                                   CFM_SUPERSCRIPT, CFM_UNDERLINE, CFM_COLOR, CFM_SPACING, CFM_WEIGHT, CFM_CHARSET, CFM_ALLCAPS, CFM_SMALLCAPS };
    static const WCHAR *faces[] = { L"Tahoma", L"Arial", L"Courier New", L"Times New Roman", L"NoSuchFont" };
    int i, n = 1 + rn(3);
    memset(cf, 0, sizeof(*cf));
    cf->cbSize = rn(8) ? sizeof(CHARFORMAT2W) : sizeof(CHARFORMATW);
    for (i = 0; i < n; i++) cf->dwMask |= masks[rn(ARRAYSIZE(masks))];
    if (cf->cbSize == sizeof(CHARFORMATW)) cf->dwMask &= CFM_BOLD | CFM_ITALIC | CFM_SIZE | CFM_FACE | CFM_OFFSET | CFM_UNDERLINE | CFM_COLOR | CFM_PROTECTED | CFM_CHARSET | CFM_LINK;
    if (!rn(10)) cf->dwMask = 0;
    cf->dwEffects = rn(2) ? 0xffffffff & ~CFE_AUTOCOLOR : 0;
    cf->yHeight = 20 * (1 + rn(60));
    cf->yOffset = rn(200) - 100;
    cf->sSpacing = rn(60);
    cf->wWeight = rn(2) ? 700 : 400;
    cf->crTextColor = rn(0xffffff);
    cf->bCharSet = rn(2) ? DEFAULT_CHARSET : HEBREW_CHARSET;
    lstrcpyW(cf->szFaceName, faces[rn(ARRAYSIZE(faces))]);
}

static LRESULT CALLBACK parent_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_NOTIFY)
    {
        NMHDR *hdr = (NMHDR *)lp;
        if (hdr->code == EN_PROTECTED) return rnd_state & 0x10000 ? 1 : 0;
        if (hdr->code == EN_REQUESTRESIZE) return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static int CALLBACK wb_proc(WCHAR *s, int cur, int cb, int code)
{
    int len = cb / sizeof(WCHAR);
    switch (code)
    {
    case WB_ISDELIMITER: return cur < len && (s[cur] == ' ' || s[cur] == ',' || s[cur] == '<');
    case WB_LEFT: case WB_MOVEWORDLEFT:
        while (cur > 0 && s[cur - 1] == ' ') cur--;
        while (cur > 0 && s[cur - 1] != ' ') cur--;
        return cur;
    case WB_RIGHT: case WB_MOVEWORDRIGHT:
        while (cur < len && s[cur] != ' ') cur++;
        while (cur < len && s[cur] == ' ') cur++;
        return cur;
    }
    return 0;
}

int main(int argc, char **argv)
{
    static const WCHAR *classes[] = { L"RICHEDIT50W", L"RichEdit20W", L"RichEdit20A", L"RICHEDIT" };
    int seed = argc > 1 ? atoi(argv[1]) : 1, steps = argc > 2 ? atoi(argv[2]) : 1000;
    int cls = argc > 3 ? atoi(argv[3]) : 0, step, i;
    DWORD style = argc > 4 ? strtoul(argv[4], NULL, 16) : ES_MULTILINE | WS_VISIBLE;
    WNDCLASSW wc = { 0 };
    IRichEditOle *ole = NULL;
    BOOL redraw = TRUE;
    int perstep = 0;

    for (i = 5; i < argc; i++)
    {
        if (!strcmp(argv[i], "-x") && i + 1 < argc) excluded = argv[++i];
        else if (!strcmp(argv[i], "-q")) quiet = 1;
        else if (!strcmp(argv[i], "-d")) dump = 1;
        else if (!strcmp(argv[i], "-w")) noword = 1;
        else if (!strcmp(argv[i], "-t")) tables = 1;
        else if (!strcmp(argv[i], "-p")) perstep = 1;
        else if (!strcmp(argv[i], "-k") && i + 1 < argc)
        {
            FILE *f = fopen(argv[++i], "r"); int n;
            have_keep = perstep = 1;
            while (f && fscanf(f, "%d", &n) == 1) if (n >= 0 && n < 100000) keep[n] = 1;
            if (f) fclose(f);
        }
    }
    setvbuf(stdout, NULL, _IONBF, 0);
    rnd_state = seed;
    LoadLibraryW(L"msftedit.dll");
    LoadLibraryW(L"riched20.dll");
    LoadLibraryW(L"riched32.dll");
    wc.lpfnWndProc = parent_proc; wc.lpszClassName = L"r141fuzz"; wc.hInstance = GetModuleHandleW(NULL);
    RegisterClassW(&wc);
    parent = CreateWindowExW(0, L"r141fuzz", L"r141", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 50, 50, 500, 300, NULL, NULL, NULL, NULL);
    other = CreateWindowExW(0, L"edit", L"", WS_CHILD | WS_VISIBLE, 0, 200, 100, 30, parent, NULL, NULL, NULL);
    SetForegroundWindow(parent);
    e = CreateWindowExW(0, classes[cls], NULL, WS_CHILD | style, 10, 10, 300, 100, parent, NULL, NULL, NULL);
    if (!e) { printf("no window of class %ls (%lu)\n", classes[cls], GetLastError()); return 2; }
    SendMessageW(e, EM_GETOLEINTERFACE, 0, (LPARAM)&ole);
    if (ole) ole->lpVtbl->QueryInterface(ole, &my_IID_ITextDocument, (void **)&doc);
    printf("seed %d steps %d class %ls style %#lx doc %p\n", seed, steps, classes[cls], style, doc);

    for (step = 0; step < steps; step++)
    {
        int act, len = text_len(), a, b;
        if (perstep) { rnd_state = seed * 2654435761u + step * 40503u + 12345; rnd(); rnd(); }
        if (have_keep && !keep[step]) continue;
        act = rn(40);
        CHARFORMAT2W cf;
        PARAFORMAT2 pf;

        if (dump)
        {
            WCHAR buf[200]; TEXTRANGEW tr = { { 0, 150 }, buf }; CHARRANGE cr; int k, got;
            got = SendMessageW(e, EM_GETTEXTRANGE, 0, (LPARAM)&tr);
            SendMessageW(e, EM_EXGETSEL, 0, (LPARAM)&cr);
            printf("   [len %d sel %ld,%ld mode %ld focus %d text '", len, cr.cpMin, cr.cpMax, (long)SendMessageW(e, EM_GETTEXTMODE, 0, 0), GetFocus() == e);
            for (k = 0; k < got; k++) { if (buf[k] < 32 || buf[k] > 126) printf("\\x%x", buf[k]); else putchar(buf[k]); }
            printf("']\n");
        }
        switch (act)
        {
        case 0: case 1:
            if (excl("settext")) break;
            a = rn(ARRAYSIZE(texts));
            LOG("%d settext %d", step, a);
            SendMessageW(e, WM_SETTEXT, 0, (LPARAM)texts[a]);
            break;
        case 2: case 3: case 4: case 5:
            if (excl("setsel")) break;
            a = rn(len + 3) - 1; b = rn(4) ? a : rn(len + 3) - 1;
            LOG("%d setsel %d %d", step, a, b);
            SendMessageW(e, EM_SETSEL, a, b);
            break;
        case 6: case 7: case 8: case 9: case 10: case 11: case 12:
        {
            static const WPARAM flagv[] = { SCF_SELECTION, SCF_SELECTION | SCF_WORD, SCF_ALL, SCF_DEFAULT, SCF_WORD, SCF_ALL | SCF_SELECTION,
                                            SCF_SELECTION | SCF_ASSOCIATEFONT, SCF_ASSOCIATEFONT, SCF_SELECTION | SCF_NOKBUPDATE, SCF_USEUIRULES | SCF_SELECTION,
                                            SCF_ASSOCIATEFONT2 | SCF_SELECTION, SCF_SELECTION | SCF_WORD | SCF_ALL, 0x100, 0xffffffff };
            WPARAM flags = act < 9 ? SCF_SELECTION | SCF_WORD : flagv[rn(ARRAYSIZE(flagv))];
            if (excl("setcf")) break;
            if (noword && (flags & SCF_SELECTION)) flags &= ~(WPARAM)SCF_WORD;
            rand_cf(&cf);
            LOG("%d setcf flags %#lx cb %u mask %#lx eff %#lx h %ld", step, (long)flags, cf.cbSize, cf.dwMask, cf.dwEffects, cf.yHeight);
            SendMessageW(e, EM_SETCHARFORMAT, flags, (LPARAM)&cf);
            break;
        }
        case 13: case 14:
            if (excl("setpf")) break;
            memset(&pf, 0, sizeof(pf));
            pf.cbSize = rn(4) ? sizeof(PARAFORMAT2) : sizeof(PARAFORMAT);
            pf.dwMask = (rn(2) ? PFM_ALIGNMENT : 0) | (rn(2) ? PFM_NUMBERING : 0) | (rn(2) ? PFM_STARTINDENT | PFM_OFFSET : 0)
                        | (rn(3) ? 0 : PFM_TABSTOPS) | (rn(3) ? 0 : PFM_RIGHTINDENT);
            if (pf.cbSize == sizeof(PARAFORMAT2)) pf.dwMask |= (rn(3) ? 0 : PFM_SPACEBEFORE | PFM_SPACEAFTER) | (rn(3) ? 0 : PFM_LINESPACING) | (rn(4) ? 0 : PFM_RTLPARA);
            pf.wAlignment = 1 + rn(4); pf.wNumbering = rn(3); pf.dxStartIndent = rn(2000); pf.dxOffset = rn(1000) - 500;
            pf.dxRightIndent = rn(1000); pf.cTabCount = rn(3); pf.rgxTabs[0] = 500; pf.rgxTabs[1] = 1500;
            pf.dySpaceBefore = rn(400); pf.dySpaceAfter = rn(400); pf.dyLineSpacing = rn(600); pf.bLineSpacingRule = rn(6);
            pf.wEffects = rn(2) ? PFE_RTLPARA : 0;
            LOG("%d setpf cb %u mask %#lx", step, pf.cbSize, pf.dwMask);
            SendMessageW(e, EM_SETPARAFORMAT, 0, (LPARAM)&pf);
            break;
        case 15:
            if (excl("fontsize")) break;
            a = rn(9) - 4;
            LOG("%d fontsize %d", step, a);
            SendMessageW(e, EM_SETFONTSIZE, a, 0);
            break;
        case 16:
            if (excl("textmode")) break;
            a = rn(2) ? TM_PLAINTEXT : TM_RICHTEXT;
            LOG("%d textmode %d", step, a);
            SendMessageW(e, EM_SETTEXTMODE, a, 0);
            break;
        case 17:
            if (excl("langopt")) break;
            a = rn(0x200);
            LOG("%d langopt %#x", step, a);
            SendMessageW(e, EM_SETLANGOPTIONS, 0, a);
            break;
        case 18: case 19:
            if (excl("replacesel")) break;
            a = rn(ARRAYSIZE(texts)); b = rn(2);
            LOG("%d replacesel %d undo %d", step, a, b);
            SendMessageW(e, EM_REPLACESEL, b, (LPARAM)texts[a]);
            break;
        case 20: case 21:
        {
            static const WCHAR chars[] = L"a b\r\x08\tZ ,.";
            if (excl("char")) break;
            a = chars[rn(ARRAYSIZE(chars) - 1)];
            LOG("%d char %#x", step, a);
            SendMessageW(e, WM_CHAR, a, 1);
            break;
        }
        case 22:
            if (excl("undo")) break;
            LOG("%d undo", step);
            SendMessageW(e, EM_UNDO, 0, 0);
            break;
        case 23:
            if (excl("redo")) break;
            LOG("%d redo", step);
            SendMessageW(e, EM_REDO, 0, 0);
            break;
        case 24:
            if (excl("setredraw")) break;
            redraw = !redraw;
            LOG("%d setredraw %d", step, redraw);
            SendMessageW(e, WM_SETREDRAW, redraw, 0);
            break;
        case 25:
            if (excl("eventmask")) break;
            a = rn(2) ? ENM_CHANGE | ENM_SELCHANGE | ENM_UPDATE | ENM_PROTECTED | ENM_REQUESTRESIZE | ENM_LINK : 0;
            LOG("%d eventmask %#x", step, a);
            SendMessageW(e, EM_SETEVENTMASK, 0, a);
            break;
        case 26: case 27:
            if (excl("focus")) break;
            a = rn(3);
            LOG("%d focus %d", step, a);
            SetFocus(a == 0 ? e : a == 1 ? parent : other);
            break;
        case 28:
            if (excl("readonly")) break;
            a = rn(2);
            LOG("%d readonly %d", step, a);
            SendMessageW(e, EM_SETREADONLY, a, 0);
            break;
        case 29:
            if (excl("password")) break;
            a = rn(3) ? 0 : '*';
            LOG("%d password %d", step, a);
            SendMessageW(e, EM_SETPASSWORDCHAR, a, 0);
            break;
        case 30:
            if (excl("zoom")) break;
            a = rn(4);
            LOG("%d zoom %d", step, a);
            SendMessageW(e, EM_SETZOOM, a, a ? 2 : 0);
            break;
        case 31:
            if (excl("targetdev")) break;
            a = rn(3);
            LOG("%d targetdev %d", step, a);
            SendMessageW(e, EM_SETTARGETDEVICE, 0, a == 0 ? 0 : a == 1 ? 1 : 0);
            if (a == 2) { HDC dc = GetDC(e); SendMessageW(e, EM_SETTARGETDEVICE, (WPARAM)dc, 1000 + rn(3000)); ReleaseDC(e, dc); }
            break;
        case 32:
            if (excl("size")) break;
            a = rn(400); b = rn(200);
            LOG("%d size %d %d show %d", step, a, b, a & 1);
            MoveWindow(e, 10, 10, a, b, TRUE);
            if (!rn(4)) ShowWindow(e, (a & 1) ? SW_SHOW : SW_HIDE);
            break;
        case 33:
        {
            EDITSTREAM es;
            const char *p;
            if (excl("streamin")) break;
            a = rn(ARRAYSIZE(rtfs)); b = rn(2) ? SF_RTF : SF_RTF | SFF_SELECTION;
            if (tables) { a = 1 + rn(3); b = SF_RTF; }
            LOG("%d streamin %d %#x", step, a, b);
            p = rtfs[a]; es.dwCookie = (DWORD_PTR)&p; es.dwError = 0; es.pfnCallback = stream_in;
            SendMessageW(e, EM_STREAMIN, b, (LPARAM)&es);
            break;
        }
        case 34:
            if (excl("setfont")) break;
            a = rn(3);
            LOG("%d setfont %d", step, a);
            SendMessageW(e, WM_SETFONT, (WPARAM)GetStockObject(a == 0 ? DEFAULT_GUI_FONT : a == 1 ? SYSTEM_FONT : ANSI_FIXED_FONT), rn(2));
            break;
        case 35:
            if (excl("misc")) break;
            a = rn(8);
            LOG("%d misc %d", step, a);
            switch (a)
            {
            case 0: SendMessageW(e, EM_AUTOURLDETECT, rn(2), 0); break;
            case 1: SendMessageW(e, EM_HIDESELECTION, rn(2), 0); break;
            case 2: SendMessageW(e, EM_SETUNDOLIMIT, rn(3) * 50, 0); break;
            case 3: SendMessageW(e, EM_EMPTYUNDOBUFFER, 0, 0); break;
            case 4: SendMessageW(e, EM_SETWORDBREAKPROC, 0, rn(2) ? (LPARAM)wb_proc : 0); break;
            case 5: SendMessageW(e, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELONG(rn(20), rn(20))); break;
            case 6: SendMessageW(e, EM_SETOPTIONS, ECOOP_XOR, rn(2) ? ECO_SELECTIONBAR : ECO_READONLY); break;
            case 7: { RECT r = { rn(20), rn(20), 50 + rn(300), 20 + rn(100) }; SendMessageW(e, EM_SETRECT, 0, (LPARAM)&r); break; }
            }
            break;
        case 36:
        {
            ITextRange *range;
            ITextFont *font;
            if (excl("tomfont") || !doc) break;
            a = rn(len + 2); b = a + rn(len + 2 - a); i = rn(6);
            LOG("%d tomfont %d %d op %d", step, a, b, i);
            if (ITextDocument_Range(doc, a, b, &range) != S_OK) break;
            if (ITextRange_GetFont(range, &font) == S_OK)
            {
                switch (i)
                {
                case 0: ITextFont_SetBold(font, tomTrue); break;
                case 1: ITextFont_SetSize(font, 5 + rn(40)); break;
                case 2: ITextFont_SetItalic(font, tomToggle); break;
                case 3: ITextFont_Reset(font, tomApplyLater); ITextFont_SetSize(font, 30); ITextFont_SetBold(font, tomTrue); ITextFont_Reset(font, tomApplyNow); break;
                case 4: { BSTR s = SysAllocString(L"Arial"); ITextFont_SetName(font, s); SysFreeString(s); break; }
                case 5: ITextFont_Reset(font, tomDefault); break;
                }
                ITextFont_Release(font);
            }
            ITextRange_Release(range);
            break;
        }
        case 37:
        {
            ITextRange *range;
            BSTR s;
            if (excl("tomtext") || !doc) break;
            a = rn(len + 2); b = a + rn(len + 2 - a); i = rn(ARRAYSIZE(texts));
            LOG("%d tomtext %d %d text %d", step, a, b, i);
            if (ITextDocument_Range(doc, a, b, &range) != S_OK) break;
            s = SysAllocString(texts[i]);
            ITextRange_SetText(range, s);
            SysFreeString(s);
            ITextRange_Release(range);
            break;
        }
        case 38:
        {
            ITextRange *range;
            ITextPara *para;
            if (excl("tompara") || !doc) break;
            a = rn(len + 2); b = a + rn(len + 2 - a);
            LOG("%d tompara %d %d", step, a, b);
            if (ITextDocument_Range(doc, a, b, &range) != S_OK) break;
            if (ITextRange_GetPara(range, &para) == S_OK)
            {
                ITextPara_SetAlignment(para, rn(3));
                ITextPara_SetIndents(para, rn(30), rn(30), rn(30));
                ITextPara_Release(para);
            }
            ITextRange_Release(range);
            break;
        }
        case 39:
        {
            LONG count;
            if (excl("freeze") || !doc) break;
            a = rn(2);
            LOG("%d freeze %d", step, a);
            if (a) ITextDocument_Freeze(doc, &count); else ITextDocument_Unfreeze(doc, &count);
            break;
        }
        }
        if (!excl("check")) check("after");
        if (!excl("focuscheck") && !rn(3))
        {
            HWND prev = GetFocus();
            SetFocus(parent); SetFocus(e);
            len = text_len();
            if (!rn(2)) { a = rn(len + 1); SendMessageW(e, EM_SETSEL, a, a); }
            SetFocus(prev);
        }
    }
    printf("done %d steps\n", steps);
    return 0;
}
