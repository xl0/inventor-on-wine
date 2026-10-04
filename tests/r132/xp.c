/* Cross-process presentation probe (issue 132; winewayland: content rendered by another process).
 *
 * xp.exe host [secs=N] [busy=N] [pw=N ph=N] [popup]
 *     Top-level (700x300) with two child panels p1 (20,20 300x200) and p2 (360,20 300x200).
 *     Prints "top=HWND p1=HWND p2=HWND cover=HWND", pumps messages for secs (default 60), prints its handle count every 5 s.
 *     cover is a hidden orange sibling (below the panels as created: `wl_winctl COVER top`, show, move to cover them).
 *     busy=N: after 5 s the UI thread sleeps N seconds without pumping (do frames need the owner's pump?).
 *     pw, ph: one panel p1 of that size at (10,10) in a top-level that fits it.
 *     flip=N: the top-level is a popup shown without activation next to an active window of the thread (not managed by
 *     the Wine driver: a subsurface on Wayland); after N seconds it is activated (managed: its surface changes role).
 * xp.exe MODE [HWND] [key=val ...]
 *     MODE: foreign  swapchain on HWND itself (window of another process)          "topology A"
 *           child    own WS_CHILD window filling HWND's client area, swapchain on it "topology B" (WebView2)
 *           hidden   own top-level that is never shown
 *           visible  own visible top-level
 *     frames=N (300) sleep=MS (33) interval=N (0) color=RRGGBB (ff00ff) ex=HEX (child ex style)
 *     w=N h=N    window size for hidden / visible
 *     hold=N     keep the window and swapchain for N seconds after the last frame, without presenting
 *     cycle=N    create + present 3 frames + destroy swapchain and child window N times, then present normally
 *     follow=1   resize the child window + swapchain when HWND's client size changes
 *     quiet=1    no per-frame output
 * Frame image: four quadrants: top-left COLOR, top-right green, bottom-left blue, bottom-right white,
 * and a black bar whose x position advances with the frame number (shows orientation and liveness).
 * Prints the time each Present took (max / average) and "STALL" when one takes more than 3 s.
 * Build: x86_64-w64-mingw32-gcc -O2 -o xp.exe xp.c -ld3d11 -ldxgi -luuid -lgdi32 -lntdll */
#define COBJMACROS
#include <windows.h>
#include <d3d11.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { HRESULT hr_ = (x); if (FAILED(hr_)) { printf("FAIL %s: 0x%08lx\n", #x, hr_); exit(1); } } while (0)

/* kernel handles of this process (GetProcessHandleCount is 0 on Wine) */
typedef struct { PVOID Object; ULONG_PTR UniqueProcessId; ULONG_PTR HandleValue; ULONG GrantedAccess;
    USHORT CreatorBackTraceIndex; USHORT ObjectTypeIndex; ULONG HandleAttributes; ULONG Reserved; } HEX;
typedef struct { ULONG_PTR NumberOfHandles; ULONG_PTR Reserved; HEX Handles[1]; } HIEX;
NTSYSAPI NTSTATUS WINAPI NtQuerySystemInformation( int, void *, ULONG, ULONG * );

static DWORD handle_count(void)
{
    static HIEX *info;
    ULONG size = 64 << 20, i;
    DWORD count = 0;

    if (!info) info = VirtualAlloc( NULL, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE );
    if (NtQuerySystemInformation( 64, info, size, NULL )) return 0;
    for (i = 0; i < info->NumberOfHandles; i++) if (info->Handles[i].UniqueProcessId == GetCurrentProcessId()) count++;
    return count;
}

static volatile LONG cur_frame = -1;
static volatile DWORD present_start;

static DWORD WINAPI watchdog( void *arg )
{
    LONG reported = -2;
    for (;; Sleep( 500 ))
    {
        DWORD start = present_start;
        if (start && (int)(GetTickCount() - start) > 3000 && reported != cur_frame)
        {
            printf( "STALL: Present of frame %ld has not returned after %lu ms\n", cur_frame, GetTickCount() - start );
            reported = cur_frame;
        }
    }
    return 0;
}

static void pump(void)
{
    MSG msg;
    while (PeekMessageA( &msg, 0, 0, 0, PM_REMOVE )) DispatchMessageA( &msg );
}

