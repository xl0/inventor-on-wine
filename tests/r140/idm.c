/* Table-driven IsDialogMessageW / GetNextDlgTabItem / GetNextDlgGroupItem probe (issue 140).
 * Every case = a window tree + (hwndDlg, msg.hwnd, focus) + one operation, run in a child process;
 * the driver kills a child whose case does not finish in 3 s and reports "!! HANG" (crash: "!! CRASH").
 * Output is deterministic (window names, no handles): diff the Wine output against the VM's.
 *
 *   idm.exe            run all cases        idm.exe N [M]   cases N..M in-process (no watchdog)
 *   idm.exe list       print the case table
 *
 * Tree spec: tokens "name:K[flags]'text'#id(parent)".
 *   K: F frame (plain top-level class, DefWindowProc)  D real dialog (child if it has a parent)
 *      C container (plain child class)  X plain control (dlg code 0)  W control wanting chars
 *      B pushbutton  b default pushbutton  R auto radio button  S static  E edit  G group box
 *   flags: h no WS_VISIBLE  d WS_DISABLED  c WS_EX_CONTROLPARENT  t WS_TABSTOP  g WS_GROUP  s DS_CONTROL
 *          (s on other kinds: the same style bit 0x400)
 *          after the focus is set: H ShowWindow(SW_HIDE)  P SetWindowPos(SWP_HIDEWINDOW)  Z EnableWindow(FALSE)
 * Build: x86_64-w64-mingw32-gcc -O2 -o idm.exe idm.c -luser32 */
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define MAXW 24
struct win { char name[8]; char kind; char flags[12]; WCHAR text[32]; int id; int parent; HWND hwnd; WNDPROC proc; };
static struct win wins[MAXW];
static int nwins;
static BOOL logging;

struct tree { const char *spec; const char *dlg, *tgt, *foc; };

/* inner (i) and outer (o) content of the pane trees */
#define INN " x:X[t](p) a:B'&Abc'[t](p) r1:R'R&1'[gt](p) r2:R'R&2'(p)"
#define OUT_PRE "f:F y:X[t](f)"
#define OUT_POST " z:B'&Zed'[t](f) ok:b'Ok'#1[t](f)"
#define PANE(fl) OUT_PRE " p:C[" fl "](f)" INN OUT_POST
#define PANE3(fl) { PANE(fl), "f", "x", "x" }, { PANE(fl), "f", "y", "y" }, { PANE(fl), "p", "x", "x" }
#define FLAT "x:X[t](f) a:B'&Abc'[t](f) s:S'&Stat'(f) e:E[t](f) r1:R'R&1'[gt](f) r2:R'R&2'(f) ok:b'&Ok'#1[gt](f) ca:B'Cancel'#2[t](f)"
#define DLGIN " x:X[t](d) a:B'&Abc'[t](d) ok2:b'O&k2'#1[t](d)"
#define NEST(fl) "f:D y:X[t](f) d:D[" fl "](f)" DLGIN " z:B'&Zed'[t](f) ok:b'Ok'#1[t](f)"
#define NEST3(fl) { NEST(fl), "f", "x", "x" }, { NEST(fl), "d", "x", "x" }, { NEST(fl), "f", "y", "y" }

