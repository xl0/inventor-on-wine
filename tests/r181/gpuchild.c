/* gpuchild.c: what the screen shows of a GPU-presented window after it is resized / moved / presented
 * partially (181). Every check reads the screen from another process (this exe, `pix`), so nothing the
 * checker does flushes or repaints for the process under test, and the thread waits like an idle
 * application (no polling). Park the pointer away from the window: input repairs some of the cases.
 * Usage: gpuchild.exe MODE [N] [OPTION..]
 *   grow     D3D11 child: N (10) resizes (+60, back, ...), each followed by one ResizeBuffers + Present
 *            of a new colour from WM_SIZE; the corner of the new size must have that colour
 *   move     D3D11 child presents once, then moves by 80,60 without presenting, N times there and
 *            back: the new place must show the frame, the old one the parent
 *   movesib  two D3D11 children, A above B (`below`: B lower in the z-order than A): A grows over B's place
 *            and presents a new colour at once, then B moves down out of the way without presenting (a
 *            layout pass of WPF panes), N times down and back: B's new place must show B's frame
 *   partial  D3D9 child, D3DSWAPEFFECT_COPY: one full present (red), then N presents of a 40x40
 *            destination rectangle in a new colour: the square must show it, the rest stays red; at the
 *            end another window covers the child beside the square, then over it, and goes away: square
 *            and rest must be as before (Wine/X11 without a compositing manager: the cover over the
 *            square brings back the last full frame, the square is lost)
 *   options: top       the swapchain is on the top-level window itself (no child; grow / partial)
 *            d3d9      grow / move with a D3D9 device (D3DSWAPEFFECT_DISCARD, Reset on resize)
 *            flip      D3D11 flip-model swapchain (default: DXGI_SWAP_EFFECT_DISCARD)
 *            wait=MS   time between an action and its check (default 400)
 *            nudge     partial: resize the window by a pixel and back before the partial presents (Wine: the
 *                      DC that wined3d holds since the swapchain was created is validated again)
 * Prints one line per step and "RESULT bad N of M"; exit 0 = all ok.
 * Build: x86_64-w64-mingw32-gcc -O2 -o gpuchild.exe gpuchild.c -ld3d11 -ld3d9 -lgdi32 -luuid */
#define COBJMACROS
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include <d3d11.h>
#include <d3d9.h>

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))
#define CHECK(x) do { HRESULT hr_ = (x); if (FAILED(hr_)) { printf( "FAIL %s: 0x%08lx\n", #x, hr_ ); exit( 2 ); } } while (0)

static const COLORREF colors[] = {RGB(255, 255, 0), RGB(0, 255, 255), RGB(255, 0, 255), RGB(0, 255, 0), RGB(0, 0, 255), RGB(255, 128, 0)};
#define PARENT_COLOR RGB(96, 96, 96)

static int use_d3d9, flip, top, nudge, below, wait_ms = 400, step, on_size;
static HWND frame, target;
static int paints;      /* WM_PAINT messages of the other child (movesib) */
static RECT paint_rect; /* the last one's rectangle */
static IDXGISwapChain *sc;
static ID3D11Device *dev;
static ID3D11DeviceContext *ctx;
static IDirect3D9 *d3d9;
static IDirect3DDevice9 *dev9;
static D3DPRESENT_PARAMETERS pp;

