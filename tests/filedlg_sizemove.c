/* filedlg_sizemove.c: modal, resizable common Open dialog (GetOpenFileName, Explorer style) owned by a
 * disabled main window; logs the size-move messages the dialog gets like sizemove_log.c, plus WM_SIZE
 * and the client size, so a WM driven resize can be checked for delivery and relayout (124).
 * The dialog title is "filedlg_sizemove". After SECS (default 60) the dialog is cancelled; prints
 * "summary enter N exit N size WxH" (final client size) and returns 1 on unbalanced pairs.
 * Usage: filedlg_sizemove.exe [SECS]
 * Build: x86_64-w64-mingw32-gcc -O2 -o filedlg_sizemove.exe filedlg_sizemove.c -lcomdlg32 */
#include <windows.h>
#include <commdlg.h>
#include <stdio.h>
#include <stdlib.h>

static DWORD start;
static int enter, leave, depth, secs = 60;
static WNDPROC old_proc;
static SIZE last;

static void log_msg( const char *what, HWND hwnd )
{
    RECT rc, client;
    GetWindowRect( hwnd, &rc );
    GetClientRect( hwnd, &client );
    last.cx = client.right;
    last.cy = client.bottom;
    printf( "%6lu %-20s depth %d rect %ld,%ld-%ld,%ld client %ldx%ld\n", GetTickCount() - start, what, depth,
            rc.left, rc.top, rc.right, rc.bottom, client.right, client.bottom );
    fflush( stdout );
}

static LRESULT CALLBACK dlg_proc( HWND hwnd, UINT msg, WPARAM wp, LPARAM lp )
{
    switch (msg)
    {
    case WM_ENTERSIZEMOVE: enter++; depth++; log_msg( "WM_ENTERSIZEMOVE", hwnd ); break;
    case WM_EXITSIZEMOVE: leave++; depth--; log_msg( "WM_EXITSIZEMOVE", hwnd ); break;
    case WM_MOVING: log_msg( "WM_MOVING", hwnd ); break;
    case WM_SIZING: log_msg( "WM_SIZING", hwnd ); break;
    case WM_SIZE: log_msg( "WM_SIZE", hwnd ); break;
    case WM_WINDOWPOSCHANGED:
        if (!(((WINDOWPOS *)lp)->flags & SWP_NOMOVE) || !(((WINDOWPOS *)lp)->flags & SWP_NOSIZE))
            log_msg( "WM_WINDOWPOSCHANGED", hwnd );
        break;
    case WM_CAPTURECHANGED: log_msg( "WM_CAPTURECHANGED", hwnd ); break;
    case WM_TIMER:
        if (wp == 0x124)
        {
            log_msg( "timeout", hwnd );
            PostMessageA( hwnd, WM_COMMAND, IDCANCEL, 0 );
            return 0;
        }
        break;
    }
    return CallWindowProcA( old_proc, hwnd, msg, wp, lp );
}

static UINT_PTR CALLBACK hook_proc( HWND hwnd, UINT msg, WPARAM wp, LPARAM lp )
{
    if (msg == WM_INITDIALOG)
    {
        HWND dlg = GetParent( hwnd );
        SetWindowTextA( dlg, "filedlg_sizemove" );
        old_proc = (WNDPROC)SetWindowLongPtrA( dlg, GWLP_WNDPROC, (LONG_PTR)dlg_proc );
        SetTimer( dlg, 0x124, secs * 1000, NULL );
        printf( "hwnd %p\n", dlg );
        log_msg( "init", dlg );
    }
    return 0;
}

int main( int argc, char **argv )
{
    char file[MAX_PATH] = "";
    OPENFILENAMEA ofn = { sizeof(ofn) };
    HWND owner;

    if (argc > 1) secs = atoi( argv[1] );
    start = GetTickCount();
    owner = CreateWindowA( "static", "filedlg_sizemove owner", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                           100, 100, 300, 200, NULL, NULL, NULL, NULL );
    ofn.hwndOwner = owner;
    ofn.lpstrFile = file;
    ofn.nMaxFile = sizeof(file);
    ofn.lpstrFilter = "All files\0*.*\0";
    ofn.lpstrInitialDir = "C:\\windows";
    ofn.lpfnHook = hook_proc;
    ofn.Flags = OFN_EXPLORER | OFN_ENABLEHOOK | OFN_ENABLESIZING | OFN_HIDEREADONLY;
    GetOpenFileNameA( &ofn );
    printf( "summary enter %d exit %d size %ldx%ld\n", enter, leave, last.cx, last.cy );
    return enter != leave || depth;
}
