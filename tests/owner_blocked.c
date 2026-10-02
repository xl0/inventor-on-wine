/* owner_blocked.c: SetWindowPos on a popup owned by a window of another process whose thread doesn't
 * process messages (085: winex11 made the owner managed with a synchronous SetWindowPos, blocking until that
 * thread pumped: AdskLicensingAgent's trial popup owned by Inventor's busy main window).
 * Prints ms per call; Windows: ~0. Build: x86_64-w64-mingw32-gcc -O2 -o owner_blocked.exe owner_blocked.c */
#include <windows.h>
#include <stdio.h>

int main( int argc, char **argv )
{
    HANDLE ready = CreateEventA( NULL, FALSE, FALSE, "owner_blocked_ready" );
    HANDLE release = CreateEventA( NULL, FALSE, FALSE, "owner_blocked_release" );
    PROCESS_INFORMATION pi;
    STARTUPINFOA si = { sizeof(si) };
    char cmd[MAX_PATH + 16];
    DWORD start;
    HWND owner, hwnd;
    MSG msg;

    if (argc > 1)  /* child: owner window, then don't process messages for a while */
    {
        owner = CreateWindowA( "static", "owner_blocked owner", WS_POPUP, 0, 0, 50, 50, 0, 0, 0, NULL );
        SetEvent( ready );
        WaitForSingleObject( release, 4000 );
        while (PeekMessageA( &msg, 0, 0, 0, PM_REMOVE )) DispatchMessageA( &msg );
        DestroyWindow( owner );
        return 0;
    }
    sprintf( cmd, "\"%s\" child", argv[0] );
    CreateProcessA( NULL, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi );
    WaitForSingleObject( ready, INFINITE );
    owner = FindWindowA( "static", "owner_blocked owner" );

    /* like AdskLicensingAgent: an active popup gets an owner in a busy process, then is resized and moved */
    hwnd = CreateWindowExA( 0, "static", "owned", WS_POPUP, 0, 0, 100, 100, 0, 0, 0, NULL );
    start = GetTickCount();
    ShowWindow( hwnd, SW_SHOW );
    printf( "show: %lu ms (active %d)\n", GetTickCount() - start, GetActiveWindow() == hwnd );
    SetWindowLongPtrA( hwnd, GWLP_HWNDPARENT, (LONG_PTR)owner );
    start = GetTickCount();
    SetWindowPos( hwnd, 0, 0, 0, 120, 120, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED );
    printf( "resize: %lu ms\n", GetTickCount() - start );
    start = GetTickCount();
    SetWindowPos( hwnd, 0, 20, 20, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE );
    printf( "move: %lu ms\n", GetTickCount() - start );
    DestroyWindow( hwnd );
    SetEvent( release );
    WaitForSingleObject( pi.hProcess, INFINITE );
    return 0;
}
