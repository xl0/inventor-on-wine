/* xicrace.c: keyboard focus / input context of a top-level window whose X window another thread recreates (191).
 *
 * A layered window's X window is destroyed and created again whenever its visual changes: UpdateLayeredWindow
 * (ARGB visual) <-> SetLayeredWindowAttributes (default visual), in the thread that makes the call. winex11 keeps
 * an X input context (XIC) per top-level X window once it had the focus or a key event.
 *
 *   xicrace.exe race SECS [gap=MS] [focus] [move]
 *       the main thread owns a focused window with an edit control and only pumps messages; one thread recreates
 *       its X window every MS milliseconds (default 5: time for the window manager to focus the new window, so
 *       that it has a focused XIC again, and for the owner to go idle: it is then woken by the events of the
 *       next destruction, FocusOut among them, and filters them while the other thread is still destroying).
 *       focus: a thread keeps activating the window; move: a thread moves the pointer over it. Prints
 *       "DONE xicrace: N recreations ..."; when the owner hangs nothing is printed and the process stays.
 *   xicrace.exe ui CMDFILE
 *       two top-level windows A and B with an edit control each, and a third one, C, whose edit control belongs
 *       to another thread (its key messages are translated by a thread that does not own the X window). Commands
 *       are read from CMDFILE as lines get appended (drive it with xicrace-ui.sh + xdotool):
 *         other A|B|C   recreate the X window from another thread      own A|B|C   ... from the owner thread
 *         focus A|B|C   SetForegroundWindow + focus the edit           text        print the three edits (U+XXXX)
 *         clear         empty the edits                                quit
 *       every command is answered by a line on stdout ("ok ..." / "text ...").
 * Build: x86_64-w64-mingw32-gcc -O1 -o xicrace.exe xicrace.c -lgdi32 -luser32 -lwinmm */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WM_RECREATE (WM_APP + 1)
#define WM_FOCUSME  (WM_APP + 2)

static HWND win[3], edit[3];
static volatile LONG stop, recreations, activations;
static int gap = 5;
static DWORD main_tid;

/* change the visual of the window: its X window is recreated by the calling thread */
static void recreate( HWND hwnd )
{
    static LONG argb[3];
    int idx = hwnd == win[0] ? 0 : hwnd == win[1] ? 1 : 2;

    /* drop and re-add WS_EX_LAYERED so that both calls are valid again */
    SetWindowLongA( hwnd, GWL_EXSTYLE, GetWindowLongA( hwnd, GWL_EXSTYLE ) & ~WS_EX_LAYERED );
    SetWindowLongA( hwnd, GWL_EXSTYLE, GetWindowLongA( hwnd, GWL_EXSTYLE ) | WS_EX_LAYERED );
    if ((argb[idx] ^= 1))
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
        UpdateLayeredWindow( hwnd, NULL, NULL, &size, mem, &pt, 0, &bf, ULW_ALPHA );
        DeleteDC( mem ); DeleteObject( dib );
    }
    else SetLayeredWindowAttributes( hwnd, 0, 255, LWA_ALPHA );
    InterlockedIncrement( &recreations );
}

static LRESULT CALLBACK wndproc( HWND hwnd, UINT msg, WPARAM wp, LPARAM lp )
{
    switch (msg)
    {
    case WM_SETFOCUS:
        if (GetWindow( hwnd, GW_CHILD )) SetFocus( GetWindow( hwnd, GW_CHILD ) );
        return 0;
    case WM_RECREATE:
        recreate( hwnd );
        return 0;
    case WM_FOCUSME:
        SetForegroundWindow( hwnd );
        if (hwnd == win[2]) SetFocus( edit[2] );
        return 0;
    case WM_ACTIVATE:
        if (wp) InterlockedIncrement( &activations );
        break;
    case WM_DESTROY:
        PostQuitMessage( 0 );
        return 0;
    }
    return DefWindowProcW( hwnd, msg, wp, lp );
}

