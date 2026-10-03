/* 141: EM_SETCHARFORMAT(SCF_WORD | SCF_SELECTION) in rich edit controls: which characters get the
 * format per caret position / selection, modify flag, undo, and whether the layout and the caret
 * follow at once (Wine left the paragraph unwrapped and asserted in cursor_coords()).
 * Build: x86_64-w64-mingw32-gcc -O2 -o scf_word.exe scf_word.c -lgdi32 -luser32
 * Run:   scf_word.exe [word|sel|layout|caret|eop]   (default: all) */
#include <windows.h>
#include <richedit.h>
#include <stdio.h>
#include <string.h>

static HWND parent;

static HWND new_edit(const WCHAR *cls, DWORD style)
{
    HWND e = CreateWindowExW(0, cls, NULL, WS_CHILD | ES_MULTILINE | style, 10, 10, 400, 150, parent, NULL, NULL, NULL);
    if (!e) { printf("no window of class %ls (%lu)\n", cls, GetLastError()); exit(2); }
    return e;
}

static void set_text(HWND e, const WCHAR *t)
{
    CHARFORMATW cf = { sizeof(cf) };
    SendMessageW(e, WM_SETTEXT, 0, (LPARAM)t);
    cf.dwMask = CFM_BOLD | CFM_SIZE; cf.yHeight = 200;
    SendMessageW(e, EM_SETCHARFORMAT, SCF_ALL, (LPARAM)&cf);
    SendMessageW(e, EM_EMPTYUNDOBUFFER, 0, 0);
}

