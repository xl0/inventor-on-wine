/* Hostile "source" for winewayland's cross-process client surfaces (issue 132): speaks the driver's
 * protocol from the Win32 side with invalid data. The owner (e.g. `xp.exe host`) must survive every case
 * and keep showing honest sources. Wine only; mirrors struct remote_shared of dlls/winewayland.drv/wayland_remote.c.
 *
 * evil.exe HWND [CASE]   HWND = a top-level window of the victim process; without CASE all cases run.
 * Cases: msg (bogus first-contact messages), nosock (wake handle is a file), huge (2^31 x 2^31 buffers),
 *   small (section smaller than the buffers), nosect (buffer section is an event), odd (sequence lock never
 *   released), index (frame index out of range), rect (absurd rectangles), flood (20000 generation changes
 *   and wakes), ok (valid: a red 300x200 frame at 30,60, then the process exits without cleanup).
 * Build: x86_64-w64-mingw32-gcc -O2 -o evil.exe evil.c -lws2_32 */
#include <winsock2.h>
#include <windows.h>
#include <stdio.h>
#include <string.h>

#define WM_WAYLAND_REMOTE_SURFACE (0x80001000 + 3)

struct remote_shared
{
    LONG seq; UINT hwnd, wake, section, generation, width, height; RECT rect; UINT visible;
    LONG ready, released, attached;
};

static HWND victim;

struct source
{
    HANDLE mapping;
    struct remote_shared *shared;
    SOCKET wake, peer;      /* the victim gets peer, we send on wake */
};

static void wake( struct source *s ) { char c = 0; send( s->wake, &c, 1, 0 ); }

static void source_init( struct source *s, HANDLE wake_handle )
{
    struct sockaddr_in addr = {.sin_family = AF_INET};
    SOCKET listener = socket( AF_INET, SOCK_STREAM, 0 );
    int len = sizeof(addr);

    addr.sin_addr.s_addr = htonl( INADDR_LOOPBACK );
    bind( listener, (struct sockaddr *)&addr, sizeof(addr) );
    listen( listener, 1 );
    getsockname( listener, (struct sockaddr *)&addr, &len );
    s->wake = socket( AF_INET, SOCK_STREAM, 0 );
    connect( s->wake, (struct sockaddr *)&addr, sizeof(addr) );
    s->peer = accept( listener, NULL, NULL );
    closesocket( listener );

    s->mapping = CreateFileMappingA( INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, 4096, NULL );
    s->shared = MapViewOfFile( s->mapping, FILE_MAP_ALL_ACCESS, 0, 0, 0 );
    s->shared->hwnd = (UINT)(UINT_PTR)victim;
    s->shared->wake = (UINT)(UINT_PTR)(wake_handle ? wake_handle : (HANDLE)s->peer);
    SetRect( &s->shared->rect, 30, 60, 330, 260 );
    s->shared->visible = 1;
}

static void source_start( struct source *s )
{
    PostMessageA( victim, WM_WAYLAND_REMOTE_SURFACE, GetCurrentProcessId(), (UINT)(UINT_PTR)s->mapping );
    wake( s );
    Sleep( 300 );
    printf( "  attached=%ld\n", s->shared->attached );
}

/* give the source buffers of w x h, in a section of the given size */
static DWORD *source_buffers( struct source *s, UINT w, UINT h, SIZE_T size, UINT generation )
{
    HANDLE mapping = CreateFileMappingA( INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, (DWORD)((UINT64)size >> 32), (DWORD)size, NULL );
    s->shared->seq++;
    s->shared->section = (UINT)(UINT_PTR)mapping;
    s->shared->generation = generation;
    s->shared->width = w;
    s->shared->height = h;
    s->shared->seq++;
    return MapViewOfFile( mapping, FILE_MAP_ALL_ACCESS, 0, 0, 0 );
}