static HWND create_window( const WCHAR *title, int x, int y, BOOL with_edit )
{
    HWND hwnd = CreateWindowExW( WS_EX_LAYERED, L"xicrace", title, WS_OVERLAPPEDWINDOW, x, y, 300, 120, 0, 0, 0, 0 );
    SetLayeredWindowAttributes( hwnd, 0, 255, LWA_ALPHA );
    if (with_edit) CreateWindowExW( 0, L"edit", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 0, 0, 280, 60, hwnd, 0, 0, 0 );
    ShowWindow( hwnd, SW_SHOW );
    return hwnd;
}

static DWORD WINAPI recreate_thread( void *arg )
{
    while (!stop)
    {
        recreate( win[0] );
        if (gap) Sleep( gap );
    }
    return 0;
}

static DWORD WINAPI focus_thread( void *arg )
{
    while (!stop)
    {
        PostMessageA( win[0], WM_FOCUSME, 0, 0 );
        Sleep( 3 );
    }
    return 0;
}

static DWORD WINAPI move_thread( void *arg )
{
    RECT rc;
    int i = 0;
    while (!stop)
    {
        GetWindowRect( win[0], &rc );
        SetCursorPos( rc.left + 20 + (i % 200), rc.top + 50 + (i % 30) );
        i++;
        if (!(i % 16)) Sleep( 1 );
    }
    return 0;
}

static int race( int secs, BOOL focus, BOOL move )
{
    HANDLE threads[3];
    DWORD end, n = 0;
    MSG msg;

    win[0] = create_window( L"xicrace", 100, 100, TRUE );
    edit[0] = GetWindow( win[0], GW_CHILD );
    SetForegroundWindow( win[0] );
    end = GetTickCount() + 500;
    while (GetTickCount() < end)
    {
        while (PeekMessageA( &msg, 0, 0, 0, PM_REMOVE )) DispatchMessageA( &msg );
        Sleep( 10 );
    }

    timeBeginPeriod( 1 );  /* for the Sleep( gap ) */
    threads[n++] = CreateThread( NULL, 0, recreate_thread, NULL, 0, NULL );
    if (focus) threads[n++] = CreateThread( NULL, 0, focus_thread, NULL, 0, NULL );
    if (move) threads[n++] = CreateThread( NULL, 0, move_thread, NULL, 0, NULL );

    end = GetTickCount() + secs * 1000;
    while (GetTickCount() < end)
    {
        MsgWaitForMultipleObjects( 0, NULL, FALSE, 50, QS_ALLINPUT );
        while (PeekMessageA( &msg, 0, 0, 0, PM_REMOVE ))
        {
            TranslateMessage( &msg );
            DispatchMessageA( &msg );
        }
    }
    stop = 1;
    WaitForMultipleObjects( n, threads, TRUE, 10000 );
    printf( "DONE xicrace: %ld recreations, %ld activations\n", recreations, activations );
    fflush( stdout );
    return 0;
}

/* thread that owns the edit control of window C */
static DWORD WINAPI child_thread( void *arg )
{
    MSG msg;
    edit[2] = CreateWindowExW( 0, L"edit", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 0, 0, 280, 60, win[2], 0, 0, 0 );
    SetEvent( arg );
    while (GetMessageW( &msg, 0, 0, 0 ))
    {
        TranslateMessage( &msg );
        DispatchMessageW( &msg );
    }
    return 0;
}

