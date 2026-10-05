/* vstate.c: window state around X window recreation (visual change) from the owner and from another thread (173 review).
 * A top-level window is maximized / minimized / restored / made fullscreen; in each state its X window is recreated
 * (WS_EX_LAYERED + UpdateLayeredWindow: ARGB visual; SetLayeredWindowAttributes: default visual), by the owner
 * thread ("own") or by a second thread ("other"). After each step the Win32 state is printed; watch the X side with
 * vstate.sh (xprop / xwininfo of the window titled "vstate").
 * Build: x86_64-w64-mingw32-gcc -O1 -o vstate.exe vstate.c -lgdi32 -luser32 */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

static HWND W;
static HANDLE req, done;
static int action; /* 1 ULW (argb), 2 SLWA (default visual) */

static void do_action( int a )
{
    if (a == 1)
    {
        BITMAPINFO bi = {{sizeof(BITMAPINFOHEADER), 64, -64, 1, 32, BI_RGB}};
        BLENDFUNCTION bf = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
        POINT pt = {0, 0};
        SIZE size = {64, 64};
        DWORD *bits;
        HDC mem = CreateCompatibleDC( 0 );
        HBITMAP dib = CreateDIBSection( 0, &bi, DIB_RGB_COLORS, (void **)&bits, 0, 0 );
        int i;
        for (i = 0; i < 64 * 64; i++) bits[i] = 0xff4080c0;
        SelectObject( mem, dib );
        if (!UpdateLayeredWindow( W, NULL, NULL, &size, mem, &pt, 0, &bf, ULW_ALPHA )) printf( "  ULW failed %lu\n", GetLastError() );
        DeleteDC( mem ); DeleteObject( dib );
    }
    else if (!SetLayeredWindowAttributes( W, 0, 255, LWA_ALPHA )) printf( "  SLWA failed %lu\n", GetLastError() );
}

static DWORD WINAPI other_proc( void *arg )
{
    for (;;)
    {
        WaitForSingleObject( req, INFINITE );
        do_action( action );
        SetEvent( done );
    }
}

static void pump( int ms )
{
    DWORD end = GetTickCount() + ms;
    MSG msg;
    while (GetTickCount() < end)
    {
        while (PeekMessageA( &msg, 0, 0, 0, PM_REMOVE )) DispatchMessageA( &msg );
        MsgWaitForMultipleObjects( 0, NULL, FALSE, 20, QS_ALLINPUT );
    }
}

static void report( const char *step )
{
    RECT rc;
    GetWindowRect( W, &rc );
    printf( "%-34s visible %d iconic %d zoomed %d rect %ld,%ld %ldx%ld style %08lx ex %08lx fg %d\n", step, IsWindowVisible( W ),
            IsIconic( W ), IsZoomed( W ), rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top,
            GetWindowLongA( W, GWL_STYLE ), GetWindowLongA( W, GWL_EXSTYLE ), GetForegroundWindow() == W );
    fflush( stdout );
}

static void recreate( const char *state, int other )
{
    char buf[64];
    int a;
    /* drop and re-add WS_EX_LAYERED so that both calls are valid again */
    for (a = 1; a <= 2; a++)
    {
        if (a == 1)
        {
            SetWindowLongA( W, GWL_EXSTYLE, GetWindowLongA( W, GWL_EXSTYLE ) & ~WS_EX_LAYERED );
            SetWindowLongA( W, GWL_EXSTYLE, GetWindowLongA( W, GWL_EXSTYLE ) | WS_EX_LAYERED );
        }
        if (other)
        {
            action = a;
            SetEvent( req );
            /* keep handling events while the other thread replaces the X window */
            while (MsgWaitForMultipleObjects( 1, &done, FALSE, 5000, QS_ALLINPUT ) == 1) { MSG msg; while (PeekMessageA( &msg, 0, 0, 0, PM_REMOVE )) DispatchMessageA( &msg ); }
        }
        else do_action( a );
        pump( 700 );
        sprintf( buf, "%s %s %s", state, other ? "other" : "own", a == 1 ? "ULW" : "SLWA" );
        report( buf );
    }
}

int main( int argc, char **argv )
{
    int other;
    setvbuf( stdout, NULL, _IONBF, 0 );
    req = CreateEventA( 0, FALSE, FALSE, 0 ); done = CreateEventA( 0, FALSE, FALSE, 0 );
    CreateThread( 0, 0, other_proc, 0, 0, 0 );
    W = CreateWindowExA( WS_EX_LAYERED, "static", "vstate", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 200, 150, 400, 300, 0, 0, 0, 0 );
    SetLayeredWindowAttributes( W, 0, 255, LWA_ALPHA );
    pump( 1000 ); report( "created" );
    if (argc > 2) /* vstate loop N [own]: N recreations at 260,190, count the ones after which the window is elsewhere */
    {
        int i, n = atoi( argv[2] ), moved = 0, sized = 0;
        other = argc <= 3;
        for (i = 0; i < n; i++)
        {
            RECT rc;
            int a;
            SetWindowPos( W, 0, 260, 190, 420, 310, SWP_NOZORDER ); pump( 150 );
            for (a = 1; a <= 2; a++)
            {
                if (a == 1)
                {
                    SetWindowLongA( W, GWL_EXSTYLE, GetWindowLongA( W, GWL_EXSTYLE ) & ~WS_EX_LAYERED );
                    SetWindowLongA( W, GWL_EXSTYLE, GetWindowLongA( W, GWL_EXSTYLE ) | WS_EX_LAYERED );
                }
                if (other)
                {
                    action = a;
                    SetEvent( req );
                    while (MsgWaitForMultipleObjects( 1, &done, FALSE, 5000, QS_ALLINPUT ) == 1) { MSG msg; while (PeekMessageA( &msg, 0, 0, 0, PM_REMOVE )) DispatchMessageA( &msg ); }
                }
                else do_action( a );
                pump( 250 );
                GetWindowRect( W, &rc );
                if (rc.left != 260 || rc.top != 190) { moved++; printf( "  %d/%d: at %ld,%ld\n", i, a, rc.left, rc.top ); SetWindowPos( W, 0, 260, 190, 0, 0, SWP_NOZORDER | SWP_NOSIZE ); pump( 150 ); }
                if (!IsWindowVisible( W ) || IsIconic( W )) sized++;
            }
        }
        printf( "DONE loop %d %s: moved %d, not visible %d\n", n, other ? "other" : "own", moved, sized );
        return 0;
    }
    for (other = 0; other < 2; other++)
    {
        recreate( "normal", other );
        ShowWindow( W, SW_MAXIMIZE ); pump( 700 ); report( "maximize" );
        recreate( "maximized", other );
        ShowWindow( W, SW_RESTORE ); pump( 700 ); report( "restore" );
        ShowWindow( W, SW_MINIMIZE ); pump( 700 ); report( "minimize" );
        recreate( "minimized", other );
        ShowWindow( W, SW_RESTORE ); pump( 700 ); report( "restore from minimized" );
        ShowWindow( W, SW_HIDE ); pump( 500 ); report( "hide" );
        recreate( "hidden", other );
        ShowWindow( W, SW_SHOW ); pump( 700 ); report( "show" );
        SetWindowPos( W, 0, 260, 190, 420, 310, SWP_NOZORDER ); pump( 700 ); report( "moved" );
        recreate( "moved", other );
    }
    printf( "DONE\n" );
    return 0;
}
