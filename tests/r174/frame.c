/* frame.c: a frame window laid out like Inventor's main window, to see what is left stale after the
 * window manager resizes it (174). Children: ribbon (top strip, red), browser (left pane, green),
 * status bar (bottom strip, blue) and a D3D11 view (the rest, yellow; `nogpu`: GDI, magenta). Every
 * GDI child has a 4 px white border, so a child that is too small or not repainted shows in a
 * screenshot (check.py). Logs the size-move messages with a ms timestamp and, 600 ms after the last
 * one, a STATE line (window / client rect, last WM_SIZE, child rects vs. the expected layout, update
 * regions left; early / late = WM_SIZE before WM_ENTERSIZEMOVE / within 400 ms after WM_EXITSIZEMOVE).
 * Usage: frame.exe [SECS] [X Y W H] [OPTION..]
 *   defer    no layout on WM_SIZE between WM_ENTERSIZEMOVE and WM_EXITSIZEMOVE, one at the exit
 *   slow=MS  the layout takes MS (Inventor's takes 100+ ms)
 *   pump     the layout runs a PeekMessage loop while it is slow (WPF dispatcher style)
 *   nogpu    GDI view
 *   popup    WS_POPUP frame (client == window, like a custom caption)
 *   plain    children without CS_HREDRAW | CS_VREDRAW and without the border: a resized child only paints
 *            what became invalid, as most real windows do (check.py: BORDER=0)
 *   thread   the GDI children are painted by another thread some ms after WM_PAINT / WM_SIZE, the way
 *            WPF's render thread presents in software mode (implies plain)
 *   rgn      SetWindowRgn( whole window ) after each layout, as MFC's frames with an own caption do (Inventor)
 *   rgnpost  the same from a message posted in WM_SIZE (a SetWindowPos that doesn't move the window, handled
 *            after the X events that came in during the layout)
 *   dpi      DPI aware, sizes scaled by the system DPI (check.py: SCALE=1.5 at 144 DPI)
 * Build: x86_64-w64-mingw32-gcc -O2 -o frame.exe frame.c -ld3d11 -lgdi32 -luuid */
#define COBJMACROS
#include <windows.h>
#include <d3d11.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int scale = 96; /* system DPI when DPI aware */
#define RIBBON_H MulDiv( 120, scale, 96 )
#define BROWSER_W MulDiv( 240, scale, 96 )
#define STATUS_H MulDiv( 24, scale, 96 )
#define BORDER (plain ? 0 : MulDiv( 4, scale, 96 ))

static DWORD start;
static int enter, leave, depth, defer, slow, pump, nogpu, plain, late_size, early_size, layouts;
static DWORD exit_time;
static int threaded, set_rgn;
static LONG need_paint[3];
static HANDLE paint_event;
static HWND frame, ribbon, browser, status, view;
static SIZE last_size;
static IDXGISwapChain *sc;
static ID3D11Device *dev;
static ID3D11DeviceContext *ctx;

static void out( const char *fmt, ... )
{
    va_list args;
    printf( "%6lu ", GetTickCount() - start );
    va_start( args, fmt );
    vprintf( fmt, args );
    va_end( args );
    printf( "\n" );
    fflush( stdout );
}

static const COLORREF colors[] = {RGB(255, 0, 0), RGB(0, 255, 0), RGB(0, 0, 255), RGB(255, 0, 255)};

/* paint whole children from another thread, like a WPF render thread does after the UI thread asked for it */
static DWORD WINAPI paint_thread( void *arg )
{
    HWND *children[] = {&ribbon, &browser, &status};
    HBRUSH brush;
    RECT rc;
    HDC hdc;
    int i;

    for (;;)
    {
        WaitForSingleObject( paint_event, INFINITE );
        Sleep( 3 );  /* render time */
        for (i = 0; i < 3; i++)
        {
            if (!InterlockedExchange( &need_paint[i], 0 )) continue;
            GetClientRect( *children[i], &rc );
            hdc = GetDC( *children[i] );
            brush = CreateSolidBrush( colors[i] );
            FillRect( hdc, &rc, brush );
            DeleteObject( brush );
            ReleaseDC( *children[i], hdc );
        }
    }
    return 0;
}

