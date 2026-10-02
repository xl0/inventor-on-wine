/* When does a pending overlapped pipe read become visible (issue 107)?
 * A pending ReadFile on an overlapped byte-mode pipe is satisfied by a write from another
 * process; nobody dequeues for a while. Before and after GetQueuedCompletionStatus we look
 * at the OVERLAPPED (Internal/InternalHigh), the buffer, the pipe handle's / event's
 * signaled state, and PeekNamedPipe. Variants: port (hEvent NULL), port + hEvent,
 * no port (hEvent NULL), port with the issuing thread blocked in an alertable wait,
 * port while the issuing thread spins in user code.
 * Build: x86_64-w64-mingw32-gcc -O2 -o iocp_deferred.exe iocp_deferred.c
 */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static HANDLE pipe_srv, pipe_cli, port;

static void make_pipe( int n )
{
    char name[64];
    sprintf( name, "\\\\.\\pipe\\iocp_deferred.%lu.%d", GetCurrentProcessId(), n );
    pipe_srv = CreateNamedPipeA( name, PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED, PIPE_TYPE_BYTE | PIPE_READMODE_BYTE,
                                 1, 4096, 4096, 5000, NULL );
    pipe_cli = CreateFileA( name, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL );
    if (pipe_srv == INVALID_HANDLE_VALUE || pipe_cli == INVALID_HANDLE_VALUE) { printf( "pipe failed\n" ); exit( 1 ); }
}

static DWORD WINAPI writer_thread( void *arg )
{
    OVERLAPPED ov = {0};
    DWORD done;
    ov.hEvent = CreateEventA( NULL, TRUE, FALSE, NULL );
    Sleep( 50 );
    if (!WriteFile( pipe_cli, "hello", 5, NULL, &ov ) && GetLastError() != ERROR_IO_PENDING) printf( "write failed\n" );
    GetOverlappedResult( pipe_cli, &ov, &done, TRUE );
    CloseHandle( ov.hEvent );
    return 0;
}

static volatile LONG spin_stop;
static OVERLAPPED *spin_ov;
static char *spin_buf;
static LONG spin_seen_status, spin_seen_data;

static void show( const char *when, OVERLAPPED *ov, const char *buf, HANDLE event )
{
    DWORD avail = 0;
    PeekNamedPipe( pipe_srv, NULL, 0, NULL, &avail, NULL );
    printf( "  %-14s Internal=%#lx InternalHigh=%lu buf=\"%.5s\" pipe_signaled=%d event_signaled=%d peek_avail=%lu\n",
            when, (unsigned long)ov->Internal, (unsigned long)ov->InternalHigh, buf,
            WaitForSingleObject( pipe_srv, 0 ) == WAIT_OBJECT_0,
            event ? WaitForSingleObject( event, 0 ) == WAIT_OBJECT_0 : -1, avail );
}

/* mode: 0 port, 1 port+event, 2 no port, 3 port + issuer in alertable SleepEx, 4 port + issuer spinning,
 * 5 port + issuer exits; other modes: issuer sleeps (non-alertable) until the end */
static DWORD WINAPI issuer( void *arg )
{
    int mode = (int)(INT_PTR)arg;
    static OVERLAPPED ov;
    static char buf[16];
    HANDLE event = mode == 1 ? CreateEventA( NULL, TRUE, FALSE, NULL ) : NULL;

    memset( &ov, 0, sizeof(ov) );
    memset( buf, '.', sizeof(buf) );
    ov.hEvent = event;
    if (ReadFile( pipe_srv, buf, sizeof(buf), NULL, &ov ) || GetLastError() != ERROR_IO_PENDING)
        printf( "read didn't pend: %lu\n", GetLastError() );
    spin_ov = &ov;
    spin_buf = buf;
    if (mode == 3) SleepEx( 300, TRUE );
    else if (mode == 5) return 0;
    else if (mode != 4) while (!spin_stop) Sleep( 1 );
    else
    {
        while (!spin_stop)
        {
            if (!spin_seen_status && ov.Internal != STATUS_PENDING) spin_seen_status = 1;
            if (!spin_seen_data && buf[0] == 'h') spin_seen_data = 1;
        }
    }
    return (DWORD)(ULONG_PTR)event;
}