static const struct tree trees[] =
{
    /* 0-5: flat frame / flat dialog */
    { "f:F " FLAT, "f", "x", "x" },
    { "f:F " FLAT, "f", "a", "a" },
    { "f:F " FLAT, "f", "e", "e" },
    { "f:F " FLAT, "f", "r1", "r1" },
    { "f:D " FLAT, "f", "x", "x" },
    { "f:D " FLAT, "f", "r1", "r1" },
    /* 6-: pane variants; msg.hwnd inside (dlg = frame), outside, inside (dlg = pane) */
    PANE3(""), PANE3("c"), PANE3("h"), PANE3("ch"), PANE3("d"), PANE3("cd"),
    PANE3("H"), PANE3("cH"), PANE3("P"), PANE3("cP"), PANE3("Z"), PANE3("cZ"),
    /* 42-: msg.hwnd itself hidden / disabled, dialog hidden / disabled */
    { "f:F x:X[th](f) a:B'&Abc'[t](f) z:B'&Zed'[t](f)", "f", "x", "x" },
    { "f:F x:X[td](f) a:B'&Abc'[t](f) z:B'&Zed'[t](f)", "f", "x", "x" },
    { "f:F y:X[t](f) a:B'&Abc'[th](f) z:B'&Zed'[t](f)", "f", "a", "y" },
    { "f:F y:X[t](f) a:B'&Abc'[td](f) z:B'&Zed'[t](f)", "f", "a", "y" },
    { "f:F[h] x:X[t](f) a:B'&Abc'[t](f) z:B'&Zed'[t](f)", "f", "x", "x" },
    { "f:F[Z] x:X[t](f) a:B'&Abc'[t](f) z:B'&Zed'[t](f)", "f", "x", "x" },
    /* focus elsewhere than msg.hwnd */
    { "f:F x:X[t](f) a:B'&Abc'[t](f) z:B'&Zed'[t](f)", "f", "x", "z" },
    { "f:F x:X[t](f) a:B'&Abc'[t](f) z:B'&Zed'[t](f)", "f", "f", "f" },
    /* no visible / enabled control at all */
    { "f:F x:X[th](f) a:B'&Abc'[th](f)", "f", "x", "x" },
    { "f:F x:X[td](f) a:B'&Abc'[td](f)", "f", "x", "x" },
    { "f:F x:X[t](f)", "f", "x", "x" },
    { "f:F x:X(f)", "f", "x", "x" },
    { "f:F", "f", "f", "f" },
    { "f:F p:C[c](f)", "f", "p", "p" },
    { "f:F p:C[c](f) pp:C[c](p)", "f", "pp", "pp" },
    { "f:F p:C[c](f) x:X[th](p) a:B'&Abc'[td](p)", "f", "x", "x" },
    { "f:F p:C[c](f) x:X[th](p) a:B'&Abc'[td](p)", "f", "f", "f" },
    { "f:F p:C[ch](f) x:X[t](p)", "f", "x", "x" },
    { "f:F p:C[h](f) x:X[t](p)", "f", "x", "x" },
    { "f:F p:C(f) x:X[t](p)", "f", "x", "x" },
    { "f:F p:C[c](f) x:X[t](p)", "f", "x", "x" },
    /* dialog under a hidden parent (Inventor's closed dock pane), plain and with control parents */
    { "f:F q:C[h](f) p:C(q)" INN " z:B'&Zed'[t](f)", "p", "x", "x" },
    { "f:F q:C[H](f) p:C(q)" INN " z:B'&Zed'[t](f)", "p", "x", "x" },
    { "f:F q:C[h](f) p:C(q) w:C[t](p) v:C[h](w) x:X(v) u:C(w) y:X(u) s:S'l&bl'(u)", "p", "x", "x" },
    { "f:F q:C[h](f) p:C(q) w:C[tc](p) v:C[hc](w) x:X(v) u:C[c](w) y:X(u) s:S'l&bl'(u)", "p", "x", "x" },
    { "f:F q:C[P](f) p:C(q) w:C[tc](p) v:C[Pc](w) x:X[t](v) u:C[c](w) y:X[t](u) s:S'l&bl'(u)", "p", "x", "x" },
    { "f:F q:C(f) p:C(q) w:C[tc](p) v:C[hc](w) x:X(v) u:C[c](w) y:X(u) a:B'&Abc'[t](u)", "p", "x", "x" },
    { "f:F q:C(f) p:C(q) w:C[tc](p) v:C[hc](w) vv:C[c](v) x:X(vv) u:C[c](w) y:X(u) a:B'&Abc'[t](u)", "p", "x", "x" },
    /* nested child dialogs */
    NEST3(""), NEST3("s"), NEST3("c"), NEST3("sc"), NEST3("sh"), NEST3("sch"), NEST3("scd"), NEST3("h"),
    /* mnemonics: static before hidden / disabled / last, disabled match, two matches, group box */
    { "f:F x:X[t](f) s:S'&Abc'(f) e:E[th](f) e2:E[t](f)", "f", "x", "x" },
    { "f:F x:X[t](f) s:S'&Abc'(f) e:E[td](f) e2:E[t](f)", "f", "x", "x" },
    { "f:F x:X[t](f) e2:E[t](f) s:S'&Abc'(f)", "f", "x", "x" },
    { "f:F x:X[t](f) s:S'&Abc'(f)", "f", "x", "x" },
    { "f:F x:X[t](f) a:B'&Abc'[td](f) z:B'&Zed'[t](f)", "f", "x", "x" },
    { "f:F a1:B'&Abc'[t](f) x:X[t](f) a2:B'&Abd'[t](f)", "f", "x", "x" },
    { "f:F a1:B'&Abc'[t](f) x:X[t](f) a2:B'&Abd'[t](f)", "f", "a1", "a1" },
    { "f:F x:X[t](f) g:G'&Abc'(f) e:E[t](f)", "f", "x", "x" },
    { "f:F x:X[t](f) w:W'&Abc'[t](f) z:B'&Zed'[t](f)", "f", "w", "w" },
    /* a button that has children (non control parent / control parent) */
    { "f:F y:X[t](f) p:B'&Pane'[t](f) x:X[t](p) a:B'&Abc'[t](p)", "f", "x", "x" },
    { "f:F y:X[t](f) p:B'&Pane'[tc](f) x:X[t](p) a:B'&Abc'[t](p)", "f", "y", "y" },
    /* deeper control parents, hidden one in the middle */
    { "f:F y:X[t](f) p:C[c](f) pp:C[ch](p) x:X[t](pp) a:B'&Abc'[t](pp) z:B'&Zed'[t](p)", "f", "x", "x" },
    { "f:F y:X[t](f) p:C[c](f) pp:C[cd](p) x:X[t](pp) a:B'&Abc'[t](pp) z:B'&Zed'[t](p)", "f", "x", "x" },
    { "f:F y:X[t](f) p:C[c](f) pp:C[c](p) x:X[t](pp) a:B'&Abc'[t](pp) z:B'&Zed'[t](p)", "f", "x", "x" },
    { "f:F y:X[t](f) p:C[ch](f) pp:C[c](p) x:X[t](pp) a:B'&Abc'[t](pp) z:B'&Zed'[t](p)", "f", "x", "x" },
    { "f:F p:C[ch](f) pp:C[c](p) x:X[t](pp) a:B'&Abc'[t](pp)", "f", "x", "x" },
    { "f:F p:C[h](f) pp:C(p) x:X[t](pp) a:B'&Abc'[t](pp)", "f", "x", "x" },
    { "f:F p:C[h](f) pp:C(p) x:X[t](pp) a:B'&Abc'[t](pp)", "pp", "x", "x" },
    /* 112-: msg.hwnd is the hidden / disabled dialog itself */
    { "f:F[h] x:X[t](f) a:B'&Abc'[t](f)", "f", "f", "f" },
    { "f:F y:X[t](f) p:C[h](f) x:X[t](p) a:B'&Abc'[t](p) z:B'&Zed'[t](f)", "p", "p", "y" },
    { "f:F y:X[t](f) p:C[d](f) x:X[t](p) a:B'&Abc'[t](p) z:B'&Zed'[t](f)", "p", "p", "y" },
    /* two hidden levels, the conformance test's shape */
    { "f:F a:B'&Abc'[t](f) p:C[h](f) pp:C[h](p) x:S'child'(pp) z:B'&Zed'[t](p)", "f", "x", "a" },
    { "f:F a:B'&Abc'[t](f) p:C[d](f) pp:C[h](p) x:S'child'(pp) z:B'&Zed'[t](p)", "f", "x", "a" },
    /* 117-: Inventor's closed iLogic pane (MFC control bars q, p; WinForms controls below; focus on x, or on t) */
#define INV "f:F q:C[P](f) tw:C(q) p:C[s](tw) w:C[tc]'iLogic'(p) v:C[Pc](w) x:C[t](v) r:C[c]'Rules'(x) t:C[t](r) " \
            "h1:C[h](t) h2:C[h](t) u:C[c](w) y:C(u) s:S'iLogic'(u)"
    { INV, "p", "x", "x" },
    { INV, "p", "t", "t" },
};

