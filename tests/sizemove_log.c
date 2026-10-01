/* sizemove_log.c: captioned window that logs the move/size notifications it gets
 * (WM_ENTERSIZEMOVE/WM_EXITSIZEMOVE, WM_MOVING/WM_SIZING, WM_WINDOWPOSCHANGED,
 * WM_CAPTURECHANGED, WM_NCLBUTTONDOWN, WM_SYSCOMMAND), one line each with a millisecond timestamp (077).
 * The top 40 px of the client area are a caption (HTCAPTION).
 * Usage: sizemove_log.exe [SECS] [X Y W H]; exits after SECS (default 60), prints
 * "summary enter N exit N" and returns 1 if the counts differ or a size-move is still open.
 * Build: x86_64-w64-mingw32-gcc -O2 -o sizemove_log.exe sizemove_log.c */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

static DWORD start;
static int enter, leave, depth;

static void log_msg( const char *what, HWND hwnd )
{
    RECT rc;
    GetWindowRect( hwnd, &rc );
    printf( "%6lu %-20s depth %d rect %ld,%ld-%ld,%ld\n", GetTickCount() - start, what, depth,
            rc.left, rc.top, rc.right, rc.bottom );
    fflush( stdout );
}

static LRESULT CALLBACK wndproc( HWND hwnd, UINT msg, WPARAM wp, LPARAM lp )
{
    switch (msg)
    {
    case WM_ENTERSIZEMOVE: enter++; depth++; log_msg( "WM_ENTERSIZEMOVE", hwnd ); break;
    case WM_EXITSIZEMOVE: leave++; depth--; log_msg( "WM_EXITSIZEMOVE", hwnd ); break;
    case WM_MOVING: log_msg( "WM_MOVING", hwnd ); break;
    case WM_SIZING: log_msg( "WM_SIZING", hwnd ); break;
    case WM_WINDOWPOSCHANGED:
        if (!(((WINDOWPOS *)lp)->flags & SWP_NOMOVE) || !(((WINDOWPOS *)lp)->flags & SWP_NOSIZE))
            log_msg( "WM_WINDOWPOSCHANGED", hwnd );
        break;
    case WM_CAPTURECHANGED: log_msg( "WM_CAPTURECHANGED", hwnd ); break;
    case WM_NCLBUTTONDOWN: log_msg( "WM_NCLBUTTONDOWN", hwnd ); break;
    case WM_SYSCOMMAND: log_msg( "WM_SYSCOMMAND", hwnd ); break;
    case WM_DESTROY: PostQuitMessage( 0 ); break;
    case WM_NCHITTEST: /* app-drawn caption strip like Inventor's: top 40 px of the client */
    {
        POINT pt = { (short)LOWORD(lp), (short)HIWORD(lp) };
        LRESULT ret = DefWindowProcA( hwnd, msg, wp, lp );
        ScreenToClient( hwnd, &pt );
        if (ret == HTCLIENT && pt.y < 40) ret = HTCAPTION;
        return ret;
    }
    }
    return DefWindowProcA( hwnd, msg, wp, lp );
}

int main( int argc, char **argv )
{
    int secs = argc > 1 ? atoi( argv[1] ) : 60;
    int x = argc > 5 ? atoi( argv[2] ) : 200, y = argc > 5 ? atoi( argv[3] ) : 200;
    int w = argc > 5 ? atoi( argv[4] ) : 400, h = argc > 5 ? atoi( argv[5] ) : 300;
    WNDCLASSA cls = { 0 };
    HWND hwnd;
    MSG msg;

    start = GetTickCount();
    cls.lpfnWndProc = wndproc;
    cls.hInstance = GetModuleHandleA( NULL );
    cls.hCursor = LoadCursorA( NULL, (LPCSTR)IDC_ARROW );
    cls.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    cls.lpszClassName = "sizemove_log";
    RegisterClassA( &cls );
    hwnd = CreateWindowA( "sizemove_log", "sizemove_log", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                          x, y, w, h, NULL, NULL, cls.hInstance, NULL );
    SetTimer( hwnd, 1, secs * 1000, NULL );
    printf( "hwnd %p\n", hwnd );
    fflush( stdout );
    while (GetMessageA( &msg, NULL, 0, 0 ))
    {
        if (msg.message == WM_TIMER && msg.hwnd == hwnd) DestroyWindow( hwnd );
        TranslateMessage( &msg );
        DispatchMessageA( &msg );
    }
    printf( "summary enter %d exit %d\n", enter, leave );
    return enter != leave || depth;
}
