/* Debug-event order for a WoW64 child (105): prints every event until the loop
 * goes idle (2 s), then kills the child.
 * Usage: dbg_wow64_order.exe [CMDLINE]  (default: syswow64\msinfo32.exe)
 *        dbg_wow64_order.exe -inherit: debugs itself with -noinherit; the debuggee clears its
 *        ProcessDebugFlags, then starts a child (is the child debugged too?)
 * Build: x86_64-w64-mingw32-gcc -O2 -o dbg_wow64_order.exe dbg_wow64_order.c -lpsapi -lntdll */
#define _WIN32_WINNT 0x0600
#include <windows.h>
#include <psapi.h>
#include <stdio.h>
#include <winternl.h>

NTSTATUS NTAPI NtSetInformationProcess( HANDLE, PROCESSINFOCLASS, void *, ULONG );
#define ProcessDebugFlags 31

static int noinherit( void )
{
    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi;
    WCHAR cmd[] = L"cmd.exe /c exit";
    ULONG flags = 0xdead, zero = 0, two = 2;
    ULONG64 big = 0;
    NTSTATUS st;

    st = NtQueryInformationProcess( GetCurrentProcess(), ProcessDebugFlags, &flags, sizeof(flags), NULL );
    printf( "child: query %#lx flags %lu\n", st, flags );
    st = NtSetInformationProcess( GetCurrentProcess(), ProcessDebugFlags, &two, sizeof(two) );
    printf( "child: set 2 -> %#lx\n", st );
    st = NtSetInformationProcess( GetCurrentProcess(), ProcessDebugFlags, &big, sizeof(big) );
    printf( "child: set size 8 -> %#lx\n", st );
    st = NtSetInformationProcess( GetCurrentProcess(), ProcessDebugFlags, &zero, sizeof(zero) );
    flags = 0xdead;
    NtQueryInformationProcess( GetCurrentProcess(), ProcessDebugFlags, &flags, sizeof(flags), NULL );
    printf( "child: set 0 -> %#lx, flags now %lu\n", st, flags );
    fflush( stdout );
    if (!CreateProcessW( NULL, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi )) return 1;
    WaitForSingleObject( pi.hProcess, 5000 );
    flags = 1;
    st = NtSetInformationProcess( GetCurrentProcess(), ProcessDebugFlags, &flags, sizeof(flags) );
    flags = 0xdead;
    NtQueryInformationProcess( GetCurrentProcess(), ProcessDebugFlags, &flags, sizeof(flags), NULL );
    printf( "child: set 1 -> %#lx, flags now %lu\n", st, flags );
    fflush( stdout );
    return 0;
}

int wmain( int argc, WCHAR **argv )
{
    WCHAR buf[MAX_PATH], *p;
    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi;
    DEBUG_EVENT ev;
    DWORD t0 = GetTickCount();
    int n = 0;

    if (argc > 1 && !lstrcmpW( argv[1], L"-noinherit" )) return noinherit();
    if (argc > 1 && !lstrcmpW( argv[1], L"-inherit" ))
    {
        GetModuleFileNameW( NULL, buf, MAX_PATH );
        lstrcatW( buf, L" -noinherit" );
    }
    else if (argc > 1) lstrcpyW( buf, argv[1] );
    else
    {
        GetSystemWow64DirectoryW( buf, MAX_PATH );
        lstrcatW( buf, L"\\msinfo32.exe" );
    }
    if (!CreateProcessW( NULL, buf, NULL, NULL, FALSE, DEBUG_PROCESS, NULL, NULL, &si, &pi ))
    {
        printf( "CreateProcess %lu\n", GetLastError() );
        return 1;
    }
    while (WaitForDebugEvent( &ev, 2000 ))
    {
        printf( "%2d %5lu pid %04lx tid %04lx ", ++n, GetTickCount() - t0, ev.dwProcessId, ev.dwThreadId );
        switch (ev.dwDebugEventCode)
        {
        case CREATE_PROCESS_DEBUG_EVENT:
            if (!GetFinalPathNameByHandleW( ev.u.CreateProcessInfo.hFile, buf, MAX_PATH, 0 )) buf[0] = 0;
            printf( "CREATE_PROCESS base %p %ls\n", ev.u.CreateProcessInfo.lpBaseOfImage, buf );
            CloseHandle( ev.u.CreateProcessInfo.hFile );
            break;
        case CREATE_THREAD_DEBUG_EVENT: printf( "CREATE_THREAD\n" ); break;
        case EXIT_THREAD_DEBUG_EVENT: printf( "EXIT_THREAD\n" ); break;
        case EXIT_PROCESS_DEBUG_EVENT: printf( "EXIT_PROCESS\n" ); break;
        case LOAD_DLL_DEBUG_EVENT:
            if (!GetMappedFileNameW( pi.hProcess, ev.u.LoadDll.lpBaseOfDll, buf, MAX_PATH )) buf[0] = 0;
            p = wcsrchr( buf, '\\' );
            printf( "LOAD_DLL %p %ls\n", ev.u.LoadDll.lpBaseOfDll, p ? p + 1 : buf );
            CloseHandle( ev.u.LoadDll.hFile );
            break;
        case UNLOAD_DLL_DEBUG_EVENT: printf( "UNLOAD_DLL %p\n", ev.u.UnloadDll.lpBaseOfDll ); break;
        case EXCEPTION_DEBUG_EVENT:
            printf( "EXCEPTION %08lx at %p first %lu\n", ev.u.Exception.ExceptionRecord.ExceptionCode,
                    ev.u.Exception.ExceptionRecord.ExceptionAddress, ev.u.Exception.dwFirstChance );
            break;
        default: printf( "event %lu\n", ev.dwDebugEventCode ); break;
        }
        if (ev.dwDebugEventCode == EXIT_PROCESS_DEBUG_EVENT && ev.dwProcessId == pi.dwProcessId) break;
        ContinueDebugEvent( ev.dwProcessId, ev.dwThreadId,
                            ev.dwDebugEventCode == EXCEPTION_DEBUG_EVENT &&
                            ev.u.Exception.ExceptionRecord.ExceptionCode != EXCEPTION_BREAKPOINT &&
                            ev.u.Exception.ExceptionRecord.ExceptionCode != 0x4000001f /* WX86 bp */
                            ? DBG_EXCEPTION_NOT_HANDLED : DBG_CONTINUE );
    }
    TerminateProcess( pi.hProcess, 0 );
    return 0;
}