enum { OP_CH_A, OP_CH_Z, OP_CH_Q, OP_SYS_A, OP_SYS_Q, OP_TAB, OP_STAB, OP_DOWN, OP_UP, OP_RET, OP_ESC,
       OP_TABN, OP_TABP, OP_GRPN, OP_GRPP, NOPS };
static const char *op_names[NOPS] = { "CH_A", "CH_Z", "CH_Q", "SYS_A", "SYS_Q", "TAB", "STAB", "DOWN", "UP", "RET", "ESC",
                                      "TABN", "TABP", "GRPN", "GRPP" };
#define NTREES ((int)(sizeof(trees) / sizeof(trees[0])))
#define NCASES (NTREES * NOPS)

static const char *wname( HWND hwnd )
{
    static char buf[4][24];
    static int idx;
    char *ret;
    int i;
    if (!hwnd) return "0";
    for (i = 0; i < nwins; i++) if (wins[i].hwnd == hwnd) return wins[i].name;
    ret = buf[idx++ & 3];
    sprintf( ret, "?%s", IsWindow( hwnd ) ? "win" : "bad" );
    return ret;
}

static struct win *find_win( const char *name )
{
    int i;
    for (i = 0; i < nwins; i++) if (!strcmp( wins[i].name, name )) return &wins[i];
    printf( "bad name %s\n", name );
    exit( 2 );
}

