/* tray.c: a notification icon kept for SECS seconds (issue 184: embedded windows / XEmbed after the host parent change).
 * Under a window manager with a system tray (awesome) winex11 docks the icon window into the tray: check with
 * `xwininfo -root -tree` that an X window of this process sits below the tray window, with the icon's size.
 * Build: x86_64-w64-mingw32-gcc -O1 -o tray.exe tray.c -lshell32 -luser32 */
#include <windows.h>
#include <shellapi.h>
#include <stdio.h>
#include <stdlib.h>

int main( int argc, char **argv )
{
    NOTIFYICONDATAA nid = { sizeof(nid) };
    DWORD end = GetTickCount() + (argc > 1 ? atoi( argv[1] ) : 10) * 1000;
    MSG msg;

    nid.hWnd = CreateWindowA( "static", "tray", WS_OVERLAPPEDWINDOW, 100, 100, 200, 100, 0, 0, 0, 0 );
    nid.uID = 1;
    nid.uFlags = NIF_ICON | NIF_TIP;
    nid.hIcon = LoadIconA( 0, (char *)IDI_INFORMATION );
    lstrcpyA( nid.szTip, "tray probe" );
    printf( "NIM_ADD %d\n", Shell_NotifyIconA( NIM_ADD, &nid ) );
    fflush( stdout );
    while ((int)(end - GetTickCount()) > 0)
    {
        while (PeekMessageA( &msg, 0, 0, 0, PM_REMOVE )) DispatchMessageA( &msg );
        MsgWaitForMultipleObjects( 0, NULL, FALSE, 50, QS_ALLINPUT );
    }
    Shell_NotifyIconA( NIM_DELETE, &nid );
    printf( "DONE tray\n" );
    return 0;
}