static int bold_at(HWND e, int from, int to)
{
    CHARFORMATW cf = { sizeof(cf) };
    SendMessageW(e, EM_SETSEL, from, to);
    SendMessageW(e, EM_GETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
    return (cf.dwMask & CFM_BOLD) ? !!(cf.dwEffects & CFE_BOLD) : -1;
}

/* one line: what a SCF_WORD bold request with the selection (from, to) did */
static void word_case(HWND e, const WCHAR *text, int from, int to)
{
    CHARFORMATW cf = { sizeof(cf) };
    char map[64];
    CHARRANGE cr;
    int i, len = lstrlenW(text), ret, mod, undo, ins;

    set_text(e, text);
    SendMessageW(e, EM_SETSEL, from, to);
    SendMessageW(e, EM_SETMODIFY, 0, 0);
    cf.dwMask = CFM_BOLD; cf.dwEffects = CFE_BOLD;
    ret = SendMessageW(e, EM_SETCHARFORMAT, SCF_WORD | SCF_SELECTION, (LPARAM)&cf);
    mod = SendMessageW(e, EM_GETMODIFY, 0, 0);
    undo = SendMessageW(e, EM_CANUNDO, 0, 0);
    SendMessageW(e, EM_EXGETSEL, 0, (LPARAM)&cr);
    memset(&cf, 0, sizeof(cf)); cf.cbSize = sizeof(cf);
    SendMessageW(e, EM_GETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
    ins = (cf.dwMask & CFM_BOLD) ? !!(cf.dwEffects & CFE_BOLD) : -1;
    for (i = 0; i <= len; i++) map[i] = bold_at(e, i, i + 1) ? 'B' : '-';
    map[len + 1] = 0;
    printf("  sel %2d,%2d ret %d modify %d canundo %d sel after %ld,%ld selfmt bold %d  map [%s]\n",
           from, to, ret, mod, undo, cr.cpMin, cr.cpMax, ins, map);
}

static void print_pos(HWND e, const char *what, int cp)
{
    POINTL pt = { -1, -1 }; POINT caret = { -1, -1 };
    SendMessageW(e, EM_POSFROMCHAR, (WPARAM)&pt, cp);
    GetCaretPos(&caret);
    printf("  %-28s pos(%d) %ld,%ld  caret %ld,%ld  focus %d lines %d\n", what, cp, pt.x, pt.y, caret.x, caret.y,
           GetFocus() == e, (int)SendMessageW(e, EM_GETLINECOUNT, 0, 0));
}

/* a size change through SCF_WORD: do EM_POSFROMCHAR and the caret follow without a paint? */
static void layout_case(const WCHAR *cls, const WCHAR *text, int cp, DWORD style, BOOL focus_first)
{
    HWND e = new_edit(cls, style);
    CHARFORMATW cf = { sizeof(cf) };
    CHARRANGE cr = { cp, cp };
    int len = lstrlenW(text);

    printf(" text '%ls' caret %d %s, %s\n", text, cp, (style & WS_VISIBLE) ? "visible" : "hidden",
           focus_first ? "focused before the format change" : "focused after the format change");
    set_text(e, text);
    SetFocus(focus_first ? e : parent);
    SendMessageW(e, EM_EXSETSEL, 0, (LPARAM)&cr);
    print_pos(e, "before", len);
    cf.dwMask = CFM_SIZE; cf.yHeight = 600;
    SendMessageW(e, EM_SETCHARFORMAT, SCF_WORD | SCF_SELECTION, (LPARAM)&cf);
    print_pos(e, "after EM_SETCHARFORMAT", len);
    if (!focus_first) { SetFocus(e); print_pos(e, "after SetFocus", len); }
    SendMessageW(e, EM_EXSETSEL, 0, (LPARAM)&cr);
    print_pos(e, "after EM_EXSETSEL", len);
    SetFocus(parent);
    DestroyWindow(e);
}

/* does the caret of a focused control follow a size change made with FLAGS? */
static void caret_case(const WCHAR *cls, WPARAM flags, const char *name)
{
    HWND e = new_edit(cls, WS_VISIBLE);
    CHARFORMATW cf = { sizeof(cf) };

    printf(" caret, %s:\n", name);
    set_text(e, L"ab cd");
    SetFocus(e);
    SendMessageW(e, EM_SETSEL, 4, 4);
    print_pos(e, "before", 5);
    cf.dwMask = CFM_SIZE; cf.yHeight = 600;
    SendMessageW(e, EM_SETCHARFORMAT, flags, (LPARAM)&cf);
    print_pos(e, "after EM_SETCHARFORMAT", 5);
    SetFocus(parent);
    DestroyWindow(e);
}

static int height_at(HWND e, int from, int to)
{
    CHARFORMATW cf = { sizeof(cf) };
    SendMessageW(e, EM_SETSEL, from, to);
    SendMessageW(e, EM_GETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
    return (cf.dwMask & CFM_SIZE) ? cf.yHeight : -1;
}

/* Inventor's Edit Dimension: a format set with the caret at the end of the text, then the user
 * deletes everything and types: which size does the new text get? */
static void eop_case(HWND e, WPARAM flags, const char *name)
{
    CHARFORMATW cf = { sizeof(cf) };
    int text, eop, typed;

    set_text(e, L"<<>>");
    SendMessageW(e, EM_SETSEL, 4, 4);
    cf.dwMask = CFM_SIZE; cf.yHeight = 600;
    SendMessageW(e, EM_SETCHARFORMAT, flags, (LPARAM)&cf);
    text = height_at(e, 0, 4); eop = height_at(e, 4, 5);
    SendMessageW(e, EM_SETSEL, 0, -1);
    SendMessageW(e, EM_REPLACESEL, TRUE, (LPARAM)L"");
    SendMessageW(e, EM_REPLACESEL, TRUE, (LPARAM)L"X");
    typed = height_at(e, 0, 1);
    printf("  %-24s text %d final paragraph mark %d; after delete all + type: %d\n", name, text, eop, typed);
}

int main(int argc, char **argv)
{
    static const WCHAR *classes[] = { L"RICHEDIT50W", L"RichEdit20W" };
    /* text 0..3; the map has one more cell for the final paragraph mark */
    static const WCHAR *texts[] = { L"one two  three", L"<<>>", L"ab, cd.", L"x\ryz" };
    const char *mode = argc > 1 ? argv[1] : "all";
    BOOL all = !strcmp(mode, "all");
    int c, t, i;

    setvbuf(stdout, NULL, _IONBF, 0);
    LoadLibraryW(L"msftedit.dll");
    LoadLibraryW(L"riched20.dll");
    parent = CreateWindowExW(0, L"static", L"r141", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 50, 50, 500, 300, NULL, NULL, NULL, NULL);
    SetForegroundWindow(parent);

    for (c = 0; c < 2; c++)
    {
        HWND e = new_edit(classes[c], 0);
        printf("== %ls\n", classes[c]);
        if (all || !strcmp(mode, "word"))
            for (t = 0; t < 4; t++)
            {
                printf(" word: text %d, empty selection at each position\n", t);
                for (i = 0; i <= lstrlenW(texts[t]); i++) word_case(e, texts[t], i, i);
            }
        if (all || !strcmp(mode, "sel"))
        {
            static const int sels[][2] = { {1, 2}, {0, 3}, {1, 5}, {3, 4}, {4, 7}, {5, 11}, {7, 9}, {12, 14}, {12, 15}, {0, -1} };
            printf(" sel: '%ls', non-empty selections\n", texts[0]);
            for (i = 0; i < sizeof(sels) / sizeof(sels[0]); i++) word_case(e, texts[0], sels[i][0], sels[i][1]);
        }
        if (all || !strcmp(mode, "eop"))
        {
            printf(" eop: '<<>>' size 200, size 600 set with the caret at the end\n");
            eop_case(e, SCF_SELECTION, "SCF_SELECTION");
            eop_case(e, SCF_WORD | SCF_SELECTION, "SCF_WORD | SCF_SELECTION");
        }
        DestroyWindow(e);
        if (all || !strcmp(mode, "layout"))
        {
            printf(" layout:\n");
            layout_case(classes[c], L"ab cd", 4, WS_VISIBLE, TRUE);
            layout_case(classes[c], L"ab cd", 4, WS_VISIBLE, FALSE);
            layout_case(classes[c], L"ab cd", 4, 0, FALSE);
            layout_case(classes[c], L"<<>>", 4, WS_VISIBLE, TRUE);
            layout_case(classes[c], L"<<>>", 4, WS_VISIBLE, FALSE);
            layout_case(classes[c], L"ab cd", 5, WS_VISIBLE, TRUE);
        }
        if (all || !strcmp(mode, "caret"))
        {
            caret_case(classes[c], SCF_ALL, "SCF_ALL");
            caret_case(classes[c], SCF_DEFAULT, "SCF_DEFAULT");
            caret_case(classes[c], SCF_SELECTION, "SCF_SELECTION");
            caret_case(classes[c], SCF_WORD | SCF_SELECTION, "SCF_WORD | SCF_SELECTION");
        }
    }
    return 0;
}