static void present_view(void)
{
    static const float yellow[4] = {1, 1, 0, 1};
    ID3D11RenderTargetView *rtv;
    ID3D11Texture2D *bb;
    RECT rc;

    if (!sc) return;
    GetClientRect( view, &rc );
    if (rc.right <= 0 || rc.bottom <= 0) return;
    ID3D11DeviceContext_OMSetRenderTargets( ctx, 0, NULL, NULL );
    IDXGISwapChain_ResizeBuffers( sc, 0, rc.right, rc.bottom, DXGI_FORMAT_UNKNOWN, 0 );
    IDXGISwapChain_GetBuffer( sc, 0, &IID_ID3D11Texture2D, (void **)&bb );
    ID3D11Device_CreateRenderTargetView( dev, (ID3D11Resource *)bb, NULL, &rtv );
    ID3D11DeviceContext_ClearRenderTargetView( ctx, rtv, yellow );
    IDXGISwapChain_Present( sc, 0, 0 );
    ID3D11RenderTargetView_Release( rtv );
    ID3D11Texture2D_Release( bb );
}

static void layout(void)
{
    RECT rc;
    MSG msg;
    DWORD end = GetTickCount() + slow;

    GetClientRect( frame, &rc );
    layouts++;
    out( "layout %ldx%ld%s", rc.right, rc.bottom, depth ? " (in size-move)" : "" );
    while (GetTickCount() < end)
    {
        if (!pump) Sleep( 5 );
        else if (PeekMessageA( &msg, 0, 0, 0, PM_REMOVE )) DispatchMessageA( &msg );
        else MsgWaitForMultipleObjects( 0, NULL, FALSE, 5, QS_ALLINPUT );
    }
    GetClientRect( frame, &rc );
    MoveWindow( ribbon, 0, 0, rc.right, RIBBON_H, TRUE );
    MoveWindow( browser, 0, RIBBON_H, BROWSER_W, rc.bottom - RIBBON_H - STATUS_H, TRUE );
    MoveWindow( status, 0, rc.bottom - STATUS_H, rc.right, STATUS_H, TRUE );
    MoveWindow( view, BROWSER_W, RIBBON_H, rc.right - BROWSER_W, rc.bottom - RIBBON_H - STATUS_H, TRUE );
    if (set_rgn == 1) SendMessageA( frame, WM_APP, 0, 0 );
    else if (set_rgn) PostMessageA( frame, WM_APP, 0, 0 );
}

static int check_child( HWND hwnd, const char *name, int x, int y, int w, int h, char *buf )
{
    RECT rc, upd = {0};
    int ok, dirty;

    GetWindowRect( hwnd, &rc );
    MapWindowPoints( 0, frame, (POINT *)&rc, 2 );
    ok = rc.left == x && rc.top == y && rc.right == x + w && rc.bottom == y + h;
    dirty = GetUpdateRect( hwnd, &upd, FALSE );
    sprintf( buf + strlen( buf ), " %s %ld,%ld-%ld,%ld%s%s", name, rc.left, rc.top, rc.right, rc.bottom,
             ok ? "" : " WRONG", dirty ? " DIRTY" : "" );
    return ok;
}

static void state(void)
{
    char buf[512] = "";
    RECT win, rc;
    int ok;

    GetWindowRect( frame, &win );
    GetClientRect( frame, &rc );
    ok = last_size.cx == rc.right && last_size.cy == rc.bottom;
    ok &= check_child( ribbon, "ribbon", 0, 0, rc.right, RIBBON_H, buf );
    ok &= check_child( browser, "browser", 0, RIBBON_H, BROWSER_W, rc.bottom - RIBBON_H - STATUS_H, buf );
    ok &= check_child( status, "status", 0, rc.bottom - STATUS_H, rc.right, STATUS_H, buf );
    ok &= check_child( view, "view", BROWSER_W, RIBBON_H, rc.right - BROWSER_W, rc.bottom - RIBBON_H - STATUS_H, buf );
    out( "STATE %s win %ld,%ld-%ld,%ld client %ldx%ld last WM_SIZE %ldx%ld depth %d early %d late %d layouts %d%s",
         ok ? "ok" : "BAD", win.left, win.top, win.right, win.bottom, rc.right, rc.bottom,
         last_size.cx, last_size.cy, depth, early_size, late_size, layouts, buf );
    early_size = late_size = layouts = 0;
}

