/* ClipCursor moving the cursor into the clip rect: is the new position still reported once it
 * is older than 100 ms (when Wine's GetCursorPos asks the X server instead)? (101)
 * No windows in this process, so winex11 doesn't grab the pointer for the clip rect.
 * Then a thread on another desktop calls SetCursorPos(600,400) (Wine: moves the shared X
 * pointer out of the clip rect); GetCursorPos must still be inside the clip rect.
 * Build: x86_64-w64-mingw32-gcc -O2 -o clipcursor_warp.exe clipcursor_warp.c
 * Exit code 0 = every read is (50,50). */
#include <windows.h>
#include <stdio.h>

static DWORD WINAPI other_desktop_thread( void *arg )
{
    BOOL ret;
    if (!SetThreadDesktop( arg )) printf( "SetThreadDesktop failed %lu\n", GetLastError() );
    GetDesktopWindow(); /* Wine: the desktop has no size (clips everything to 0,0) until then */
    SetLastError( 0xdeadbeef );
    ret = SetCursorPos( 600, 400 );
    printf( "other desktop SetCursorPos(600,400) ret %d err %lu\n", ret, GetLastError() );
    return 0;
}

int main(void)
{
    HDESK desktop;
    HANDLE thread;
    RECT clip = {50, 50, 51, 51};
    POINT pos;
    int fail = 0, delay;

    for (delay = 0; delay <= 300; delay += 150)
    {
        SetCursorPos( 49, 51 );
        ClipCursor( &clip );
        Sleep( delay );
        GetCursorPos( &pos );
        printf( "clip, %3d ms later: (%ld,%ld)\n", delay, pos.x, pos.y );
        fail |= pos.x != 50 || pos.y != 50;
        SetCursorPos( 52, 52 );
        Sleep( delay );
        GetCursorPos( &pos );
        printf( "SetCursorPos(52,52) clipped, %3d ms later: (%ld,%ld)\n", delay, pos.x, pos.y );
        fail |= pos.x != 50 || pos.y != 50;
        ClipCursor( NULL );
    }

    SetCursorPos( 150, 150 );
    SetRect( &clip, 100, 100, 200, 200 );
    ClipCursor( &clip );
    desktop = CreateDesktopA( "clipcursor_warp", NULL, NULL, 0, GENERIC_ALL, NULL );
    thread = CreateThread( NULL, 0, other_desktop_thread, desktop, 0, NULL );
    WaitForSingleObject( thread, INFINITE );
    Sleep( 150 );
    GetCursorPos( &pos );
    printf( "clip (100,100)-(200,200), moved from other desktop, 150 ms later: (%ld,%ld)\n", pos.x, pos.y );
    fail |= pos.x < 100 || pos.x >= 200 || pos.y < 100 || pos.y >= 200;
    ClipCursor( NULL );
    CloseDesktop( desktop );
    return fail;
}