static void present_color( COLORREF c, const RECT *dst )
{
    RECT rc;

    GetClientRect( target, &rc );
    if (rc.right <= 0 || rc.bottom <= 0) return;
    if (dev9)
    {
        if (pp.SwapEffect != D3DSWAPEFFECT_COPY && (pp.BackBufferWidth != rc.right || pp.BackBufferHeight != rc.bottom))
        {
            pp.BackBufferWidth = rc.right;
            pp.BackBufferHeight = rc.bottom;
            CHECK(IDirect3DDevice9_Reset( dev9, &pp ));
        }
        CHECK(IDirect3DDevice9_Clear( dev9, 0, NULL, D3DCLEAR_TARGET, D3DCOLOR_XRGB(GetRValue(c), GetGValue(c), GetBValue(c)), 0, 0 ));
        CHECK(IDirect3DDevice9_Present( dev9, dst, dst, NULL, NULL ));
    }
    else
    {
        float color[4] = {GetRValue(c) / 255.f, GetGValue(c) / 255.f, GetBValue(c) / 255.f, 1};
        ID3D11RenderTargetView *rtv;
        DXGI_SWAP_CHAIN_DESC desc;
        ID3D11Texture2D *bb;

        IDXGISwapChain_GetDesc( sc, &desc );
        if (desc.BufferDesc.Width != rc.right || desc.BufferDesc.Height != rc.bottom)
        {
            ID3D11DeviceContext_OMSetRenderTargets( ctx, 0, NULL, NULL );
            CHECK(IDXGISwapChain_ResizeBuffers( sc, 0, rc.right, rc.bottom, DXGI_FORMAT_UNKNOWN, 0 ));
        }
        CHECK(IDXGISwapChain_GetBuffer( sc, 0, &IID_ID3D11Texture2D, (void **)&bb ));
        CHECK(ID3D11Device_CreateRenderTargetView( dev, (ID3D11Resource *)bb, NULL, &rtv ));
        ID3D11DeviceContext_ClearRenderTargetView( ctx, rtv, color );
        CHECK(IDXGISwapChain_Present( sc, 0, 0 ));
        ID3D11RenderTargetView_Release( rtv );
        ID3D11Texture2D_Release( bb );
    }
}

static LRESULT CALLBACK target_proc( HWND hwnd, UINT msg, WPARAM wp, LPARAM lp )
{
    switch (msg)
    {
    case WM_ERASEBKGND: if (hwnd == target || hwnd != frame) return 1; break;
    case WM_SIZE:
        if (hwnd == target && on_size) present_color( colors[step % ARRAY_SIZE(colors)], NULL );
        break;
    case WM_PAINT:  /* validate without drawing or presenting, the window presents when it wants to */
        if (hwnd == target || hwnd != frame)
        {
            PAINTSTRUCT ps;
            BeginPaint( hwnd, &ps );
            if (hwnd != target) { paints++; paint_rect = ps.rcPaint; }
            EndPaint( hwnd, &ps );
            return 0;
        }
        break;
    }
    return DefWindowProcA( hwnd, msg, wp, lp );
}

/* wait like an idle application: no polling, messages are only looked at when the queue is signaled */
static void idle( DWORD ms )
{
    DWORD end = GetTickCount() + ms, now;
    MSG msg;

    while ((int)(end - (now = GetTickCount())) > 0)
    {
        if (MsgWaitForMultipleObjects( 0, NULL, FALSE, end - now, QS_ALLINPUT ) != WAIT_OBJECT_0) break;
        while (PeekMessageA( &msg, 0, 0, 0, PM_REMOVE )) DispatchMessageA( &msg );
    }
}

/* screen pixels read by another process; points are client coordinates of hwnd */
static void screen_pixels( HWND hwnd, const POINT *pts, COLORREF *res, int count )
{
    char cmd[512], buf[256] = {0}, *p;
    SECURITY_ATTRIBUTES sa = {sizeof(sa), NULL, TRUE};
    STARTUPINFOA si = {sizeof(si)};
    PROCESS_INFORMATION pi;
    HANDLE rd, wr;
    DWORD len, total = 0;
    int i;

    GetModuleFileNameA( NULL, buf, sizeof(buf) );
    p = cmd + sprintf( cmd, "\"%s\" pix", buf );
    for (i = 0; i < count; i++)
    {
        POINT pt = pts[i];
        ClientToScreen( hwnd, &pt );
        p += sprintf( p, " %ld %ld", pt.x, pt.y );
    }
    CreatePipe( &rd, &wr, &sa, 0 );
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = wr;
    si.hStdError = GetStdHandle( STD_ERROR_HANDLE );
    if (!CreateProcessA( NULL, cmd, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi )) { printf( "FAIL CreateProcess\n" ); exit( 2 ); }
    CloseHandle( wr );
    memset( buf, 0, sizeof(buf) );
    while (total < sizeof(buf) - 1 && ReadFile( rd, buf + total, sizeof(buf) - 1 - total, &len, NULL ) && len) total += len;
    WaitForSingleObject( pi.hProcess, INFINITE );
    CloseHandle( pi.hProcess ); CloseHandle( pi.hThread ); CloseHandle( rd );
    for (i = 0, p = buf; i < count; i++) res[i] = strtoul( p, &p, 16 );
}