static LRESULT CALLBACK log_proc( HWND hwnd, UINT msg, WPARAM wp, LPARAM lp )
{
    struct win *w = (struct win *)GetPropA( hwnd, "r140" );
    LRESULT ret;

    if (!w) return DefWindowProcW( hwnd, msg, wp, lp );
    if (logging) switch (msg)
    {
    case WM_GETDLGCODE:
        if (lp) printf( "  %s GETDLGCODE wp=%x lp=msg(%x)", w->name, (int)wp, ((MSG *)lp)->message );
        else printf( "  %s GETDLGCODE wp=%x lp=0", w->name, (int)wp );
        break;
    case DM_GETDEFID: printf( "  %s DM_GETDEFID", w->name ); break;
    case DM_SETDEFID: printf( "  %s DM_SETDEFID %d\n", w->name, (int)wp ); break;
    case WM_NEXTDLGCTL:
        if (LOWORD(lp)) printf( "  %s NEXTDLGCTL hwnd=%s\n", w->name, wname( (HWND)wp ) );
        else printf( "  %s NEXTDLGCTL prev=%d\n", w->name, !!wp );
        break;
    case WM_COMMAND: printf( "  %s COMMAND id=%d code=%d from=%s\n", w->name, LOWORD(wp), HIWORD(wp), wname( (HWND)lp ) ); break;
    case BM_CLICK: printf( "  %s BM_CLICK\n", w->name ); break;
    case BM_SETSTYLE: printf( "  %s BM_SETSTYLE %x\n", w->name, (int)wp ); break;
    case BM_SETCHECK: printf( "  %s BM_SETCHECK %d\n", w->name, (int)wp ); break;
    case BM_GETCHECK: printf( "  %s BM_GETCHECK\n", w->name ); break;
    case WM_SETFOCUS: printf( "  %s SETFOCUS\n", w->name ); break;
    case WM_KILLFOCUS: printf( "  %s KILLFOCUS\n", w->name ); break;
    case WM_KEYDOWN: printf( "  %s dispatched KEYDOWN %x\n", w->name, (int)wp ); break;
    case WM_CHAR: printf( "  %s dispatched CHAR %x\n", w->name, (int)wp ); break;
    case WM_SYSCHAR: printf( "  %s dispatched SYSCHAR %x\n", w->name, (int)wp ); break;
    case WM_SYSCOMMAND: printf( "  %s SYSCOMMAND %x\n", w->name, (int)wp ); break;
    case WM_GETTEXT: printf( "  %s GETTEXT\n", w->name ); break;
    case WM_GETTEXTLENGTH: printf( "  %s GETTEXTLENGTH\n", w->name ); break;
    case EM_SETSEL: printf( "  %s EM_SETSEL %d %d\n", w->name, (int)wp, (int)lp ); break;
    }

    if (msg == WM_GETDLGCODE && w->kind == 'W') ret = DLGC_WANTCHARS;
    else if (msg == WM_SYSCOMMAND) ret = 0;  /* no menu loop */
    else ret = CallWindowProcW( w->proc, hwnd, msg, wp, lp );

    if (logging && (msg == WM_GETDLGCODE || msg == DM_GETDEFID)) printf( " -> %x\n", (int)ret );
    return ret;
}

