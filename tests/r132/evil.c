/* Hostile "source" for winewayland's cross-process client surfaces (issue 132): speaks the driver's
 * protocol from the Win32 side with invalid data. The owner (e.g. `xp.exe host`) must survive every case
 * and keep showing honest sources. Wine only; mirrors struct remote_shared of dlls/winewayland.drv/wayland_remote.c.
 *
 * evil.exe HWND [CASE [N [CLIENT]]]   HWND = a top-level window of the victim process; without CASE all cases run.
 *   CLIENT = the window the source claims to present to (default HWND, i.e. over the whole client area).
 * Cases: msg (bogus first-contact messages, then 200 sources: prints how many the victim took), nosock (wake
 *   handle is a file), huge (2^31 x 2^31 buffers), small (section smaller than the buffers), nosect (buffer
 *   section is an event), odd (sequence lock never released), index (frame index out of range), hwnd (client
 *   windows that are not in the victim's top-level: bogus, desktop, own window), flood (N, default 20000,
 *   generation changes + window change notifications + wakes), ok (valid: a red frame over the whole client
 *   area of HWND, then the process exits without cleanup).
 * Build: x86_64-w64-mingw32-gcc -O2 -o evil.exe evil.c -lws2_32 */
#include <winsock2.h>
#include <windows.h>
#include <stdio.h>
#include <string.h>

#define WM_WAYLAND_REMOTE_SURFACE (0x80001000 + 3)

struct remote_shared
{
    LONG seq; UINT hwnd, wake, section, generation, width, height;
    LONG ready, released, attached;
};

static HWND victim, client;

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
    s->shared->hwnd = (UINT)(UINT_PTR)client;
    s->shared->wake = (UINT)(UINT_PTR)(wake_handle ? wake_handle : (HANDLE)s->peer);
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

static void run( const char *name, UINT count )
{
    struct source s, all[200];
    DWORD *bits;
    UINT i, n;

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
            source_init( &all[i], 0 );
            PostMessageA( victim, WM_WAYLAND_REMOTE_SURFACE, GetCurrentProcessId(), (UINT)(UINT_PTR)all[i].mapping );
        }
        Sleep( 1500 );
        for (i = n = 0; i < 200; i++) n += all[i].shared->attached;
        printf( "  %u of 200 sources attached\n", n );
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
    else if (!strcmp( name, "hwnd" ))
    {
        /* the client window is read once, so each one needs a source of its own */
        HWND own = CreateWindowA( "static", "evil", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 0, 0, 200, 200, 0, 0, 0, 0 );
        HWND hwnds[] = {(HWND)(UINT_PTR)0xdeadbeef, GetDesktopWindow(), own, 0};
        for (i = 0; i < ARRAYSIZE(hwnds); i++)
        {
            source_init( &s, 0 );
            s.shared->hwnd = (UINT)(UINT_PTR)hwnds[i];
            source_buffers( &s, 300, 200, 300 * 200 * 4 * 3, 1 );
            s.shared->ready = (1 << 8) | 1;
            source_start( &s );
        }
    }
    else if (!strcmp( name, "flood" ))
    {
        DWORD start = GetTickCount();
        for (i = 2; i < count + 2; i++)
        {
            source_buffers( &s, 64, 64, 64 * 64 * 4 * 3, i & 0xffffff );
            s.shared->ready = (i << 8) | 1; s.shared->released = 0xdeadbeef;
            wake( &s );
        }
        printf( "  %u generations in %lu ms\n", count, GetTickCount() - start );
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
    static const char *all[] = {"msg", "nosock", "huge", "small", "nosect", "odd", "index", "hwnd", "flood", "ok"};
    WSADATA data;
    UINT i;

    setvbuf( stdout, NULL, _IONBF, 0 );
    if (argc < 2) return 1;
    WSAStartup( MAKEWORD( 2, 2 ), &data );
    client = victim = (HWND)(ULONG_PTR)strtoull( argv[1], NULL, 16 );
    if (argc > 4) client = (HWND)(ULONG_PTR)strtoull( argv[4], NULL, 16 );
    if (argc > 2) run( argv[2], argc > 3 ? atoi( argv[3] ) : 20000 );
    else for (i = 0; i < ARRAYSIZE(all); i++) run( all[i], 20000 );
    return 0;
}