static void run( int mode )
{
    static const char *names[] = { "port, hEvent NULL", "port + hEvent", "no port, hEvent NULL",
                                   "port, issuer thread in alertable SleepEx", "port, issuer thread spinning",
                                   "port, issuer thread exited before the write" };
    HANDLE thread, wthread;
    DWORD bytes, tid;
    ULONG_PTR key;
    OVERLAPPED *res;
    BOOL ret;

    printf( "%s:\n", names[mode] );
    make_pipe( mode );
    if (mode != 2)
    {
        port = CreateIoCompletionPort( INVALID_HANDLE_VALUE, NULL, 0, 1 );
        CreateIoCompletionPort( pipe_srv, port, 7, 1 );
    }
    spin_stop = 0;
    spin_seen_status = spin_seen_data = 0;
    /* the read is issued by another thread */
    thread = CreateThread( NULL, 0, issuer, (void *)(INT_PTR)mode, 0, &tid );
    Sleep( 20 );
    wthread = CreateThread( NULL, 0, writer_thread, NULL, 0, NULL );
    WaitForSingleObject( wthread, INFINITE );
    Sleep( 100 );
    show( "before dequeue", spin_ov, spin_buf, spin_ov->hEvent );
    if (mode == 4)
    {
        printf( "  spinning issuer saw status %ld data %ld\n", spin_seen_status, spin_seen_data );
    }
    if (mode == 0)
    {
        DWORD got = 0xdead, t = GetTickCount();
        ret = GetOverlappedResultEx( pipe_srv, spin_ov, &got, 300, FALSE );
        printf( "  GetOverlappedResultEx(300) ret %d err %lu got %lu after %lu ms\n", ret, ret ? 0 : GetLastError(),
                got, GetTickCount() - t );
        show( "after GORE", spin_ov, spin_buf, NULL );
    }
    if (mode != 2)
    {
        ret = GetQueuedCompletionStatus( port, &bytes, &key, &res, 0 );
        printf( "  GQCS(0) ret %d bytes %lu key %lu ov ok %d\n", ret, bytes, (unsigned long)key, res == spin_ov );
        show( "after dequeue", spin_ov, spin_buf, spin_ov->hEvent );
    }
    if (mode == 0 || mode == 2)
    {
        DWORD got = 0xdead, t = GetTickCount();
        ret = GetOverlappedResultEx( pipe_srv, spin_ov, &got, 300, FALSE );
        printf( "  GetOverlappedResultEx(300) after completion: ret %d err %lu got %lu after %lu ms\n", ret, ret ? 0 : GetLastError(),
                got, GetTickCount() - t );
    }
    spin_stop = 1;
    WaitForSingleObject( thread, INFINITE );
    CloseHandle( thread );
    CloseHandle( wthread );
    CloseHandle( pipe_srv );
    CloseHandle( pipe_cli );
    if (mode != 2) CloseHandle( port );
}

/* overlapped I/O that completes at once on a port-bound pipe: what does the issuer see? */
static void run_immediate( int with_event )
{
    OVERLAPPED wov = {0}, rov = {0}, *res;
    char buf[16];
    DWORD written = 0xdead, readn = 0xdead, bytes;
    ULONG_PTR key;
    BOOL ret;

    printf( "immediate completions, port%s:\n", with_event ? " + hEvent" : ", hEvent NULL" );
    make_pipe( 10 + with_event );
    port = CreateIoCompletionPort( INVALID_HANDLE_VALUE, NULL, 0, 1 );
    CreateIoCompletionPort( pipe_srv, port, 7, 1 );
    if (with_event)
    {
        wov.hEvent = CreateEventA( NULL, TRUE, FALSE, NULL );
        rov.hEvent = CreateEventA( NULL, TRUE, FALSE, NULL );
    }
    wov.Internal = rov.Internal = 0x1234;
    wov.InternalHigh = rov.InternalHigh = 0x5678;
    ret = WriteFile( pipe_srv, "abc", 3, &written, &wov );
    printf( "  WriteFile ret %d err %lu written %lu Internal=%#lx InternalHigh=%lu\n", ret, ret ? 0 : GetLastError(),
            written, (unsigned long)wov.Internal, (unsigned long)wov.InternalHigh );
    WriteFile( pipe_cli, "xyz", 3, NULL, &(OVERLAPPED){0} );
    Sleep( 50 );
    memset( buf, '.', sizeof(buf) );
    ret = ReadFile( pipe_srv, buf, sizeof(buf), &readn, &rov );
    printf( "  ReadFile ret %d err %lu read %lu Internal=%#lx InternalHigh=%lu buf=\"%.3s\"\n", ret, ret ? 0 : GetLastError(),
            readn, (unsigned long)rov.Internal, (unsigned long)rov.InternalHigh, buf );
    while (GetQueuedCompletionStatus( port, &bytes, &key, &res, 0 ))
        printf( "  dequeued %s bytes %lu\n", res == &wov ? "write" : res == &rov ? "read" : "?", bytes );
    printf( "  after: write Internal=%#lx InternalHigh=%lu, read Internal=%#lx InternalHigh=%lu\n",
            (unsigned long)wov.Internal, (unsigned long)wov.InternalHigh, (unsigned long)rov.Internal, (unsigned long)rov.InternalHigh );
    CloseHandle( pipe_srv );
    CloseHandle( pipe_cli );
    CloseHandle( port );
}

int main( int argc, char **argv )
{
    int i;
    for (i = 0; i < 6; i++) run( i );
    run_immediate( 0 );
    run_immediate( 1 );
    return 0;
}