static INT_PTR CALLBACK dlg_proc( HWND hwnd, UINT msg, WPARAM wp, LPARAM lp )
{
    return FALSE;
}

static void parse_tree( const char *spec )
{
    char buf[512], *tok, *p, *q;

    nwins = 0;
    memset( wins, 0, sizeof(wins) );
    strcpy( buf, spec );
    for (tok = strtok( buf, " " ); tok; tok = strtok( NULL, " " ))
    {
        struct win *w = &wins[nwins];
        p = strchr( tok, ':' );
        *p++ = 0;
        strcpy( w->name, tok );
        w->kind = *p++;
        w->id = 100 + nwins;
        w->parent = -1;
        while (*p)  /* attributes in any order */
        {
            if (*p == '[') { q = strchr( p, ']' ); *q = 0; strcpy( w->flags, p + 1 ); p = q + 1; }
            else if (*p == '\'') { q = strchr( p + 1, '\'' ); *q = 0; MultiByteToWideChar( CP_ACP, 0, p + 1, -1, w->text, 32 ); p = q + 1; }
            else if (*p == '#') w->id = strtol( p + 1, &p, 10 );
            else if (*p == '(') { q = strchr( p, ')' ); *q = 0; w->parent = find_win( p + 1 ) - wins; p = q + 1; }
            else { printf( "bad spec at '%s'\n", p ); exit( 2 ); }
        }
        nwins++;
    }
}

static void create_tree(void)
{
    int i;

    for (i = 0; i < nwins; i++)
    {
        struct win *w = &wins[i];
        HWND parent = w->parent >= 0 ? wins[w->parent].hwnd : 0;
        DWORD style = parent ? WS_CHILD : WS_OVERLAPPEDWINDOW, ex = 0;
        const WCHAR *cls = NULL;

        if (!strchr( w->flags, 'h' )) style |= WS_VISIBLE;
        if (strchr( w->flags, 'd' )) style |= WS_DISABLED;
        if (strchr( w->flags, 't' )) style |= WS_TABSTOP;
        if (strchr( w->flags, 'g' )) style |= WS_GROUP;
        if (strchr( w->flags, 'c' )) ex |= WS_EX_CONTROLPARENT;
        if (strchr( w->flags, 's' )) style |= DS_CONTROL;
        switch (w->kind)
        {
        case 'F': cls = L"r140frame"; break;
        case 'C': case 'X': case 'W': cls = L"r140ctl"; break;
        case 'B': cls = L"BUTTON"; style |= BS_PUSHBUTTON; break;
        case 'b': cls = L"BUTTON"; style |= BS_DEFPUSHBUTTON; break;
        case 'R': cls = L"BUTTON"; style |= BS_AUTORADIOBUTTON; break;
        case 'G': cls = L"BUTTON"; style |= BS_GROUPBOX; break;
        case 'S': cls = L"STATIC"; break;
        case 'E': cls = L"EDIT"; style |= WS_BORDER; break;
        case 'D':
        {
            struct { DLGTEMPLATE t; WORD menu, cls, title; } tmpl = {{0}};
            if (!parent) style |= WS_CAPTION | WS_POPUP;
            tmpl.t.style = style;
            tmpl.t.dwExtendedStyle = ex;
            tmpl.t.x = 5 + 3 * i; tmpl.t.y = 5 + 3 * i; tmpl.t.cx = 200; tmpl.t.cy = 150;
            w->hwnd = CreateDialogIndirectParamW( GetModuleHandleW( NULL ), &tmpl.t, parent, dlg_proc, 0 );
            break;
        }
        default: printf( "bad kind %c\n", w->kind ); exit( 2 );
        }
        if (cls) w->hwnd = CreateWindowExW( ex, cls, w->text, style, parent ? 5 + 6 * i : 50, parent ? 5 + 6 * i : 50,
                                           parent ? 150 : 500, parent ? 100 : 400, parent, parent ? (HMENU)(INT_PTR)w->id : 0,
                                           GetModuleHandleW( NULL ), NULL );
        if (!w->hwnd) { printf( "failed to create %s: %lu\n", w->name, GetLastError() ); exit( 2 ); }
        SetPropA( w->hwnd, "r140", w );
        w->proc = (WNDPROC)SetWindowLongPtrW( w->hwnd, GWLP_WNDPROC, (LONG_PTR)log_proc );
    }
}