static LRESULT CALLBACK child_proc( HWND hwnd, UINT msg, WPARAM wp, LPARAM lp )
{
    static const char *names[] = {"ribbon", "browser", "status", "view"};
    int id = GetWindowLongPtrA( hwnd, GWLP_ID );

    switch (msg)
    {
    case WM_ERASEBKGND: return 1;
    case WM_SIZE:
        if (hwnd == view && sc) present_view();
        else if (threaded && id < 3) { InterlockedExchange( &need_paint[id], 1 ); SetEvent( paint_event ); }
        break;
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HBRUSH brush;
        RECT rc, in;

        BeginPaint( hwnd, &ps );
        if (threaded && id < 3)
        {
            EndPaint( hwnd, &ps );
            InterlockedExchange( &need_paint[id], 1 );
            SetEvent( paint_event );
            return 0;
        }
        out( "  paint %s %ld,%ld-%ld,%ld", names[id], ps.rcPaint.left, ps.rcPaint.top, ps.rcPaint.right, ps.rcPaint.bottom );
        if (hwnd != view || !sc)
        {
            GetClientRect( hwnd, &rc );
            in = rc;
            InflateRect( &in, -BORDER, -BORDER );
            FillRect( ps.hdc, &rc, GetStockObject( WHITE_BRUSH ) );
            brush = CreateSolidBrush( colors[id] );
            FillRect( ps.hdc, &in, brush );
            DeleteObject( brush );
        }
        EndPaint( hwnd, &ps );
        return 0;
    }
    }
    return DefWindowProcA( hwnd, msg, wp, lp );
}

static LRESULT CALLBACK frame_proc( HWND hwnd, UINT msg, WPARAM wp, LPARAM lp )
{
    switch (msg)
    {
    case WM_ENTERSIZEMOVE: enter++; depth++; out( "WM_ENTERSIZEMOVE" ); break;
    case WM_EXITSIZEMOVE:
        leave++; depth--;
        exit_time = GetTickCount();
        out( "WM_EXITSIZEMOVE" );
        if (defer) layout();
        SetTimer( hwnd, 3, 600, NULL );
        break;
    case WM_WINDOWPOSCHANGED:
    {
        WINDOWPOS *pos = (WINDOWPOS *)lp;
        if (!(pos->flags & SWP_NOMOVE) || !(pos->flags & SWP_NOSIZE))
            out( "WM_WINDOWPOSCHANGED %d,%d %dx%d flags %#x depth %d", pos->x, pos->y, pos->cx, pos->cy, pos->flags, depth );
        SetTimer( hwnd, 3, 600, NULL );
        break;
    }
    case WM_SIZE:
        last_size.cx = (short)LOWORD(lp); last_size.cy = (short)HIWORD(lp);
        out( "WM_SIZE %ldx%ld depth %d", last_size.cx, last_size.cy, depth );
        /* sizes outside the size-move: right after its end (late) or before its start (early) */
        if (!depth && enter && GetTickCount() - exit_time < 400) late_size++;
        else if (!depth && enter) early_size++;
        if (!frame) break;
        if (!defer || !depth) layout();
        return 0;
    case WM_APP:
    {
        RECT rc;
        GetWindowRect( hwnd, &rc );
        SetWindowRgn( hwnd, CreateRectRgn( 0, 0, rc.right - rc.left, rc.bottom - rc.top ), TRUE );
        return 0;
    }
    case WM_TIMER:
        if (wp == 3) { KillTimer( hwnd, 3 ); state(); }
        return 0;
    case WM_ERASEBKGND:
    {
        RECT rc;
        GetClientRect( hwnd, &rc );
        FillRect( (HDC)wp, &rc, GetStockObject( GRAY_BRUSH ) );
        return 1;
    }
    case WM_DESTROY: PostQuitMessage( 0 ); break;
    }
    return DefWindowProcA( hwnd, msg, wp, lp );
}