static DWORD WINAPI command_thread( void *arg )
{
    const char *path = arg;
    char line[256];
    long pos = 0;
    FILE *f;
    int i, j;

    for (;;)
    {
        Sleep( 50 );
        if (!(f = fopen( path, "r" ))) continue;
        fseek( f, pos, SEEK_SET );
        while (fgets( line, sizeof(line), f ))
        {
            char cmd[32] = "", arg1[32] = "";
            if (!strchr( line, '\n' )) break;  /* incomplete line */
            pos = ftell( f );
            sscanf( line, "%31s %31s", cmd, arg1 );
            i = arg1[0] == 'B' ? 1 : arg1[0] == 'C' ? 2 : 0;
            if (!strcmp( cmd, "other" )) { recreate( win[i] ); printf( "ok other %c\n", 'A' + i ); }
            else if (!strcmp( cmd, "own" )) { SendMessageA( win[i], WM_RECREATE, 0, 0 ); printf( "ok own %c\n", 'A' + i ); }
            else if (!strcmp( cmd, "focus" )) { SendMessageA( win[i], WM_FOCUSME, 0, 0 ); printf( "ok focus %c\n", 'A' + i ); }
            else if (!strcmp( cmd, "clear" )) { for (j = 0; j < 3; j++) SendMessageW( edit[j], WM_SETTEXT, 0, (LPARAM)L"" ); printf( "ok clear\n" ); }
            else if (!strcmp( cmd, "text" ))
            {
                printf( "text" );
                for (j = 0; j < 3; j++)
                {
                    WCHAR text[256];
                    int k, len = SendMessageW( edit[j], WM_GETTEXT, 256, (LPARAM)text );
                    printf( " %c[", 'A' + j );
                    for (k = 0; k < len; k++)
                    {
                        if (text[k] >= 0x20 && text[k] < 0x7f) printf( "%c", text[k] );
                        else printf( "<U+%04X>", text[k] );
                    }
                    printf( "]" );
                }
                printf( "\n" );
            }
            else if (!strcmp( cmd, "quit" )) { printf( "ok quit\n" ); fflush( stdout ); PostThreadMessageA( main_tid, WM_QUIT, 0, 0 ); fclose( f ); return 0; }
            fflush( stdout );
        }
        fclose( f );
    }
}

static int ui( char *cmdfile )
{
    HANDLE ready = CreateEventA( NULL, FALSE, FALSE, NULL );
    MSG msg;
    int i;

    for (i = 0; i < 2; i++)
    {
        win[i] = create_window( i ? L"xicrace B" : L"xicrace A", 100 + 350 * i, 100, TRUE );
        edit[i] = GetWindow( win[i], GW_CHILD );
    }
    win[2] = create_window( L"xicrace C", 100, 300, FALSE );
    CreateThread( NULL, 0, child_thread, ready, 0, NULL );
    /* the child's creation sends messages to the parent's thread */
    while (MsgWaitForMultipleObjects( 1, &ready, FALSE, INFINITE, QS_ALLINPUT ) == 1)
        while (PeekMessageW( &msg, 0, 0, 0, PM_REMOVE )) DispatchMessageW( &msg );
    CreateThread( NULL, 0, command_thread, cmdfile, 0, NULL );
    printf( "ready\n" ); fflush( stdout );
    while (GetMessageW( &msg, 0, 0, 0 ))
    {
        TranslateMessage( &msg );
        DispatchMessageW( &msg );
    }
    return 0;
}

int main( int argc, char **argv )
{
    WNDCLASSW cls = {0};
    int i;

    main_tid = GetCurrentThreadId();
    cls.lpfnWndProc = wndproc;
    cls.lpszClassName = L"xicrace";
    cls.hbrBackground = GetStockObject( WHITE_BRUSH );
    cls.hCursor = LoadCursorW( 0, (WCHAR *)IDC_ARROW );
    RegisterClassW( &cls );

    if (argc >= 3 && !strcmp( argv[1], "race" ))
    {
        BOOL focus = FALSE, move = FALSE;
        for (i = 3; i < argc; i++)
        {
            if (!strcmp( argv[i], "focus" )) focus = TRUE;
            if (!strcmp( argv[i], "move" )) move = TRUE;
            if (!strncmp( argv[i], "gap=", 4 )) gap = atoi( argv[i] + 4 );
        }
        return race( atoi( argv[2] ), focus, move );
    }
    if (argc >= 3 && !strcmp( argv[1], "ui" )) return ui( argv[2] );
    printf( "usage: xicrace.exe race SECS [gap=MS] [focus] [move] | ui CMDFILE\n" );
    return 2;
}
