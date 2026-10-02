/* Mojo-style IPC ping-pong (issue 107): two processes exchange small messages over an
 * overlapped byte-mode named pipe, each side with its own I/O completion port, the way
 * Chromium's mojo/core/channel_win.cc + base MessagePumpForIO do it:
 *   - pipe: CreateNamedPipe(PIPE_ACCESS_DUPLEX|FILE_FLAG_OVERLAPPED, byte mode, 4096/4096),
 *     client end via CreateFile(FILE_FLAG_OVERLAPPED), inherited by the child;
 *   - CreateIoCompletionPort(pipe, port, key, 1); OVERLAPPEDs with hEvent = NULL;
 *   - one ReadFile always pending (4096-byte buffer), WriteFile per message, both
 *     overlapped, completions through the port (no FILE_SKIP_* flags);
 *   - pump loop: DoWork; GetQueuedCompletionStatus(0); if nothing, GQCS(INFINITE).
 * The parent sends a message, the child echoes it on its read completion.
 *
 * mojo_pingpong.exe [N] [SIZE] [skip]   (skip: FILE_SKIP_COMPLETION_PORT_ON_SUCCESS |
 *                                        FILE_SKIP_SET_EVENT_ON_HANDLE on both ends)
 * Prints round trips/s, us per round trip, CPU us per message of each process.
 * Build: x86_64-w64-mingw32-gcc -O2 -o mojo_pingpong.exe mojo_pingpong.c
 */
#define _WIN32_WINNT 0x0601
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

static HANDLE pipe, port;
static OVERLAPPED rd_ov, wr_ov;
static char rd_buf[4096], wr_buf[4096];
static int msg_size = 64, skip;
static LONG received, sent;
static int write_pending;

static void fail( const char *what )
{
    printf( "%s failed: %lu\n", what, GetLastError() );
    ExitProcess( 1 );
}

static void read_more(void)
{
    if (!ReadFile( pipe, rd_buf, sizeof(rd_buf), NULL, &rd_ov ) && GetLastError() != ERROR_IO_PENDING
        && GetLastError() != ERROR_BROKEN_PIPE)  /* the peer is done */
        fail( "ReadFile" );
}

static int queued;  /* messages waiting for the pending write, like ChannelWin's outgoing queue */

static void write_msg(void)
{
    if (write_pending)
    {
        queued++;
        return;
    }
    write_pending = 1;
    if (WriteFile( pipe, wr_buf, msg_size, NULL, &wr_ov ))
    {
        if (skip)  /* no completion packet */
        {
            write_pending = 0;
            if (queued)
            {
                queued--;
                write_msg();
            }
        }
    }
    else if (GetLastError() != ERROR_IO_PENDING) fail( "WriteFile" );
}

/* one GQCS like MessagePumpForIO::WaitForIOCompletion; returns FALSE if nothing came */
static BOOL wait_io( DWORD timeout, int is_parent, int n )
{
    DWORD bytes = 0;
    ULONG_PTR key = 0;
    OVERLAPPED *ov = NULL;

    if (!GetQueuedCompletionStatus( port, &bytes, &key, &ov, timeout ))
    {
        if (!ov) return FALSE;
        fail( "I/O" );
    }
    if (ov == &wr_ov)
    {
        if (bytes != msg_size) fail( "short write" );
        write_pending = 0;
        sent++;
        if (queued)
        {
            queued--;
            write_msg();
        }
        return TRUE;
    }
    if (ov != &rd_ov) fail( "bad overlapped" );
    if (!bytes) fail( "eof" );
    /* byte mode: a message may arrive in pieces or merged; count whole messages */
    {
        static DWORD partial;
        partial += bytes;
        while (partial >= (DWORD)msg_size)
        {
            partial -= msg_size;
            received++;
            if (!is_parent || received < n) write_msg();
        }
    }
    read_more();
    if (skip)
    {
        /* with SKIP_COMPLETION_PORT_ON_SUCCESS an immediate read completes without a packet */
        while (HasOverlappedIoCompleted( &rd_ov ) && rd_ov.Internal == 0)
        {
            DWORD got = rd_ov.InternalHigh;
            static DWORD partial2;
            partial2 += got;
            while (partial2 >= (DWORD)msg_size) { partial2 -= msg_size; received++; if (!is_parent || received < n) write_msg(); }
            read_more();
        }
    }
    return TRUE;
}