static void pump(void)
{
    MSG msg;
    while (PeekMessageW( &msg, 0, 0, 0, PM_REMOVE )) DispatchMessageW( &msg );
}

static void run_case( int n )
{
    const struct tree *t = &trees[n / NOPS];
    int i, op = n % NOPS;
    HWND dlg, tgt, foc, res = 0;
    BYTE keys[256] = {0};
    MSG msg = {0};
    BOOL ret = 0;

    parse_tree( t->spec );
    printf( "== %d.%s dlg=%s tgt=%s foc=%s | %s\n", n / NOPS, op_names[op], t->dlg, t->tgt, t->foc, t->spec );
    create_tree();
    dlg = find_win( t->dlg )->hwnd;
    tgt = find_win( t->tgt )->hwnd;
    foc = find_win( t->foc )->hwnd;
    for (i = 0; i < 20; i++)  /* another process may still hold the foreground */
    {
        SetForegroundWindow( wins[0].hwnd );
        SetActiveWindow( wins[0].hwnd );
        SetFocus( foc );
        pump();
        if (GetActiveWindow() == wins[0].hwnd || !IsWindowVisible( wins[0].hwnd ) || !IsWindowEnabled( wins[0].hwnd )) break;
        Sleep( 50 );
    }
    for (i = 0; i < nwins; i++)
    {
        if (strchr( wins[i].flags, 'H' )) ShowWindow( wins[i].hwnd, SW_HIDE );
        if (strchr( wins[i].flags, 'P' )) SetWindowPos( wins[i].hwnd, 0, 0, 0, 0, 0, SWP_HIDEWINDOW | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE );
        if (strchr( wins[i].flags, 'Z' )) EnableWindow( wins[i].hwnd, FALSE );
    }
    pump();
    printf( "  pre: focus=%s active=%s\n", wname( GetFocus() ), wname( GetActiveWindow() ) );
    fflush( stdout );

    msg.hwnd = tgt;
    if (op == OP_STAB) keys[VK_SHIFT] = 0x80;
    SetKeyboardState( keys );
    logging = TRUE;
    switch (op)
    {
    case OP_CH_A:  msg.message = WM_CHAR; msg.wParam = 'a'; msg.lParam = 0x001e0001; break;
    case OP_CH_Z:  msg.message = WM_CHAR; msg.wParam = 'z'; msg.lParam = 0x002c0001; break;
    case OP_CH_Q:  msg.message = WM_CHAR; msg.wParam = 'q'; msg.lParam = 0x00100001; break;
    case OP_SYS_A: msg.message = WM_SYSCHAR; msg.wParam = 'a'; msg.lParam = 0x201e0001; break;
    case OP_SYS_Q: msg.message = WM_SYSCHAR; msg.wParam = 'q'; msg.lParam = 0x20100001; break;
    case OP_TAB:
    case OP_STAB:  msg.message = WM_KEYDOWN; msg.wParam = VK_TAB; msg.lParam = 0x000f0001; break;
    case OP_DOWN:  msg.message = WM_KEYDOWN; msg.wParam = VK_DOWN; msg.lParam = 0x01500001; break;
    case OP_UP:    msg.message = WM_KEYDOWN; msg.wParam = VK_UP; msg.lParam = 0x01480001; break;
    case OP_RET:   msg.message = WM_KEYDOWN; msg.wParam = VK_RETURN; msg.lParam = 0x001c0001; break;
    case OP_ESC:   msg.message = WM_KEYDOWN; msg.wParam = VK_ESCAPE; msg.lParam = 0x00010001; break;
    case OP_TABN:  res = GetNextDlgTabItem( dlg, tgt, FALSE ); break;
    case OP_TABP:  res = GetNextDlgTabItem( dlg, tgt, TRUE ); break;
    case OP_GRPN:  res = GetNextDlgGroupItem( dlg, tgt, FALSE ); break;
    case OP_GRPP:  res = GetNextDlgGroupItem( dlg, tgt, TRUE ); break;
    }
    if (msg.message) ret = IsDialogMessageW( dlg, &msg );
    logging = FALSE;
    if (msg.message) printf( "  -> ret=%d focus=%s\n", ret, wname( GetFocus() ) );
    else printf( "  -> %s\n", wname( res ) );
    fflush( stdout );

    memset( keys, 0, sizeof(keys) );
    SetKeyboardState( keys );
    pump();
    DestroyWindow( wins[0].hwnd );
    pump();
}

