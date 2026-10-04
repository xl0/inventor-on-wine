/* Outgoing backpressure on the Wayland connection (issue 132, the driver's event loop).
 * burst.exe [N] [LEN]: shows a white window, waits 4 s (stop the compositor now), then queues N (2000) title
 * changes of LEN (3000) characters, far more than the socket buffer, paints the window red, lets the window
 * surface flush, and sleeps 60 s WITHOUT pumping messages: only the driver's event thread can send the rest.
 * After the compositor continues the window must turn red.
 * Build: x86_64-w64-mingw32-gcc -O2 -o burst.exe burst.c -lgdi32 */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

static HBRUSH brush;

static LRESULT CALLBACK wndproc( HWND hwnd, UINT msg, WPARAM wp, LPARAM lp )
{
    if (msg == WM_ERASEBKGND)
    {
        RECT rect;
        GetClientRect( hwnd, &rect );
        FillRect( (HDC)wp, &rect, brush );
        return 1;
    }
    return DefWindowProcA( hwnd, msg, wp, lp );
}

static void pump( DWORD ms )
{
    DWORD end = GetTickCount() + ms;
    MSG msg;
    for (; GetTickCount() < end; Sleep( 10 ))
        while (PeekMessageA( &msg, 0, 0, 0, PM_REMOVE )) DispatchMessageA( &msg );
}

int main( int argc, char **argv )
{
    int i, n = argc > 1 ? atoi( argv[1] ) : 2000, len = argc > 2 ? atoi( argv[2] ) : 3000;
    WNDCLASSA wc = {.lpfnWndProc = wndproc, .lpszClassName = "burst"};
    char *title = malloc( len + 16 );
    DWORD start;
    HWND hwnd;

    setvbuf( stdout, NULL, _IONBF, 0 );
    brush = CreateSolidBrush( RGB( 255, 255, 255 ) );
    RegisterClassA( &wc );
    hwnd = CreateWindowA( "burst", "burst", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 100, 100, 400, 300, 0, 0, 0, 0 );
    pump( 4000 );
    printf( "burst: start\n" );
    start = GetTickCount();
    memset( title, 'x', len + 15 );
    for (i = 0; i < n; i++)
    {
        sprintf( title, "%06d", i ); title[6] = 'x'; title[len] = 0;
        SetWindowTextA( hwnd, title );
    }
    printf( "burst: %d titles of %d bytes queued in %lu ms\n", n, len, GetTickCount() - start );
    brush = CreateSolidBrush( RGB( 255, 0, 0 ) );
    RedrawWindow( hwnd, NULL, 0, RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW );
    pump( 300 );
    printf( "burst: painted, not pumping any more\n" );
    Sleep( 60000 );
    return 0;
}
