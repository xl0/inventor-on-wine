/* 177: the X errors that winex11 expects must still be caught (XGetImage BadMatch when part of the source is off screen).
 * Build: x86_64-w64-mingw32-gcc -O2 -o expect.exe expect.c -lgdi32 -luser32
 *   expect.exe [THREADS] [SECS]   every thread reads the screen DC partly outside of the screen (GetPixel, BitBlt) and
 *   inside it; prints the results of the first round and "DONE expect ... mismatches 0". An uncaught error ends the
 *   process with "X Error of failed request".
 */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

static volatile LONG stop, rounds, mismatches;
static int cx, cy;

static DWORD WINAPI worker( void *arg )
{
    int first = !arg;
    HDC screen = GetDC( 0 ), mem = CreateCompatibleDC( screen );
    HBITMAP bmp = CreateCompatibleBitmap( screen, 32, 32 );
    SelectObject( mem, bmp );
    while (!stop)
    {
        COLORREF out = GetPixel( screen, -5, -5 ), in, edge;
        BOOL ret;
        SetPixel( screen, 3, 3, RGB( 0x12, 0x34, 0x56 ) );
        in = GetPixel( screen, 3, 3 );
        PatBlt( mem, 0, 0, 32, 32, WHITENESS );
        ret = BitBlt( mem, 0, 0, 32, 32, screen, cx - 16, cy - 16, SRCCOPY );  /* half of it is outside */
        edge = GetPixel( mem, 20, 20 );
        if (first) { printf( "outside %08x, inside %06x, BitBlt over the corner %d, pixel outside %06x\n", (int)out, (int)in, ret, (int)edge ); fflush( stdout ); first = 0; }
        if (out != CLR_INVALID || !ret) InterlockedIncrement( &mismatches );
        InterlockedIncrement( &rounds );
    }
    return 0;
}

int main( int argc, char **argv )
{
    int n = argc > 1 ? atoi( argv[1] ) : 4, secs = argc > 2 ? atoi( argv[2] ) : 5, i;
    HANDLE h[64];
    cx = GetSystemMetrics( SM_CXSCREEN ); cy = GetSystemMetrics( SM_CYSCREEN );
    for (i = 0; i < n; i++) h[i] = CreateThread( NULL, 0, worker, (void *)(INT_PTR)i, 0, NULL );
    Sleep( secs * 1000 );
    stop = 1;
    WaitForMultipleObjects( n, h, TRUE, INFINITE );
    printf( "DONE expect threads %d: %ld rounds, mismatches %ld\n", n, rounds, mismatches );
    return mismatches != 0;
}