static int near_color( COLORREF a, COLORREF b )
{
    return abs( GetRValue(a) - GetRValue(b) ) < 24 && abs( GetGValue(a) - GetGValue(b) ) < 24 && abs( GetBValue(a) - GetBValue(b) ) < 24;
}

static void create_device(void)
{
    RECT rc;

    GetClientRect( target, &rc );
    if (use_d3d9)
    {
        d3d9 = Direct3DCreate9( D3D_SDK_VERSION );
        pp.Windowed = TRUE;
        pp.hDeviceWindow = target;
        if (!pp.SwapEffect) pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
        pp.BackBufferWidth = rc.right;
        pp.BackBufferHeight = rc.bottom;
        pp.BackBufferFormat = D3DFMT_X8R8G8B8;
        pp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
        CHECK(IDirect3D9_CreateDevice( d3d9, D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, target, D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, &dev9 ));
    }
    else
    {
        DXGI_SWAP_CHAIN_DESC sd = {0};
        sd.BufferCount = flip ? 2 : 1;
        sd.BufferDesc.Width = rc.right; sd.BufferDesc.Height = rc.bottom;
        sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sd.OutputWindow = target; sd.SampleDesc.Count = 1; sd.Windowed = TRUE;
        sd.SwapEffect = flip ? DXGI_SWAP_EFFECT_FLIP_DISCARD : DXGI_SWAP_EFFECT_DISCARD;
        CHECK(D3D11CreateDeviceAndSwapChain( NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, NULL, 0, D3D11_SDK_VERSION, &sd, &sc, &dev, NULL, &ctx ));
    }
}