static int opt( int argc, char **argv, const char *name, int def, int base )
{
    size_t len = strlen( name );
    int i;
    for (i = 1; i < argc; i++)
        if (!strncmp( argv[i], name, len ) && argv[i][len] == '=') return strtoul( argv[i] + len + 1, NULL, base );
    return def;
}

struct chain
{
    IDXGISwapChain *sc;
    ID3D11Texture2D *bb;
    UINT w, h;
    DWORD *bits;
};

static ID3D11Device *dev;
static ID3D11DeviceContext *ctx;

static void chain_create( struct chain *c, HWND hwnd, UINT w, UINT h )
{
    DXGI_SWAP_CHAIN_DESC sd = {0};
    IDXGIDevice *dxgi_dev; IDXGIAdapter *adapter; IDXGIFactory *factory;

    sd.BufferCount = 2;
    sd.BufferDesc.Width = c->w = max( w, 1 ); sd.BufferDesc.Height = c->h = max( h, 1 );
    sd.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hwnd; sd.SampleDesc.Count = 1; sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    CHECK(ID3D11Device_QueryInterface( dev, &IID_IDXGIDevice, (void **)&dxgi_dev ));
    CHECK(IDXGIDevice_GetAdapter( dxgi_dev, &adapter ));
    CHECK(IDXGIAdapter_GetParent( adapter, &IID_IDXGIFactory, (void **)&factory ));
    CHECK(IDXGIFactory_CreateSwapChain( factory, (IUnknown *)dev, &sd, &c->sc ));
    IDXGIFactory_Release( factory ); IDXGIAdapter_Release( adapter ); IDXGIDevice_Release( dxgi_dev );
    CHECK(IDXGISwapChain_GetBuffer( c->sc, 0, &IID_ID3D11Texture2D, (void **)&c->bb ));
    c->bits = malloc( c->w * c->h * 4 );
}

static void chain_destroy( struct chain *c )
{
    ID3D11Texture2D_Release( c->bb );
    IDXGISwapChain_Release( c->sc );
    free( c->bits );
}

static void chain_resize( struct chain *c, UINT w, UINT h )
{
    ID3D11Texture2D_Release( c->bb );
    free( c->bits );
    c->w = max( w, 1 ); c->h = max( h, 1 );
    CHECK(IDXGISwapChain_ResizeBuffers( c->sc, 2, c->w, c->h, DXGI_FORMAT_B8G8R8A8_UNORM, 0 ));
    CHECK(IDXGISwapChain_GetBuffer( c->sc, 0, &IID_ID3D11Texture2D, (void **)&c->bb ));
    c->bits = malloc( c->w * c->h * 4 );
}

static void chain_draw( struct chain *c, DWORD color, UINT frame )
{
    UINT x, y, bar = (frame * 4) % c->w;
    for (y = 0; y < c->h; y++)
        for (x = 0; x < c->w; x++)
        {
            DWORD px;
            if (x >= bar && x < bar + 8) px = 0xff000000;
            else if (y < c->h / 2) px = x < c->w / 2 ? 0xff000000 | color : 0xff00ff00;
            else px = x < c->w / 2 ? 0xff0000ff : 0xffffffff;
            c->bits[y * c->w + x] = px;
        }
    ID3D11DeviceContext_UpdateSubresource( ctx, (ID3D11Resource *)c->bb, 0, NULL, c->bits, c->w * 4, 0 );
}

