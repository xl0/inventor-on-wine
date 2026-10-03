/* 141 review: tiny rich edit script runner, same binary on Wine and Windows.
 * Build: x86_64-w64-mingw32-gcc -O1 -o rescript.exe rescript.c -lgdi32 -luser32 -lole32 -loleaut32 -luuid
 * Run:   rescript.exe [class:N] [style:HEX] CMD...   (class 0 RICHEDIT50W, 1 RichEdit20W, 2 RichEdit20A, 3 RICHEDIT)
 * A "--" argument destroys the control and starts a new case with the same class/style.
 * Strings: \r \t \n \\ \s (space) \xHHHH. */
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
static char notif[512];
static int protect_ret, wb_calls, updates;

static void note(const char *s)
{
    if (strlen(notif) + strlen(s) + 2 < sizeof(notif)) { strcat(notif, s); strcat(notif, " "); }
}

static LRESULT CALLBACK parent_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    char buf[64];
    if (msg == WM_COMMAND && (HWND)lp == e)
    {
        switch (HIWORD(wp))
        {
        case EN_CHANGE: note("EN_CHANGE"); break;
        case EN_UPDATE: updates++; break;
        case EN_SETFOCUS: note("EN_SETFOCUS"); break;
        case EN_KILLFOCUS: note("EN_KILLFOCUS"); break;
        case EN_HSCROLL: note("EN_HSCROLL"); break;
        case EN_VSCROLL: note("EN_VSCROLL"); break;
        default: sprintf(buf, "WM_COMMAND(%#x)", HIWORD(wp)); note(buf);
        }
    }
    if (msg == WM_NOTIFY && ((NMHDR *)lp)->hwndFrom == e)
    {
        NMHDR *hdr = (NMHDR *)lp;
        switch (hdr->code)
        {
        case EN_SELCHANGE: { SELCHANGE *sc = (SELCHANGE *)lp; sprintf(buf, "EN_SELCHANGE(%ld,%ld,%#x)", sc->chrg.cpMin, sc->chrg.cpMax, sc->seltyp); note(buf); break; }
        case EN_PROTECTED: { ENPROTECTED *ep = (ENPROTECTED *)lp; sprintf(buf, "EN_PROTECTED(msg=%#x,%ld,%ld)", ep->msg, ep->chrg.cpMin, ep->chrg.cpMax); note(buf); return protect_ret; }
        case EN_REQUESTRESIZE: note("EN_REQUESTRESIZE"); break;
        case EN_LINK: break;
        default: sprintf(buf, "WM_NOTIFY(%#x)", hdr->code); note(buf);
        }
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

/* everything but spaces delimits nothing; ',' '<' and ' ' are delimiters */
static int CALLBACK wb_proc(WCHAR *s, int cur, int cb, int code)
{
    int len = cb / sizeof(WCHAR);
    wb_calls++;
    switch (code)
    {
    case WB_ISDELIMITER: return cur < len && (s[cur] == ' ' || s[cur] == ',');
    case WB_CLASSIFY: return (s[cur] == ' ') ? WBF_ISWHITE | 2 : (s[cur] == ',') ? WBF_BREAKAFTER | 1 : 0;
    case WB_LEFT: case WB_MOVEWORDLEFT: case WB_LEFTBREAK:
        while (cur > 0 && (s[cur - 1] == ' ' || s[cur - 1] == ',')) cur--;
        while (cur > 0 && s[cur - 1] != ' ' && s[cur - 1] != ',') cur--;
        return cur;
    case WB_RIGHT: case WB_MOVEWORDRIGHT: case WB_RIGHTBREAK:
        while (cur < len && s[cur] != ' ' && s[cur] != ',') cur++;
        while (cur < len && (s[cur] == ' ' || s[cur] == ',')) cur++;
        return cur;
    }
    return 0;
}

/* an edit-control style proc: only the three documented EDIT codes, 0 for everything else */
static int CALLBACK wb_proc_classic(WCHAR *s, int cur, int cb, int code)
{
    wb_calls++;
    switch (code)
    {
    case WB_ISDELIMITER: return s[cur] == ' ';
    case WB_LEFT:
        while (cur > 0 && s[cur - 1] == ' ') cur--;
        while (cur > 0 && s[cur - 1] != ' ') cur--;
        return cur;
    case WB_RIGHT:
        while (s[cur] && s[cur] != ' ') cur++;
        while (s[cur] == ' ') cur++;
        return cur;
    }
    return 0;
}

static int CALLBACK wb_proc_zero(WCHAR *s, int cur, int cb, int code)
{
    wb_calls++;
    return 0;
}

static WCHAR *unesc(const char *s)
{
    static WCHAR buf[2048];
    WCHAR *d = buf;
    for (; *s && d < buf + 2040; s++)
    {
        if (*s != '\\') { *d++ = (unsigned char)*s; continue; }
        s++;
        if (*s == 'r') *d++ = '\r';
        else if (*s == 'n') *d++ = '\n';
        else if (*s == 't') *d++ = '\t';
        else if (*s == 's') *d++ = ' ';
        else if (*s == 'x') { char h[5] = { s[1], s[2], s[3], s[4], 0 }; *d++ = strtoul(h, NULL, 16); s += 4; }
        else *d++ = *s;
    }
    *d = 0;
    return buf;
}

static int text_len(void)
{
    GETTEXTLENGTHEX gtl = { GTL_NUMCHARS | GTL_PRECISE, 1200 };
    return SendMessageW(e, EM_GETTEXTLENGTHEX, (WPARAM)&gtl, 0);
}

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

static void get_cf(int from, int to, CHARFORMAT2W *cf)
{
    memset(cf, 0, sizeof(*cf));
    cf->cbSize = sizeof(*cf);
    if (from >= 0) SendMessageW(e, EM_SETSEL, from, to);
    SendMessageW(e, EM_GETCHARFORMAT, SCF_SELECTION, (LPARAM)cf);
}

static void print_map(const char *prop)
{
    int i, len = text_len();
    CHARRANGE cr;
    CHARFORMAT2W cf;
    DWORD mask = SendMessageW(e, EM_GETEVENTMASK, 0, 0);
    char saved[sizeof(notif)];

    strcpy(saved, notif);
    SendMessageW(e, EM_SETEVENTMASK, 0, 0);
    SendMessageW(e, EM_EXGETSEL, 0, (LPARAM)&cr);
    printf("  map %s [", prop);
    for (i = 0; i <= len; i++)
    {
        get_cf(i, i + 1, &cf);
        if (!strcmp(prop, "bold")) putchar(cf.dwEffects & CFE_BOLD ? 'B' : '-');
        else if (!strcmp(prop, "italic")) putchar(cf.dwEffects & CFE_ITALIC ? 'I' : '-');
        else if (!strcmp(prop, "hidden")) putchar(cf.dwEffects & CFE_HIDDEN ? 'H' : '-');
        else if (!strcmp(prop, "link")) putchar(cf.dwEffects & CFE_LINK ? 'L' : '-');
        else if (!strcmp(prop, "protected")) putchar(cf.dwEffects & CFE_PROTECTED ? 'P' : '-');
        else if (!strcmp(prop, "size")) printf("%ld ", cf.yHeight);
        else if (!strcmp(prop, "face")) printf("%ls|", cf.szFaceName);
    }
    printf("]\n");
    SendMessageW(e, EM_EXSETSEL, 0, (LPARAM)&cr);
    SendMessageW(e, EM_SETEVENTMASK, 0, mask);
    strcpy(notif, saved);
}


static void italic_map(char *map)
{
    int i, len = text_len();
    CHARFORMAT2W cf;
    for (i = 0; i <= len && i < 250; i++)
    {
        get_cf(i, i + 1, &cf);
        map[i] = !(cf.dwMask & CFM_ITALIC) ? '?' : cf.dwEffects & CFE_ITALIC ? 'I' : '-';
    }
    map[i] = 0;
}

/* SCF_WORD | SCF_SELECTION italic at every caret position of the current content */
static void sweep(void)
{
    int pos, len = text_len();
    char map[256], map2[256];
    CHARFORMAT2W cf;
    DWORD mask = SendMessageW(e, EM_GETEVENTMASK, 0, 0);
    CHARRANGE cr;
    LRESULT ret, mod, undo, ins;

    for (pos = 0; pos <= len; pos++)
    {
        SendMessageW(e, EM_SETEVENTMASK, 0, 0);
        SendMessageW(e, EM_SETSEL, pos, pos);
        SendMessageW(e, EM_SETMODIFY, 0, 0);
        SendMessageW(e, EM_EMPTYUNDOBUFFER, 0, 0);
        SendMessageW(e, EM_SETEVENTMASK, 0, mask);
        notif[0] = 0;
        memset(&cf, 0, sizeof(cf)); cf.cbSize = sizeof(cf); cf.dwMask = CFM_ITALIC; cf.dwEffects = CFE_ITALIC;
        ret = SendMessageW(e, EM_SETCHARFORMAT, SCF_WORD | SCF_SELECTION, (LPARAM)&cf);
        SendMessageW(e, EM_SETEVENTMASK, 0, 0);
        mod = SendMessageW(e, EM_GETMODIFY, 0, 0);
        undo = SendMessageW(e, EM_CANUNDO, 0, 0);
        SendMessageW(e, EM_EXGETSEL, 0, (LPARAM)&cr);
        get_cf(-1, -1, &cf);
        ins = !!(cf.dwEffects & CFE_ITALIC);
        italic_map(map);
        printf("  %2d: ret %ld mod %d undo %d sel %ld,%ld ins %ld [%s]", pos, (long)ret, !!mod, !!undo, cr.cpMin, cr.cpMax, (long)ins, map);
        if (undo)
        {
            SendMessageW(e, EM_UNDO, 0, 0);
            italic_map(map2);
            if (strchr(map2, 'I')) printf(" UNDO LEFT [%s]", map2);
        }
        if (notif[0]) printf(" notif: %s", notif);
        printf("\n");
        /* clean up whatever is left */
        SendMessageW(e, EM_SETSEL, 0, -1);
        memset(&cf, 0, sizeof(cf)); cf.cbSize = sizeof(cf); cf.dwMask = CFM_ITALIC;
        SendMessageW(e, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
    }
    SendMessageW(e, EM_SETEVENTMASK, 0, mask);
    notif[0] = 0;
}

static void check(void)
{
    int len = text_len(), cp;
    if (!doc) { printf("  check: no ITextDocument\n"); return; }
    for (cp = 0; cp <= len; cp++)
    {
        ITextRange *range;
        if (ITextDocument_Range(doc, cp, cp, &range) == S_OK)
        {
            ITextRange_ScrollIntoView(range, tomStart);
            ITextRange_Release(range);
        }
    }
    printf("  check ok\n");
}

static void new_edit(int cls, DWORD style)
{
    static const WCHAR *classes[] = { L"RICHEDIT50W", L"RichEdit20W", L"RichEdit20A", L"RICHEDIT" };
    IRichEditOle *ole = NULL;
    if (doc) { ITextDocument_Release(doc); doc = NULL; }
    if (e) { SetFocus(parent); DestroyWindow(e); }
    e = CreateWindowExW(0, classes[cls], NULL, WS_CHILD | style, 10, 10, 300, 100, parent, NULL, NULL, NULL);
    if (!e) { printf("no window of class %ls (%lu)\n", classes[cls], GetLastError()); exit(2); }
    SendMessageW(e, EM_GETOLEINTERFACE, 0, (LPARAM)&ole);
    if (ole) { ole->lpVtbl->QueryInterface(ole, &my_IID_ITextDocument, (void **)&doc); ole->lpVtbl->Release(ole); }
    notif[0] = 0; protect_ret = 0;
}

int main(int argc, char **argv)
{
    int cls = 0, i;
    DWORD style = ES_MULTILINE | WS_VISIBLE;
    WNDCLASSW wc = { 0 };

    setvbuf(stdout, NULL, _IONBF, 0);
    LoadLibraryW(L"msftedit.dll");
    LoadLibraryW(L"riched20.dll");
    LoadLibraryW(L"riched32.dll");
    wc.lpfnWndProc = parent_proc; wc.lpszClassName = L"r141script"; wc.hInstance = GetModuleHandleW(NULL);
    RegisterClassW(&wc);
    parent = CreateWindowExW(0, L"r141script", L"r141", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 50, 50, 500, 300, NULL, NULL, NULL, NULL);
    other = CreateWindowExW(0, L"edit", L"", WS_CHILD | WS_VISIBLE, 0, 200, 100, 30, parent, NULL, NULL, NULL);
    SetForegroundWindow(parent);

    for (i = 1; i < argc; i++)
    {
        char *cmd = argv[i], *arg = strchr(cmd, ':');
        CHARFORMAT2W cf;
        LRESULT ret;

        if (arg) *arg++ = 0; else arg = "";
        if (!strcmp(cmd, "class")) { cls = atoi(arg); continue; }
        if (!strcmp(cmd, "style")) { style = strtoul(arg, NULL, 16); continue; }
        if (!strcmp(cmd, "--")) { new_edit(cls, style); printf("--\n"); continue; }
        if (!strcmp(cmd, "echo")) { printf("%s\n", arg); continue; }
        if (!e) new_edit(cls, style);

        if (!strcmp(cmd, "text")) SendMessageW(e, WM_SETTEXT, 0, (LPARAM)unesc(arg));
        else if (!strcmp(cmd, "sel")) { int a = atoi(arg), b = a; char *c = strchr(arg, ','); if (c) b = atoi(c + 1); SendMessageW(e, EM_SETSEL, a, b); }
        else if (!strcmp(cmd, "cf") || !strcmp(cmd, "cf1"))
        {
            /* cf:FLAGS,prop,value */
            char *prop = strchr(arg, ','), *val;
            WPARAM flags = strtoul(arg, NULL, 16);
            if (!prop) { printf("bad cf\n"); return 1; }
            prop++; val = strchr(prop, ','); if (val) *val++ = 0; else val = "1";
            memset(&cf, 0, sizeof(cf));
            cf.cbSize = !strcmp(cmd, "cf1") ? sizeof(CHARFORMATW) : sizeof(cf);
            if (!strcmp(prop, "bold")) { cf.dwMask = CFM_BOLD; cf.dwEffects = atoi(val) ? CFE_BOLD : 0; }
            else if (!strcmp(prop, "italic")) { cf.dwMask = CFM_ITALIC; cf.dwEffects = atoi(val) ? CFE_ITALIC : 0; }
            else if (!strcmp(prop, "hidden")) { cf.dwMask = CFM_HIDDEN; cf.dwEffects = atoi(val) ? CFE_HIDDEN : 0; }
            else if (!strcmp(prop, "link")) { cf.dwMask = CFM_LINK; cf.dwEffects = atoi(val) ? CFE_LINK : 0; }
            else if (!strcmp(prop, "protected")) { cf.dwMask = CFM_PROTECTED; cf.dwEffects = atoi(val) ? CFE_PROTECTED : 0; }
            else if (!strcmp(prop, "size")) { cf.dwMask = CFM_SIZE; cf.yHeight = atoi(val); }
            else if (!strcmp(prop, "face")) { cf.dwMask = CFM_FACE; lstrcpyW(cf.szFaceName, unesc(val)); }
            else if (!strcmp(prop, "none")) cf.dwMask = 0;
            else { printf("bad prop %s\n", prop); return 1; }
            ret = SendMessageW(e, EM_SETCHARFORMAT, flags, (LPARAM)&cf);
            printf("  cf %#lx %s -> %ld\n", (long)flags, prop, (long)ret);
        }
        else if (!strcmp(cmd, "map")) print_map(arg);
        else if (!strcmp(cmd, "focus")) SetFocus(atoi(arg) == 1 ? e : atoi(arg) == 2 ? other : parent);
        else if (!strcmp(cmd, "mod")) printf("  modify %ld canundo %ld canredo %ld\n", (long)SendMessageW(e, EM_GETMODIFY, 0, 0), (long)SendMessageW(e, EM_CANUNDO, 0, 0), (long)SendMessageW(e, EM_CANREDO, 0, 0));
        else if (!strcmp(cmd, "setmod")) SendMessageW(e, EM_SETMODIFY, atoi(arg), 0);
        else if (!strcmp(cmd, "undo")) printf("  undo -> %ld\n", (long)SendMessageW(e, EM_UNDO, 0, 0));
        else if (!strcmp(cmd, "redo")) printf("  redo -> %ld\n", (long)SendMessageW(e, EM_REDO, 0, 0));
        else if (!strcmp(cmd, "emptyundo")) SendMessageW(e, EM_EMPTYUNDOBUFFER, 0, 0);
        else if (!strcmp(cmd, "rtf") || !strcmp(cmd, "rtfsel"))
        {
            EDITSTREAM es;
            const char *p = arg;
            char *u;
            for (u = arg; *u; u++) if (*u == '_') *u = ' ';
            es.dwCookie = (DWORD_PTR)&p; es.dwError = 0; es.pfnCallback = stream_in;
            ret = SendMessageW(e, EM_STREAMIN, !strcmp(cmd, "rtf") ? SF_RTF : SF_RTF | SFF_SELECTION, (LPARAM)&es);
        }
        else if (!strcmp(cmd, "replace")) SendMessageW(e, EM_REPLACESEL, TRUE, (LPARAM)unesc(arg));
        else if (!strcmp(cmd, "replacenu")) SendMessageW(e, EM_REPLACESEL, FALSE, (LPARAM)unesc(arg));
        else if (!strcmp(cmd, "char")) SendMessageW(e, WM_CHAR, unesc(arg)[0], 1);
        else if (!strcmp(cmd, "evmask")) SendMessageW(e, EM_SETEVENTMASK, 0, strtoul(arg, NULL, 16));
        else if (!strcmp(cmd, "wbproc")) SendMessageW(e, EM_SETWORDBREAKPROC, 0, atoi(arg) == 1 ? (LPARAM)wb_proc : atoi(arg) == 2 ? (LPARAM)wb_proc_classic : atoi(arg) == 3 ? (LPARAM)wb_proc_zero : 0);
        else if (!strcmp(cmd, "wbcalls")) { printf("  wb_proc calls %d\n", wb_calls); wb_calls = 0; }
        else if (!strcmp(cmd, "targetdev")) SendMessageW(e, EM_SETTARGETDEVICE, 0, atoi(arg));
        else if (!strcmp(cmd, "redraw")) SendMessageW(e, WM_SETREDRAW, atoi(arg), 0);
        else if (!strcmp(cmd, "ro")) SendMessageW(e, EM_SETREADONLY, atoi(arg), 0);
        else if (!strcmp(cmd, "pw")) SendMessageW(e, EM_SETPASSWORDCHAR, arg[0], 0);
        else if (!strcmp(cmd, "textmode")) printf("  textmode -> %ld\n", (long)SendMessageW(e, EM_SETTEXTMODE, atoi(arg), 0));
        else if (!strcmp(cmd, "protret")) protect_ret = atoi(arg);
        else if (!strcmp(cmd, "size")) { int w = atoi(arg), h = 100; char *c = strchr(arg, ','); if (c) h = atoi(c + 1); MoveWindow(e, 10, 10, w, h, TRUE); }
        else if (!strcmp(cmd, "show")) ShowWindow(e, atoi(arg) ? SW_SHOW : SW_HIDE);
        else if (!strcmp(cmd, "hidecaret")) printf("  HideCaret -> %d\n", HideCaret(e));
        else if (!strcmp(cmd, "paint")) { UpdateWindow(e); }
        else if (!strcmp(cmd, "scrollcaret")) SendMessageW(e, EM_SCROLLCARET, 0, 0);
        else if (!strcmp(cmd, "linescroll")) SendMessageW(e, EM_LINESCROLL, 0, atoi(arg));
        else if (!strcmp(cmd, "pos"))
        {
            POINTL pt = { -1, -1 };
            SendMessageW(e, EM_POSFROMCHAR, (WPARAM)&pt, atoi(arg));
            printf("  pos(%d) %ld,%ld lines %ld\n", atoi(arg), pt.x, pt.y, (long)SendMessageW(e, EM_GETLINECOUNT, 0, 0));
        }
        else if (!strcmp(cmd, "caret"))
        {
            GUITHREADINFO gi = { sizeof(gi) };
            POINT pt = { -1, -1 }, sp = { -1, -1 };
            GetCaretPos(&pt);
            GetGUIThreadInfo(GetCurrentThreadId(), &gi);
            SendMessageW(e, EM_GETSCROLLPOS, 0, (LPARAM)&sp);
            printf("  caret %ld,%ld owner %s blinking %d rc (%ld,%ld)-(%ld,%ld) focus %s scroll %ld,%ld firstline %ld\n", pt.x, pt.y,
                   gi.hwndCaret == e ? "edit" : gi.hwndCaret == other ? "other" : gi.hwndCaret ? "?" : "none", !!(gi.flags & GUI_CARETBLINKING),
                   gi.rcCaret.left, gi.rcCaret.top, gi.rcCaret.right, gi.rcCaret.bottom,
                   GetFocus() == e ? "edit" : GetFocus() == other ? "other" : "parent", sp.x, sp.y, (long)SendMessageW(e, EM_GETFIRSTVISIBLELINE, 0, 0));
        }
        else if (!strcmp(cmd, "insfmt"))
        {
            CHARRANGE cr;
            SendMessageW(e, EM_EXGETSEL, 0, (LPARAM)&cr);
            get_cf(-1, -1, &cf);
            printf("  sel %ld,%ld fmt mask %#lx bold %d italic %d size %ld face %ls\n", cr.cpMin, cr.cpMax, cf.dwMask,
                   !!(cf.dwEffects & CFE_BOLD), !!(cf.dwEffects & CFE_ITALIC), cf.yHeight, cf.szFaceName);
        }
        else if (!strcmp(cmd, "deffmt"))
        {
            memset(&cf, 0, sizeof(cf)); cf.cbSize = sizeof(cf);
            SendMessageW(e, EM_GETCHARFORMAT, SCF_DEFAULT, (LPARAM)&cf);
            printf("  default fmt bold %d italic %d size %ld face %ls\n", !!(cf.dwEffects & CFE_BOLD), !!(cf.dwEffects & CFE_ITALIC), cf.yHeight, cf.szFaceName);
        }
        else if (!strcmp(cmd, "gettext"))
        {
            WCHAR buf[1024]; int k, n = SendMessageW(e, WM_GETTEXT, 1024, (LPARAM)buf);
            printf("  text(%d, len %d) '", n, text_len());
            for (k = 0; k < n; k++) { if (buf[k] < 32 || buf[k] > 126) printf("\\x%x", buf[k]); else putchar(buf[k]); }
            printf("'\n");
        }
        else if (!strcmp(cmd, "sweep")) sweep();
        else if (!strcmp(cmd, "notif")) { printf("  notif: %s(EN_UPDATE x%d)\n", notif, updates); notif[0] = 0; updates = 0; }
        else if (!strcmp(cmd, "check")) check();
        else if (!strcmp(cmd, "tomtext"))
        {
            /* tomtext:a,b,STR */
            ITextRange *range; int a = atoi(arg), b; char *c = strchr(arg, ','), *s;
            b = atoi(c + 1); s = strchr(c + 1, ',') + 1;
            if (doc && ITextDocument_Range(doc, a, b, &range) == S_OK)
            {
                BSTR str = SysAllocString(unesc(s));
                printf("  SetText -> %#lx\n", ITextRange_SetText(range, str));
                SysFreeString(str); ITextRange_Release(range);
            }
        }
        else { printf("unknown command %s\n", cmd); return 1; }
    }
    return 0;
}
