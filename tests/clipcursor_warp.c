/* ClipCursor moving the cursor into the clip rect: is the new position still reported once it
 * is older than 100 ms (when Wine's GetCursorPos asks the X server instead)? (101)
 * No windows in this process, so winex11 doesn't grab the pointer for the clip rect.
 * Build: x86_64-w64-mingw32-gcc -O2 -o clipcursor_warp.exe clipcursor_warp.c
 * Exit code 0 = every read is (50,50). */
#include <windows.h>
#include <stdio.h>

int main(void)
{
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
    return fail;
}
