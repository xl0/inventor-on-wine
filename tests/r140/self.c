/* IsDialogMessageW when msg.hwnd is the dialog itself (140 review finding 4): is anything searched, sent or
 * dispatched?  Dialog kinds: plain top-level window, real dialog, plain child container; chars incl. Alt+Space.
 * Build: x86_64-w64-mingw32-gcc -O2 -o self.exe self.c -luser32 */
#include <windows.h>
#include <stdio.h>

static WNDPROC dlg_wndproc, btn_wndproc;
static HWND btn;

static void log_msg( const char *who, UINT msg, WPARAM wp, LPARAM lp )
{
    switch (msg)
    {
    case WM_GETDLGCODE: printf( "   %s GETDLGCODE wp=%x lp=%s\n", who, (int)wp, lp ? "msg" : "0" ); break;
    case WM_GETTEXT: printf( "   %s GETTEXT\n", who ); break;
    case WM_COMMAND: printf( "   %s COMMAND id=%d code=%d\n", who, LOWORD(wp), HIWORD(wp) ); break;
    case BM_CLICK: printf( "   %s BM_CLICK\n", who ); break;
    case WM_CHAR: printf( "   %s got CHAR %x\n", who, (int)wp ); break;
    case WM_SYSCHAR: printf( "   %s got SYSCHAR %x\n", who, (int)wp ); break;
    case WM_SYSCOMMAND: printf( "   %s SYSCOMMAND %x lp=%x\n", who, (int)wp, (int)lp ); break;
    case DM_GETDEFID: printf( "   %s DM_GETDEFID\n", who ); break;
    }
}

static LRESULT CALLBACK dlg_log( HWND hwnd, UINT msg, WPARAM wp, LPARAM lp )
{
    log_msg( "dlg", msg, wp, lp );
    if (msg == WM_SYSCOMMAND) return 0;  /* no menu loop */
    return CallWindowProcW( dlg_wndproc, hwnd, msg, wp, lp );
}

static WNDPROC top_wndproc;
static LRESULT CALLBACK top_log( HWND hwnd, UINT msg, WPARAM wp, LPARAM lp )
{
    log_msg( "top", msg, wp, lp );
    if (msg == WM_SYSCOMMAND) return 0;  /* no menu loop */
    return CallWindowProcW( top_wndproc, hwnd, msg, wp, lp );
}

static LRESULT CALLBACK btn_log( HWND hwnd, UINT msg, WPARAM wp, LPARAM lp )
{
    log_msg( "btn", msg, wp, lp );
    return CallWindowProcW( btn_wndproc, hwnd, msg, wp, lp );
}

static INT_PTR CALLBACK dlg_proc( HWND hwnd, UINT msg, WPARAM wp, LPARAM lp ) { return FALSE; }

static void run( const char *name, HWND dlg, HWND focus )
{
    static const struct { UINT msg; WPARAM wp; LPARAM lp; const char *name; } keys[] =
    {
        { WM_CHAR, 'z', 0x002c0001, "CHAR z (button &Zed)" }, { WM_CHAR, 'q', 0x00100001, "CHAR q" },
        { WM_SYSCHAR, 'z', 0x202c0001, "SYSCHAR z" }, { WM_SYSCHAR, 'q', 0x20100001, "SYSCHAR q" },
        { WM_SYSCHAR, ' ', 0x20390001, "SYSCHAR space" }, { WM_CHAR, ' ', 0x00390001, "CHAR space" },
    };
    unsigned int i;
    MSG m;

    btn = CreateWindowW( L"BUTTON", L"&Zed", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 10, 10, 80, 24, dlg, (HMENU)100, 0, 0 );
    dlg_wndproc = (WNDPROC)SetWindowLongPtrW( dlg, GWLP_WNDPROC, (LONG_PTR)dlg_log );
    btn_wndproc = (WNDPROC)SetWindowLongPtrW( btn, GWLP_WNDPROC, (LONG_PTR)btn_log );
    SetForegroundWindow( GetAncestor( dlg, GA_ROOT ) );
    for (i = 0; i < sizeof(keys) / sizeof(keys[0]); i++)
    {
        MSG msg = { dlg, keys[i].msg, keys[i].wp, keys[i].lp };
        BOOL ret;
        SetFocus( focus ? btn : dlg );
        while (PeekMessageW( &m, 0, 0, 0, PM_REMOVE )) DispatchMessageW( &m );
        printf( " %s, msg.hwnd = dialog, %s, focus on the %s\n", name, keys[i].name, GetFocus() == dlg ? "dialog" : GetFocus() == btn ? "button" : "?" );
        ret = IsDialogMessageW( dlg, &msg );
        printf( "   -> %d\n", ret );
        fflush( stdout );
    }
    dlg_wndproc = NULL;
}

int main(void)
{
    struct { DLGTEMPLATE t; WORD menu, cls, title; } tmpl = {{0}};
    WNDCLASSW wc = {0};
    HWND top, dlg;

    setvbuf( stdout, NULL, _IONBF, 0 );
    wc.lpfnWndProc = DefWindowProcW;
    wc.lpszClassName = L"r140self";
    RegisterClassW( &wc );

    top = CreateWindowW( L"r140self", L"plain", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 50, 50, 300, 200, 0, 0, 0, 0 );
    run( "plain top-level", top, 0 );
    DestroyWindow( top );

    top = CreateWindowW( L"r140self", L"plain", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 50, 50, 300, 200, 0, 0, 0, 0 );
    run( "plain top-level (focus on its button)", top, (HWND)1 );
    DestroyWindow( top );

    tmpl.t.style = WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE;
    tmpl.t.cx = 200; tmpl.t.cy = 100;
    dlg = CreateDialogIndirectParamW( GetModuleHandleW( NULL ), &tmpl.t, 0, dlg_proc, 0 );
    run( "real dialog", dlg, 0 );
    DestroyWindow( dlg );

    top = CreateWindowW( L"r140self", L"plain", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 50, 50, 300, 200, 0, 0, 0, 0 );
    top_wndproc = (WNDPROC)SetWindowLongPtrW( top, GWLP_WNDPROC, (LONG_PTR)top_log );
    dlg = CreateWindowW( L"r140self", L"pane", WS_CHILD | WS_VISIBLE, 0, 0, 200, 100, top, 0, 0, 0 );
    run( "plain child", dlg, 0 );
    DestroyWindow( top );

    /* DS_CONTROL child dialog inside a real dialog: messages go to the outer dialog, msg.hwnd is the inner one */
    top = CreateDialogIndirectParamW( GetModuleHandleW( NULL ), &tmpl.t, 0, dlg_proc, 0 );
    top_wndproc = (WNDPROC)SetWindowLongPtrW( top, GWLP_WNDPROC, (LONG_PTR)top_log );
    CreateWindowW( L"BUTTON", L"&Zed", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 10, 60, 80, 24, top, (HMENU)101, 0, 0 );
    tmpl.t.style = WS_CHILD | WS_VISIBLE | DS_CONTROL;
    tmpl.t.cx = 100; tmpl.t.cy = 30;
    dlg = CreateDialogIndirectParamW( GetModuleHandleW( NULL ), &tmpl.t, top, dlg_proc, 0 );
    run( "DS_CONTROL child dialog (outer dialog has &Zed too)", dlg, 0 );
    DestroyWindow( top );
    return 0;
}