static void setup_port(void)
{
    if (!(port = CreateIoCompletionPort( INVALID_HANDLE_VALUE, NULL, 0, 1 ))) fail( "CreateIoCompletionPort" );
    if (!CreateIoCompletionPort( pipe, port, 1, 1 )) fail( "CreateIoCompletionPort(pipe)" );
    if (skip && !SetFileCompletionNotificationModes( pipe, FILE_SKIP_COMPLETION_PORT_ON_SUCCESS | FILE_SKIP_SET_EVENT_ON_HANDLE ))
        fail( "SetFileCompletionNotificationModes" );
}

static double ft_us( FILETIME ft )
{
    return (((ULONGLONG)ft.dwHighDateTime << 32) | ft.dwLowDateTime) / 10.0;
}

static double cpu_us( HANDLE process )
{
    FILETIME c, e, k, u;
    GetProcessTimes( process, &c, &e, &k, &u );
    return ft_us( k ) + ft_us( u );
}

static void pump( int is_parent, int n )
{
    while (received < n)
    {
        /* DoWork: nothing queued here; then poll, then block */
        if (wait_io( 0, is_parent, n )) continue;
        wait_io( INFINITE, is_parent, n );
    }
    while (write_pending) wait_io( INFINITE, is_parent, n );
}

int main( int argc, char **argv )
{
    int n = argc > 1 ? atoi( argv[1] ) : 20000;
    char cmd[MAX_PATH + 64], name[64];
    STARTUPINFOA si = { sizeof(si) };
    PROCESS_INFORMATION pi;
    LARGE_INTEGER freq, t0, t1;
    double c0, c1, child_cpu, secs;
    SECURITY_ATTRIBUTES sa = { sizeof(sa), NULL, TRUE };
    HANDLE client;

    if (argc > 2) msg_size = atoi( argv[2] );
    if (argc > 3) skip = !strcmp( argv[3], "skip" );
    memset( wr_buf, 'x', sizeof(wr_buf) );

    if (argc > 4 && !strcmp( argv[4], "child" ))
    {
        pipe = (HANDLE)(ULONG_PTR)strtoull( argv[5], NULL, 0 );
        setup_port();
        read_more();
        pump( 0, n );
        return 0;
    }

    sprintf( name, "\\\\.\\pipe\\mojo_pingpong.%lu", GetCurrentProcessId() );
    pipe = CreateNamedPipeA( name, PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE,
                             PIPE_TYPE_BYTE | PIPE_READMODE_BYTE, 1, 4096, 4096, 5000, NULL );
    if (pipe == INVALID_HANDLE_VALUE) fail( "CreateNamedPipe" );
    client = CreateFileA( name, (FILE_GENERIC_READ | FILE_GENERIC_WRITE) & ~FILE_APPEND_DATA, 0, &sa,
                          OPEN_EXISTING, SECURITY_SQOS_PRESENT | SECURITY_ANONYMOUS | FILE_FLAG_OVERLAPPED, NULL );
    if (client == INVALID_HANDLE_VALUE) fail( "CreateFile" );
    if (ConnectNamedPipe( pipe, NULL ) || GetLastError() != ERROR_PIPE_CONNECTED) fail( "ConnectNamedPipe" );

    sprintf( cmd, "\"%s\" %d %d %s child %#lx", argv[0], n, msg_size, skip ? "skip" : "noskip", (unsigned long)(ULONG_PTR)client );
    if (!CreateProcessA( NULL, cmd, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi )) fail( "CreateProcess" );
    CloseHandle( client );
    setup_port();
    read_more();

    /* warm up */
    Sleep( 200 );
    QueryPerformanceFrequency( &freq );
    QueryPerformanceCounter( &t0 );
    c0 = cpu_us( GetCurrentProcess() );
    write_msg();
    pump( 1, n );
    QueryPerformanceCounter( &t1 );
    c1 = cpu_us( GetCurrentProcess() );
    WaitForSingleObject( pi.hProcess, INFINITE );
    child_cpu = cpu_us( pi.hProcess );
    secs = (double)(t1.QuadPart - t0.QuadPart) / freq.QuadPart;
    printf( "%d round trips of %d bytes%s: %.3f s, %.0f rt/s, %.1f us/rt, cpu us/rt parent %.1f child %.1f (incl. startup)\n",
            n, msg_size, skip ? " (skip flags)" : "", secs, n / secs, secs * 1e6 / n,
            (c1 - c0) / n, child_cpu / n );
    return 0;
}
