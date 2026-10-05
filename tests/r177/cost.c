/* 177: cost of the paths that expect X errors (winex11 X11DRV_expect_error): reading the screen, new window surfaces.
 * Build: x86_64-w64-mingw32-gcc -O2 -o cost.exe cost.c -lgdi32 -luser32
 *   cost.exe SECS   one thread, SECS seconds per line; us per call:
 *     GetPixel            from the screen DC (XGetImage of one pixel)
 *     SetPixel+GetPixel   a request without a reply before each one
 *     N lines + GetPixel  N buffered drawing requests before the round trip (N = 16, 256)
 *     BitBlt 16 / 256     screen -> memory DC
 *     surface             SetWindowPos resize of a visible window by 200 pixels + repaint (new XShm window surface)
 */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

static double freq;
static double now_us(void)
{
    LARGE_INTEGER t;
    QueryPerformanceCounter( &t );
    return t.QuadPart * 1e6 / freq;
}

static LRESULT WINAPI wndproc( HWND hwnd, UINT msg, WPARAM wp, LPARAM lp )
{
    if (msg == WM_PAINT)
    {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint( hwnd, &ps );
        FillRect( dc, &ps.rcPaint, GetStockObject( GRAY_BRUSH ) );
        EndPaint( hwnd, &ps );
        return 0;
    }
    return DefWindowProcA( hwnd, msg, wp, lp );
}

#define BENCH(name, code) do { \
    double start = now_us(), end = start + secs * 1e6, t; long n = 0; \
    while ((t = now_us()) < end) { int k; for (k = 0; k < 16; k++) { code; } n += 16; } \
    printf( "%-22s %9.2f us\n", name, (t - start) / n ); fflush( stdout ); } while (0)

int main( int argc, char **argv )
{
    int secs = argc > 1 ? atoi( argv[1] ) : 3, i, big = 0;
    LARGE_INTEGER f;
    HDC screen = GetDC( 0 ), mem = CreateCompatibleDC( screen );
    HBITMAP bmp = CreateCompatibleBitmap( screen, 256, 256 );
    WNDCLASSA wc = { 0 };
    HWND hwnd;
    MSG msg;
    volatile COLORREF c;

    QueryPerformanceFrequency( &f ); freq = f.QuadPart;
    SelectObject( mem, bmp );
    SelectObject( screen, GetStockObject( WHITE_PEN ) );

    BENCH( "GetPixel", c = GetPixel( screen, 10, 600 ) );
    BENCH( "SetPixel+GetPixel", SetPixel( screen, 11, 600, 0x123456 ); c = GetPixel( screen, 10, 600 ) );
    BENCH( "16 lines + GetPixel", for (i = 0; i < 16; i++) { MoveToEx( screen, 20, 600 + i, NULL ); LineTo( screen, 40, 600 + i ); } c = GetPixel( screen, 10, 600 ) );
    BENCH( "256 lines + GetPixel", for (i = 0; i < 256; i++) { MoveToEx( screen, 20, 600 + (i & 15), NULL ); LineTo( screen, 40, 600 + (i & 15) ); } c = GetPixel( screen, 10, 600 ) );
    BENCH( "BitBlt 16", BitBlt( mem, 0, 0, 16, 16, screen, 0, 600, SRCCOPY ) );
    BENCH( "BitBlt 256", BitBlt( mem, 0, 0, 256, 256, screen, 0, 600, SRCCOPY ) );

    wc.lpfnWndProc = wndproc; wc.hInstance = GetModuleHandleA( 0 ); wc.lpszClassName = "cost177";
    wc.hCursor = LoadCursorA( 0, (char *)IDC_ARROW );
    RegisterClassA( &wc );
    hwnd = CreateWindowA( "cost177", "cost", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 300, 100, 300, 300, 0, 0, 0, 0 );
    while (PeekMessageA( &msg, 0, 0, 0, PM_REMOVE )) DispatchMessageA( &msg );
    BENCH( "surface", big = !big; SetWindowPos( hwnd, 0, 0, 0, big ? 500 : 300, big ? 500 : 300, SWP_NOMOVE | SWP_NOZORDER );
           UpdateWindow( hwnd ); while (PeekMessageA( &msg, 0, 0, 0, PM_REMOVE )) DispatchMessageA( &msg ) );
    (void)c;
    return 0;
}
