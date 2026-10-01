/* Debug-event order for a WoW64 child (105): prints every event until the loop
 * goes idle (2 s), then kills the child.
 * Usage: dbg_wow64_order.exe [CMDLINE]  (default: syswow64\msinfo32.exe)
 *        dbg_wow64_order.exe -inherit: debugs itself with -noinherit; the debuggee clears its
 *        ProcessDebugFlags, then starts a child (is the child debugged too?)
 *        dbg_wow64_order.exe -ntinherit: the debuggee starts cmd.exe with NtCreateUserProcess,
 *        ProcessFlags 0, then PROCESS_CREATE_FLAGS_NO_DEBUG_INHERIT (2); which child is debugged?
 * Build: x86_64-w64-mingw32-gcc -O2 -o dbg_wow64_order.exe dbg_wow64_order.c -lpsapi -lntdll */
#define _WIN32_WINNT 0x0600
#include <windows.h>
#include <psapi.h>
#include <stdio.h>
#include <winternl.h>

NTSTATUS NTAPI NtSetInformationProcess( HANDLE, PROCESSINFOCLASS, void *, ULONG );
#define ProcessDebugFlags 31

typedef struct { ULONG_PTR Attribute; SIZE_T Size; void *ValuePtr; SIZE_T *ReturnLength; } PS_ATTR;
typedef struct { SIZE_T TotalLength; PS_ATTR Attributes[1]; } PS_ATTR_LIST;
typedef struct { SIZE_T Size; ULONG State; BYTE rest[0x100]; } PS_CREATE;
NTSTATUS NTAPI NtCreateUserProcess( HANDLE *, HANDLE *, ACCESS_MASK, ACCESS_MASK, void *, void *, ULONG, ULONG,
                                    void *, PS_CREATE *, PS_ATTR_LIST * );
NTSTATUS NTAPI RtlCreateProcessParametersEx( void **, UNICODE_STRING *, UNICODE_STRING *, UNICODE_STRING *,
                                             UNICODE_STRING *, void *, UNICODE_STRING *, UNICODE_STRING *,
                                             UNICODE_STRING *, UNICODE_STRING *, ULONG );

static int ntinherit( void )
{
    static WCHAR nt_image[] = L"\\??\\C:\\Windows\\System32\\cmd.exe";
    UNICODE_STRING image, cmd;
    PS_ATTR_LIST attr;
    PS_CREATE info;
    HANDLE process, thread;
    void *params;
    NTSTATUS st;
    ULONG flags, dbgflags;

    RtlInitUnicodeString( &image, nt_image + 4 );
    RtlInitUnicodeString( &cmd, L"cmd.exe /c exit" );
    RtlCreateProcessParametersEx( &params, &image, NULL, NULL, &cmd, NULL, NULL, NULL, NULL, NULL, 1 );
    for (flags = 0; flags <= 2; flags += 2)
    {
        memset( &info, 0, sizeof(info) );
        info.Size = sizeof(SIZE_T) == 8 ? 0x58 : 0x48;
        attr.TotalLength = sizeof(attr);
        attr.Attributes[0].Attribute = 0x20005; /* PS_ATTRIBUTE_IMAGE_NAME */
        attr.Attributes[0].Size = wcslen( nt_image ) * sizeof(WCHAR);
        attr.Attributes[0].ValuePtr = nt_image;
        attr.Attributes[0].ReturnLength = NULL;
        st = NtCreateUserProcess( &process, &thread, PROCESS_ALL_ACCESS, THREAD_ALL_ACCESS, NULL, NULL,
                                  flags, 0, params, &info, &attr );
        dbgflags = 0xdead;
        if (!st) NtQueryInformationProcess( process, ProcessDebugFlags, &dbgflags, sizeof(dbgflags), NULL );
        printf( "child: NtCreateUserProcess flags %lu -> %#lx pid %04lx, its ProcessDebugFlags %lu\n", flags, st,
                st ? 0 : GetProcessId( process ), dbgflags );
        fflush( stdout );
        if (st) continue;
        WaitForSingleObject( process, 5000 );
        CloseHandle( thread );
        CloseHandle( process );
    }
    return 0;
}

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
    if (argc > 1 && !lstrcmpW( argv[1], L"-ntflags" )) return ntinherit();
    if (argc > 1 && (!lstrcmpW( argv[1], L"-inherit" ) || !lstrcmpW( argv[1], L"-ntinherit" )))
    {
        GetModuleFileNameW( NULL, buf, MAX_PATH );
        lstrcatW( buf, argv[1][1] == 'n' ? L" -ntflags" : L" -noinherit" );
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
