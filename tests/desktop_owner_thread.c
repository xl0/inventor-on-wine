/* Does the desktop window's owner process (Wine: explorer.exe /desktop) survive one of its
 * threads exiting while this single-threaded process is the desktop's only other user? (101)
 * Wine bug: the exiting thread was still counted in running_threads, so the server armed the
 * desktop close timeout and explorer got WM_CLOSE one second later.
 * Wine only (on Windows csrss owns the desktop window and OpenProcess fails: exit 2).
 * Build: x86_64-w64-mingw32-gcc -O2 -o desktop_owner_thread.exe desktop_owner_thread.c
 * Exit code 0 = owner survived every round, 1 = it died, 2 = setup failed. */
#include <windows.h>
#include <stdio.h>

int main(void)
{
    HWND desktop = GetDesktopWindow();
    DWORD pid = 0, i;
    HANDLE process, thread;

    GetWindowThreadProcessId( desktop, &pid );
    if (!(process = OpenProcess( PROCESS_ALL_ACCESS, FALSE, pid )))
    {
        printf( "OpenProcess(%04lx) failed %lu\n", pid, GetLastError() );
        return 2;
    }
    for (i = 0; i < 3; i++)
    {
        /* kernel32 is at the same address in every process of the same architecture */
        if (!(thread = CreateRemoteThread( process, NULL, 0, (LPTHREAD_START_ROUTINE)Sleep, (void *)50, 0, NULL )))
        {
            printf( "CreateRemoteThread failed %lu\n", GetLastError() );
            return 2;
        }
        WaitForSingleObject( thread, INFINITE );
        CloseHandle( thread );
        Sleep( 2000 );
        if (!WaitForSingleObject( process, 0 ) || !IsWindow( desktop ))
        {
            printf( "round %lu: desktop owner %04lx exited\n", i, pid );
            return 1;
        }
    }
    printf( "desktop owner %04lx alive after 3 thread exits\n", pid );
    return 0;
}