int main( int argc, char **argv )
{
    setvbuf( stdout, NULL, _IONBF, 0 );

    if (argc < 2) return 1;
    if (!strcmp( argv[1], "host" ))
    {
        int secs = opt( argc, argv, "secs", 60, 10 ), busy = opt( argc, argv, "busy", 0, 10 ), popup = argc > 2 && !strcmp( argv[argc - 1], "popup" );
        WNDCLASSA wc = {.lpfnWndProc = DefWindowProcA, .lpszClassName = "xp_host", .hbrBackground = CreateSolidBrush( RGB( 64, 64, 64 ) )};
        WNDCLASSA pc = {.lpfnWndProc = DefWindowProcA, .lpszClassName = "xp_panel", .hbrBackground = CreateSolidBrush( RGB( 255, 255, 0 ) )};
        WNDCLASSA cc = {.lpfnWndProc = DefWindowProcA, .lpszClassName = "xp_cover", .hbrBackground = CreateSolidBrush( RGB( 255, 128, 0 ) )};
        int pw = opt( argc, argv, "pw", 0, 10 ), ph = opt( argc, argv, "ph", 0, 10 ), flip = opt( argc, argv, "flip", 0, 10 );
        DWORD start = GetTickCount(), last = 0;
        HWND top, p1, p2 = 0, cover;
        RECT rect = {0, 0, 700, 300};

        if (pw) SetRect( &rect, 0, 0, pw + 20, ph + 20 );

        RegisterClassA( &wc ); RegisterClassA( &pc ); RegisterClassA( &cc );
        AdjustWindowRect( &rect, popup ? WS_POPUP : WS_OVERLAPPEDWINDOW, FALSE );
        if (flip)
        {
            HWND main = CreateWindowA( "xp_host", "xp main", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 50, 50, 900, 500, 0, 0, 0, 0 );
            pump();
            top = CreateWindowExA( WS_EX_TOOLWINDOW, "xp_host", "xp host", WS_POPUP | WS_CLIPCHILDREN, 100, 100, 700, 300, main, 0, 0, 0 );
            ShowWindow( top, SW_SHOWNOACTIVATE );
        }
        else
        top = CreateWindowA( "xp_host", "xp host", (popup ? WS_POPUP : WS_OVERLAPPEDWINDOW) | WS_CLIPCHILDREN | WS_VISIBLE,
                             100, 100, rect.right - rect.left, rect.bottom - rect.top, 0, 0, 0, 0 );
        p1 = CreateWindowA( "xp_panel", NULL, WS_CHILD | WS_CLIPSIBLINGS | WS_CLIPCHILDREN | WS_VISIBLE, pw ? 10 : 20, pw ? 10 : 20,
                            pw ? pw : 300, pw ? ph : 200, top, 0, 0, 0 );
        if (!pw) p2 = CreateWindowA( "xp_panel", NULL, WS_CHILD | WS_CLIPSIBLINGS | WS_CLIPCHILDREN | WS_VISIBLE, 360, 20, 300, 200, top, 0, 0, 0 );
        cover = CreateWindowA( "xp_cover", NULL, WS_CHILD | WS_CLIPSIBLINGS, 0, 0, 100, 100, top, 0, 0, 0 );
        printf( "top=%p p1=%p p2=%p cover=%p\n", top, p1, p2, cover );
        while (GetTickCount() - start < secs * 1000)
        {
            pump();
            if (GetTickCount() - last >= 5000)
            {
                printf( "host: %lu s, %lu handles\n", (GetTickCount() - start) / 1000, handle_count() );
                last = GetTickCount();
            }
            if (flip && GetTickCount() - start > flip * 1000)
            {
                SetWindowLongA( top, GWL_STYLE, GetWindowLongA( top, GWL_STYLE ) | WS_CAPTION | WS_SYSMENU );
                SetWindowPos( top, 0, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED );
                SetActiveWindow( top );
                printf( "host: activated the popup, active %p\n", GetActiveWindow() );
                flip = 0;
            }
            if (busy && GetTickCount() - start > 5000)
            {
                printf( "host: not pumping for %d s\n", busy );
                Sleep( busy * 1000 );
                printf( "host: pumping again\n" );
                busy = 0;
            }
            Sleep( 10 );
        }
        return 0;
    }
    else
    {
        int frames = opt( argc, argv, "frames", 300, 10 ), delay = opt( argc, argv, "sleep", 33, 10 );
        int interval = opt( argc, argv, "interval", 0, 10 ), cycle = opt( argc, argv, "cycle", 0, 10 );
        int follow = opt( argc, argv, "follow", 0, 10 ), quiet = opt( argc, argv, "quiet", 0, 10 );
        int hold = opt( argc, argv, "hold", 0, 10 );
        DWORD handles = 0, end;
        DWORD color = opt( argc, argv, "color", 0xff00ff, 16 ), ex = opt( argc, argv, "ex", 0, 16 );
        HWND target = argc > 2 ? (HWND)(ULONG_PTR)strtoull( argv[2], NULL, 16 ) : 0, hwnd = 0;
        WNDCLASSA wc = {.lpfnWndProc = DefWindowProcA, .lpszClassName = "xp_render"};
        const char *mode = argv[1];
        LARGE_INTEGER freq, t0, t1, start;
        double total = 0, worst = 0;
        struct chain c;
        RECT rect = {0, 0, opt( argc, argv, "w", 300, 10 ), opt( argc, argv, "h", 200, 10 )};
        int i;

        RegisterClassA( &wc );
        QueryPerformanceFrequency( &freq );
        CreateThread( NULL, 0, watchdog, NULL, 0, NULL );
        CHECK(D3D11CreateDevice( NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, NULL, 0, D3D11_SDK_VERSION, &dev, NULL, &ctx ));

        for (i = 0; i <= cycle; i++)
        {
            int j;

            if (!strcmp( mode, "foreign" )) { hwnd = target; GetClientRect( hwnd, &rect ); }
            else if (!strcmp( mode, "child" ))
            {
                GetClientRect( target, &rect );
                hwnd = CreateWindowExA( ex, "xp_render", NULL, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN | (ex ? WS_DISABLED : 0),
                                        0, 0, rect.right, rect.bottom, target, 0, 0, 0 );
            }
            else hwnd = CreateWindowA( "xp_render", "xp render", WS_POPUP | (strcmp( mode, "hidden" ) ? WS_VISIBLE : 0),
                                       100, 100, rect.right, rect.bottom, 0, 0, 0, 0 );
            if (!hwnd) { printf( "no window, error %lu\n", GetLastError() ); return 1; }
            chain_create( &c, hwnd, rect.right, rect.bottom );
            if (i == cycle) break;
            if (i == 10) handles = handle_count();
            for (j = 0; j < 3; j++)
            {
                chain_draw( &c, color, j );
                CHECK(IDXGISwapChain_Present( c.sc, 0, 0 ));
                pump();
            }
            chain_destroy( &c );
            if (hwnd != target) DestroyWindow( hwnd );
            pump();
        }
        if (cycle)
        {
            printf( "%d create/destroy cycles done, handles after 10 cycles %lu, now %lu\n", cycle, handles, handle_count() );
        }
        printf( "%s: hwnd %p target %p %ux%u interval %d\n", mode, hwnd, target, c.w, c.h, interval );

        QueryPerformanceCounter( &start );
        for (i = 0; i < frames; i++)
        {
            double ms;

            pump();
            if (follow && target && GetClientRect( target, &rect ) && (rect.right != c.w || rect.bottom != c.h) && rect.right && rect.bottom)
            {
                if (hwnd != target) SetWindowPos( hwnd, 0, 0, 0, rect.right, rect.bottom, SWP_NOZORDER | SWP_NOACTIVATE );
                chain_resize( &c, rect.right, rect.bottom );
                if (!quiet) printf( "frame %d: resized to %ldx%ld\n", i, rect.right, rect.bottom );
            }
            chain_draw( &c, color, i );
            cur_frame = i;
            QueryPerformanceCounter( &t0 );
            present_start = GetTickCount() | 1;
            CHECK(IDXGISwapChain_Present( c.sc, interval, 0 ));
            present_start = 0;
            QueryPerformanceCounter( &t1 );
            ms = (t1.QuadPart - t0.QuadPart) * 1000.0 / freq.QuadPart;
            total += ms; if (ms > worst) worst = ms;
            if (!quiet && (i < 5 || ms > 100)) printf( "frame %d: Present took %.1f ms\n", i, ms );
            if (delay) Sleep( delay );
        }
        QueryPerformanceCounter( &t1 );
        printf( "%d presents: average %.2f ms, max %.1f ms; %.2f ms per frame overall\n", frames, total / frames, worst,
                (t1.QuadPart - start.QuadPart) * 1000.0 / freq.QuadPart / frames );
        for (end = GetTickCount() + hold * 1000; hold && GetTickCount() < end; Sleep( 10 )) pump();
        chain_destroy( &c );
        return 0;
    }
}