int main( int argc, char **argv )
{
    int secs = argc > 1 ? atoi( argv[1] ) : 60, i, popup = 0;
    int x = 200, y = 200, w = 800, h = 600;
    WNDCLASSA cls = {0};
    MSG msg;

    if (argc > 5) { x = atoi( argv[2] ); y = atoi( argv[3] ); w = atoi( argv[4] ); h = atoi( argv[5] ); }
    for (i = 1; i < argc; i++)
    {
        if (!strcmp( argv[i], "defer" )) defer = 1;
        else if (!strcmp( argv[i], "pump" )) pump = 1;
        else if (!strcmp( argv[i], "nogpu" )) nogpu = 1;
        else if (!strcmp( argv[i], "popup" )) popup = 1;
        else if (!strcmp( argv[i], "plain" )) plain = 1;
        else if (!strcmp( argv[i], "thread" )) threaded = plain = 1;
        else if (!strcmp( argv[i], "rgn" )) set_rgn = 1;
        else if (!strcmp( argv[i], "rgnpost" )) set_rgn = 2;
        else if (!strcmp( argv[i], "dpi" )) { SetProcessDPIAware(); scale = GetDpiForSystem(); }
        else if (!strncmp( argv[i], "slow=", 5 )) slow = atoi( argv[i] + 5 );
    }

    start = GetTickCount();
    cls.lpfnWndProc = frame_proc;
    cls.hInstance = GetModuleHandleA( NULL );
    cls.hCursor = LoadCursorA( NULL, (LPCSTR)IDC_ARROW );
    cls.lpszClassName = "r174_frame";
    RegisterClassA( &cls );
    cls.lpfnWndProc = child_proc;
    if (!plain) cls.style = CS_HREDRAW | CS_VREDRAW; /* the border depends on the size */
    cls.lpszClassName = "r174_child";
    RegisterClassA( &cls );

    frame = CreateWindowA( "r174_frame", "r174_frame", (popup ? WS_POPUP : WS_OVERLAPPEDWINDOW) | WS_CLIPCHILDREN,
                           x, y, w, h, NULL, NULL, cls.hInstance, NULL );
    ribbon = CreateWindowA( "r174_child", NULL, WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, frame, (HMENU)0, 0, NULL );
    browser = CreateWindowA( "r174_child", NULL, WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, frame, (HMENU)1, 0, NULL );
    status = CreateWindowA( "r174_child", NULL, WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, frame, (HMENU)2, 0, NULL );
    view = CreateWindowA( "r174_child", NULL, WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, frame, (HMENU)3, 0, NULL );
    if (!nogpu)
    {
        DXGI_SWAP_CHAIN_DESC sd = {0};
        HRESULT hr;

        sd.BufferCount = 2;
        sd.BufferDesc.Width = sd.BufferDesc.Height = 16;
        sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sd.OutputWindow = view; sd.SampleDesc.Count = 1; sd.Windowed = TRUE;
        sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        hr = D3D11CreateDeviceAndSwapChain( NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, NULL, 0,
                                            D3D11_SDK_VERSION, &sd, &sc, &dev, NULL, &ctx );
        if (FAILED(hr)) { out( "no D3D11 device %#lx, GDI view", hr ); sc = NULL; }
    }
    if (threaded)
    {
        paint_event = CreateEventA( NULL, FALSE, FALSE, NULL );
        CloseHandle( CreateThread( NULL, 0, paint_thread, NULL, 0, NULL ) );
    }
    layout();
    ShowWindow( frame, SW_SHOW );
    SetTimer( frame, 1, secs * 1000, NULL );
    out( "hwnd %p defer %d slow %d pump %d gpu %d dpi %d", frame, defer, slow, pump, !!sc, scale );
    while (GetMessageA( &msg, NULL, 0, 0 ))
    {
        if (msg.message == WM_TIMER && msg.hwnd == frame && msg.wParam == 1) DestroyWindow( frame );
        TranslateMessage( &msg );
        DispatchMessageA( &msg );
    }
    out( "summary enter %d exit %d", enter, leave );
    return enter != leave || depth;
}
