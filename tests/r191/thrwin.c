/* thrwin.c: N threads create their first window at the same moment (190: winex11 opens an X input method per
 * thread, and libX11's XOpenIM is not thread safe: "double free or corruption" abort / hang at start).
 *   thrwin.exe [THREADS] [ROUNDS] [STAGGER_MS] [nomain]     prints "DONE thrwin: N windows"; one process = one real
 * attempt (Xlib's list of input methods only grows the first time), so run it in a loop; XCOMPOSEFILE=/dev/null in
 * the environment makes XOpenIM short and the overlap likely (tests/r191/ximopen.c is the same in plain Xlib).
 * STAGGER_MS: thread i creates its window i * STAGGER_MS later, so that threads exit while others start.
 * nomain: the main thread has no window (no X connection) of its own. Then the last thread display can be closed
 * while another thread opens its own: libXext frees its global XGE record at that moment (193: heap corruption or
 * a hang on Xlib's global lock, with or without 190's fix).
 * Build: x86_64-w64-mingw32-gcc -O1 -o thrwin.exe thrwin.c -luser32 */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static HANDLE go;
static LONG windows;
static int stagger;

static DWORD WINAPI thread( void *arg )
{
    HWND hwnd;
    MSG msg;

    WaitForSingleObject( go, INFINITE );
    if (stagger) Sleep( stagger * (int)(INT_PTR)arg );
    hwnd = CreateWindowExA( 0, "static", "thrwin", WS_OVERLAPPEDWINDOW, 10, 10, 100, 100, 0, 0, 0, 0 );
    if (hwnd) InterlockedIncrement( &windows );
    while (PeekMessageA( &msg, 0, 0, 0, PM_REMOVE )) DispatchMessageA( &msg );
    DestroyWindow( hwnd );
    return 0;
}

int main( int argc, char **argv )
{
    int i, r, n = argc > 1 ? atoi( argv[1] ) : 16, rounds = argc > 2 ? atoi( argv[2] ) : 1;
    HANDLE threads[64];

    if (argc > 3) stagger = atoi( argv[3] );
    if (n > 64) n = 64;
    if (argc <= 4 || strcmp( argv[4], "nomain" ))
        CreateWindowExA( 0, "static", "thrwin main", WS_OVERLAPPEDWINDOW, 10, 10, 100, 100, 0, 0, 0, 0 );
    for (r = 0; r < rounds; r++)
    {
        go = CreateEventA( NULL, TRUE, FALSE, NULL );
        for (i = 0; i < n; i++) threads[i] = CreateThread( NULL, 0, thread, (void *)(INT_PTR)i, 0, NULL );
        Sleep( 50 );
        SetEvent( go );
        WaitForMultipleObjects( n, threads, TRUE, INFINITE );
        for (i = 0; i < n; i++) CloseHandle( threads[i] );
        CloseHandle( go );
    }
    printf( "DONE thrwin: %ld windows\n", windows );
    fflush( stdout );
    return 0;
}