static void run( const char *name )
{
    struct source s;
    DWORD *bits;
    UINT i;

    printf( "case %s\n", name );
    if (!strcmp( name, "msg" ))
    {
        HANDLE event = CreateEventA( NULL, FALSE, FALSE, NULL ), small = CreateFileMappingA( INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, 16, NULL );
        DWORD pid;
        GetWindowThreadProcessId( victim, &pid );
        PostMessageA( victim, WM_WAYLAND_REMOTE_SURFACE, 0xdeadbeef, 0x1234 );
        PostMessageA( victim, WM_WAYLAND_REMOTE_SURFACE, 0, 0 );
        PostMessageA( victim, WM_WAYLAND_REMOTE_SURFACE, GetCurrentProcessId(), 0xfffffffc );
        PostMessageA( victim, WM_WAYLAND_REMOTE_SURFACE, GetCurrentProcessId(), (UINT)(UINT_PTR)event );
        PostMessageA( victim, WM_WAYLAND_REMOTE_SURFACE, GetCurrentProcessId(), (UINT)(UINT_PTR)small );
        PostMessageA( victim, WM_WAYLAND_REMOTE_SURFACE, pid, 4 );
        PostMessageA( victim, WM_WAYLAND_REMOTE_SURFACE, pid, (LPARAM)-1 );
        for (i = 0; i < 200; i++)  /* more sources than the victim accepts */
        {
            source_init( &s, 0 );
            PostMessageA( victim, WM_WAYLAND_REMOTE_SURFACE, GetCurrentProcessId(), (UINT)(UINT_PTR)s.mapping );
        }
        Sleep( 1000 );
        return;
    }
    if (!strcmp( name, "nosock" ))
    {
        char path[MAX_PATH];
        GetModuleFileNameA( NULL, path, sizeof(path) );
        source_init( &s, CreateFileA( path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL ) );
        source_start( &s );
        return;
    }

    source_init( &s, 0 );
    if (!strcmp( name, "huge" )) source_buffers( &s, 0x7fffffff, 0x7fffffff, 4096, 1 );
    else if (!strcmp( name, "small" )) source_buffers( &s, 300, 200, 4096, 1 );
    else if (!strcmp( name, "nosect" ))
    {
        source_buffers( &s, 300, 200, 4096, 1 );
        s.shared->section = (UINT)(UINT_PTR)CreateEventA( NULL, FALSE, FALSE, NULL );
    }
    else bits = source_buffers( &s, 300, 200, 300 * 200 * 4 * 3, 1 );

    if (!strcmp( name, "odd" )) s.shared->seq = 1;
    s.shared->ready = (1 << 8) | 1;
    source_start( &s );

    if (!strcmp( name, "index" ))
    {
        static const LONG values[] = {(1 << 8) | 4, (1 << 8) | 200, (1 << 8) | 0xff, (2 << 8) | 1, -1, 0x7fffffff, 0x80000000};
        for (i = 0; i < ARRAYSIZE(values); i++) { s.shared->ready = values[i]; wake( &s ); Sleep( 100 ); }
    }
    else if (!strcmp( name, "rect" ))
    {
        static const RECT rects[] = {{0x7fffffff, 0x7fffffff, -1, -1}, {-100000, -100000, 100000, 100000}, {10, 10, 10, 10},
                                     {0x80000000, 0x80000000, 0x7fffffff, 0x7fffffff}, {0, 0, 40000, 40000}, {-5000, -5000, 100, 100}};
        for (i = 0; i < ARRAYSIZE(rects); i++)
        {
            s.shared->seq++; s.shared->rect = rects[i]; s.shared->seq++;
            s.shared->ready = (1 << 8) | 1; wake( &s ); Sleep( 200 );
        }
    }
    else if (!strcmp( name, "flood" ))
    {
        DWORD start = GetTickCount();
        for (i = 2; i < 20002; i++)
        {
            source_buffers( &s, 64, 64, 64 * 64 * 4 * 3, i );
            s.shared->ready = (i << 8) | 1; s.shared->released = 0xdeadbeef;
            wake( &s );
        }
        printf( "  20000 generations in %lu ms\n", GetTickCount() - start );
        Sleep( 2000 );
    }
    else if (!strcmp( name, "ok" ))
    {
        for (i = 0; i < 300 * 200; i++) bits[i] = 0xffff0000;
        s.shared->ready = (1 << 8) | 1; wake( &s );
        Sleep( 4000 );
        printf( "  released=%#lx, exiting without cleanup\n", s.shared->released );
        TerminateProcess( GetCurrentProcess(), 0 );
    }
    Sleep( 500 );
}

int main( int argc, char **argv )
{
    static const char *all[] = {"msg", "nosock", "huge", "small", "nosect", "odd", "index", "rect", "flood", "ok"};
    WSADATA data;
    UINT i;

    setvbuf( stdout, NULL, _IONBF, 0 );
    if (argc < 2) return 1;
    WSAStartup( MAKEWORD( 2, 2 ), &data );
    victim = (HWND)(ULONG_PTR)strtoull( argv[1], NULL, 16 );
    if (argc > 2) run( argv[2] );
    else for (i = 0; i < ARRAYSIZE(all); i++) run( all[i] );
    return 0;
}