int main( int argc, char **argv )
{
    WNDCLASSW wc = {0};
    HANDLE map;
    LONG *prog;
    int i, start = 0, end = NCASES;

    setvbuf( stdout, NULL, _IONBF, 0 );
    if (argc > 1 && !strcmp( argv[1], "list" ))
    {
        for (i = 0; i < NTREES; i++) printf( "%d dlg=%s tgt=%s foc=%s | %s\n", i, trees[i].dlg, trees[i].tgt, trees[i].foc, trees[i].spec );
        return 0;
    }

    map = CreateFileMappingA( INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, 4096, "r140-progress" );
    prog = MapViewOfFile( map, FILE_MAP_ALL_ACCESS, 0, 0, 4096 );

    if (argc > 1)  /* child: cases start..end-1 */
    {
        start = atoi( argv[1] );
        if (argc > 2 && strcmp( argv[2], "-" )) end = atoi( argv[2] ) + 1;
        else if (argc <= 2) end = start + 1;
        wc.lpfnWndProc = DefWindowProcW;
        wc.hInstance = GetModuleHandleW( NULL );
        wc.hCursor = LoadCursorW( 0, (WCHAR *)IDC_ARROW );
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.lpszClassName = L"r140frame";
        RegisterClassW( &wc );
        wc.lpszClassName = L"r140ctl";
        RegisterClassW( &wc );
        for (i = start; i < end && i < NCASES; i++)
        {
            InterlockedExchange( prog, i );
            run_case( i );
        }
        InterlockedExchange( prog, NCASES );
        return 0;
    }

    while (start < NCASES)  /* driver */
    {
        STARTUPINFOA si = { sizeof(si) };
        PROCESS_INFORMATION pi;
        char cmd[MAX_PATH + 32], path[MAX_PATH];
        int cur = start, last = -1;
        DWORD idle = 0, code = 0;

        GetModuleFileNameA( NULL, path, sizeof(path) );
        sprintf( cmd, "\"%s\" %d -", path, start );
        InterlockedExchange( prog, start );
        if (!CreateProcessA( NULL, cmd, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi )) { printf( "CreateProcess failed\n" ); return 2; }
        while (WaitForSingleObject( pi.hProcess, 100 ) == WAIT_TIMEOUT)
        {
            cur = *prog;
            if (cur != last) { last = cur; idle = 0; }
            else if ((idle += 100) >= 3000) break;
        }
        cur = *prog;
        if (WaitForSingleObject( pi.hProcess, 0 ) == WAIT_TIMEOUT)
        {
            TerminateProcess( pi.hProcess, 99 );
            WaitForSingleObject( pi.hProcess, 5000 );
            printf( "\n  !! HANG %d.%s (watchdog 3 s)\n", cur / NOPS, op_names[cur % NOPS] );
        }
        else
        {
            GetExitCodeProcess( pi.hProcess, &code );
            if (cur < NCASES) printf( "\n  !! CRASH %d.%s exit code %#lx\n", cur / NOPS, op_names[cur % NOPS], code );
        }
        CloseHandle( pi.hProcess );
        CloseHandle( pi.hThread );
        start = cur + 1;
    }
    printf( "done: %d cases\n", NCASES );
    return 0;
}