int main( int argc, char **argv )
{
    WNDCLASSA wc = {.lpfnWndProc = target_proc, .lpszClassName = "r181", .hCursor = LoadCursorA( 0, (char *)IDC_ARROW )};
    const char *mode = argc > 1 ? argv[1] : "";
    int i, n = 10, bad = 0, checks = 0;
    COLORREF got[4];
    RECT rc;

    setvbuf( stdout, NULL, _IONBF, 0 );
    SetProcessDPIAware();
    if (!strcmp( mode, "pix" ))
    {
        HDC hdc = GetDC( 0 );
        for (i = 2; i + 1 < argc; i += 2) printf( "%06lx ", GetPixel( hdc, atoi( argv[i] ), atoi( argv[i + 1] ) ) );
        printf( "\n" );
        ReleaseDC( 0, hdc );
        return 0;
    }
    for (i = 2; i < argc; i++)
    {
        if (!strcmp( argv[i], "d3d9" )) use_d3d9 = 1;
        else if (!strcmp( argv[i], "flip" )) flip = 1;
        else if (!strcmp( argv[i], "top" )) top = 1;
        else if (!strcmp( argv[i], "nudge" )) nudge = 1;
        else if (!strcmp( argv[i], "below" )) below = 1;
        else if (!strncmp( argv[i], "wait=", 5 )) wait_ms = atoi( argv[i] + 5 );
        else if (atoi( argv[i] )) n = atoi( argv[i] );
    }
    if (!strcmp( mode, "partial" )) { use_d3d9 = 1; pp.SwapEffect = D3DSWAPEFFECT_COPY; }
    else if (strcmp( mode, "grow" ) && strcmp( mode, "move" ) && strcmp( mode, "movesib" )) { printf( "usage: gpuchild.exe grow|move|movesib|partial [N] [top] [d3d9] [flip] [below] [nudge] [wait=MS]\n" ); return 2; }

    wc.hbrBackground = CreateSolidBrush( PARENT_COLOR );
    RegisterClassA( &wc );
    frame = CreateWindowExA( WS_EX_TOPMOST, "r181", "r181_gpuchild", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN | WS_VISIBLE, 200, 150, 700, 560, 0, 0, 0, 0 );
    target = top ? frame : CreateWindowA( "r181", NULL, WS_CHILD | WS_VISIBLE, 100, 80, 300, 200, frame, 0, 0, 0 );
    idle( 500 );
    create_device();
    present_color( RGB(255, 0, 0), NULL );
    idle( 1000 );

    if (!strcmp( mode, "grow" ))
    {
        on_size = 1;
        for (step = 1; step <= n; step++)
        {
            POINT pts[2];
            COLORREF c = colors[step % ARRAY_SIZE(colors)];
            int d = (step & 1) ? 60 : -60, ok;

            GetWindowRect( target, &rc );
            SetWindowPos( target, 0, 0, 0, rc.right - rc.left + d, rc.bottom - rc.top + d, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE );
            idle( wait_ms );
            GetClientRect( target, &rc );
            pts[0].x = rc.right / 2; pts[0].y = rc.bottom / 2;  /* middle */
            pts[1].x = rc.right - 8; pts[1].y = rc.bottom - 8;  /* in the strip a growth added */
            screen_pixels( target, pts, got, 2 );
            ok = near_color( got[0], c ) && near_color( got[1], c );
            printf( "%s %ldx%ld: middle %06lx corner %06lx, expect %06lx %s\n", d > 0 ? "grow  " : "shrink", rc.right, rc.bottom,
                    got[0], got[1], c, ok ? "ok" : "BAD" );
            checks++; bad += !ok;
        }
    }
    else if (!strcmp( mode, "movesib" ))
    {
        static const float blue[4] = {0, 0, 1, 1};
        DXGI_SWAP_CHAIN_DESC sd = {0};
        ID3D11RenderTargetView *rtv;
        IDXGISwapChain *sc_b;
        IDXGIFactory *factory;
        IDXGIAdapter *adapter;
        IDXGIDevice *dxgi_dev;
        ID3D11Texture2D *bb;
        HWND b;

        /* the target is A: 300x200 at 100,80; B is a strip right below it */
        b = CreateWindowA( "r181", NULL, WS_CHILD | WS_VISIBLE, 100, 280, 300, 40, frame, 0, 0, 0 );
        if (below) SetWindowPos( b, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE );
        CHECK(ID3D11Device_QueryInterface( dev, &IID_IDXGIDevice, (void **)&dxgi_dev ));
        CHECK(IDXGIDevice_GetAdapter( dxgi_dev, &adapter ));
        CHECK(IDXGIAdapter_GetParent( adapter, &IID_IDXGIFactory, (void **)&factory ));
        sd.BufferCount = 1;
        sd.BufferDesc.Width = 300; sd.BufferDesc.Height = 40;
        sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sd.OutputWindow = b; sd.SampleDesc.Count = 1; sd.Windowed = TRUE;
        CHECK(IDXGIFactory_CreateSwapChain( factory, (IUnknown *)dev, &sd, &sc_b ));
        CHECK(IDXGISwapChain_GetBuffer( sc_b, 0, &IID_ID3D11Texture2D, (void **)&bb ));
        CHECK(ID3D11Device_CreateRenderTargetView( dev, (ID3D11Resource *)bb, NULL, &rtv ));
        ID3D11DeviceContext_ClearRenderTargetView( ctx, rtv, blue );
        CHECK(IDXGISwapChain_Present( sc_b, 0, 0 ));
        idle( 1000 );
        {
            POINT pts[2] = {{20, 20}, {280, 20}};
            screen_pixels( b, pts, got, 2 );
            printf( "B before: %06lx %06lx (expect ff0000)\n", got[0], got[1] );
        }
        on_size = 1;
        for (step = 1; step <= n; step++)
        {
            POINT pts[2] = {{20, 20}, {280, 20}}, pa;
            COLORREF c = colors[step % ARRAY_SIZE(colors)];
            int d = (step & 1) ? 60 : -60, ok;

            paints = 0;
            GetWindowRect( target, &rc );
            SetWindowPos( target, 0, 0, 0, rc.right - rc.left, rc.bottom - rc.top + d, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE );
            GetWindowRect( b, &rc );
            MapWindowPoints( 0, frame, (POINT *)&rc, 2 );
            SetWindowPos( b, 0, rc.left, rc.top + d, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE );
            idle( wait_ms );
            GetClientRect( target, &rc );
            pa.x = 150; pa.y = rc.bottom - 10;
            screen_pixels( b, pts, got, 2 );
            screen_pixels( target, &pa, got + 2, 1 );
            ok = near_color( got[0], RGB(0, 0, 255) ) && near_color( got[1], RGB(0, 0, 255) ) && near_color( got[2], c );
            printf( "A %+d, B moved: B %06lx %06lx (expect ff0000), bottom of A %06lx (expect %06lx) %s; B got %d WM_PAINT %ld,%ld-%ld,%ld\n", d,
                    got[0], got[1], got[2], c, ok ? "ok" : "BAD", paints, paint_rect.left, paint_rect.top, paint_rect.right, paint_rect.bottom );
            checks++; bad += !ok;
        }
    }
    else if (!strcmp( mode, "move" ))
    {
        for (step = 1; step <= n; step++)
        {
            POINT pts[2] = {{20, 20}, {280, 180}}, old[2];
            int dx = (step & 1) ? 80 : -80, dy = (step & 1) ? 60 : -60, ok;
            HWND parent = GetParent( target );

            GetWindowRect( target, &rc );
            MapWindowPoints( 0, parent, (POINT *)&rc, 2 );
            /* points of the old place that the new one doesn't cover */
            old[0].x = dx > 0 ? rc.left + 20 : rc.right - 20; old[0].y = rc.top + 100;
            old[1].x = rc.left + 150; old[1].y = dy > 0 ? rc.top + 20 : rc.bottom - 20;
            SetWindowPos( target, 0, rc.left + dx, rc.top + dy, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE );
            idle( wait_ms );
            screen_pixels( target, pts, got, 2 );
            screen_pixels( parent, old, got + 2, 2 );
            ok = near_color( got[0], RGB(255, 0, 0) ) && near_color( got[1], RGB(255, 0, 0) ) &&
                 near_color( got[2], PARENT_COLOR ) && near_color( got[3], PARENT_COLOR );
            printf( "move %+d,%+d: new place %06lx %06lx (expect 0000ff), old place %06lx %06lx (expect %06lx) %s\n", dx, dy,
                    got[0], got[1], got[2], got[3], PARENT_COLOR, ok ? "ok" : "BAD" );
            checks++; bad += !ok;
        }
    }
    else
    {
        POINT pts[2] = {{40, 40}, {200, 150}}, pt = {0, 0};
        RECT dst = {20, 20, 60, 60};
        COLORREF c = 0;
        HWND cover;
        int ok;

        if (nudge)
        {
            GetWindowRect( target, &rc );
            SetWindowPos( target, 0, 0, 0, rc.right - rc.left + 1, rc.bottom - rc.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE );
            SetWindowPos( target, 0, 0, 0, rc.right - rc.left, rc.bottom - rc.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE );
            present_color( RGB(255, 0, 0), NULL );
            idle( 500 );
        }
        for (step = 1; step <= n; step++)
        {
            c = colors[step % ARRAY_SIZE(colors)];
            present_color( c, &dst );
            idle( wait_ms );
            screen_pixels( target, pts, got, 2 );
            ok = near_color( got[0], c ) && near_color( got[1], RGB(255, 0, 0) );
            printf( "partial present: square %06lx (expect %06lx), rest %06lx (expect 0000ff) %s\n", got[0], c, got[1], ok ? "ok" : "BAD" );
            checks++; bad += !ok;
        }
        /* another window covers a part of the target and goes away: first beside the square, then over it */
        ClientToScreen( target, &pt );
        for (step = 0; step < 2; step++)
        {
            cover = CreateWindowExA( WS_EX_TOPMOST | WS_EX_TOOLWINDOW, "static", NULL, WS_POPUP | WS_VISIBLE,
                                     pt.x + (step ? 0 : 120), pt.y, step ? 260 : 140, 190, 0, 0, 0, 0 );
            idle( 500 );
            DestroyWindow( cover );
            idle( 1000 );
            screen_pixels( target, pts, got, 2 );
            ok = near_color( got[0], c ) && near_color( got[1], RGB(255, 0, 0) );
            printf( "after a cover %s the square: square %06lx (expect %06lx), rest %06lx (expect 0000ff) %s\n",
                    step ? "over" : "beside", got[0], c, got[1], ok ? "ok" : "BAD" );
            checks++; bad += !ok;
        }
    }
    printf( "RESULT bad %d of %d\n", bad, checks );
    return bad != 0;
}
