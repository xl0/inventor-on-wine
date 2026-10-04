/* What Windows shows when process B presents to a window of process A (issue 132, for the record).
 * xwin.exe                 process A: popup with a black panel per mode; starts B for each mode and reads the
 *                          screen pixel in the middle of the panel while B holds its content.
 * xwin.exe MODE HWND       process B: gdi (GetDC + FillRect red), discard / flipseq / flipdiscard (D3D11 swapchain
 *                          on HWND itself, cleared green / blue / magenta), child-discard / child-flipseq (same on an
 *                          own WS_CHILD of HWND, yellow / cyan).
 * Build: x86_64-w64-mingw32-gcc -O2 -o xwin.exe xwin.c -ld3d11 -ldxgi -luuid -lgdi32 */
#define COBJMACROS
#include <windows.h>
#include <d3d11.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void pump( DWORD ms )
{
    DWORD end = GetTickCount() + ms;
    MSG msg;
    for (; GetTickCount() < end; Sleep( 10 ))
        while (PeekMessageA( &msg, 0, 0, 0, PM_REMOVE )) DispatchMessageA( &msg );
}

static int present( HWND hwnd, DXGI_SWAP_EFFECT effect, UINT buffers, const float *color )
{
    DXGI_SWAP_CHAIN_DESC sd = {0};
    ID3D11RenderTargetView *rtv;
    ID3D11DeviceContext *ctx;
    IDXGIAdapter *adapter;
    IDXGIFactory *factory;
    IDXGIDevice *dxgi_dev;
    ID3D11Texture2D *bb;
    IDXGISwapChain *sc;
    ID3D11Device *dev;
    HRESULT hr;
    RECT rect;
    int i;

    hr = D3D11CreateDevice( NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, NULL, 0, D3D11_SDK_VERSION, &dev, NULL, &ctx );
    if (FAILED(hr)) hr = D3D11CreateDevice( NULL, D3D_DRIVER_TYPE_WARP, NULL, 0, NULL, 0, D3D11_SDK_VERSION, &dev, NULL, &ctx );
    if (FAILED(hr)) { printf( "  D3D11CreateDevice %#lx\n", hr ); return 1; }
    ID3D11Device_QueryInterface( dev, &IID_IDXGIDevice, (void **)&dxgi_dev );
    IDXGIDevice_GetAdapter( dxgi_dev, &adapter );
    IDXGIAdapter_GetParent( adapter, &IID_IDXGIFactory, (void **)&factory );

    GetClientRect( hwnd, &rect );
    sd.BufferCount = buffers;
    sd.BufferDesc.Width = rect.right; sd.BufferDesc.Height = rect.bottom;
    sd.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hwnd; sd.SampleDesc.Count = 1; sd.Windowed = TRUE;
    sd.SwapEffect = effect;
    hr = IDXGIFactory_CreateSwapChain( factory, (IUnknown *)dev, &sd, &sc );
    printf( "  CreateSwapChain %#lx\n", hr );
    if (FAILED(hr)) return 1;
    for (i = 0; i < 20; i++)
    {
        IDXGISwapChain_GetBuffer( sc, 0, &IID_ID3D11Texture2D, (void **)&bb );
        ID3D11Device_CreateRenderTargetView( dev, (ID3D11Resource *)bb, NULL, &rtv );
        ID3D11DeviceContext_ClearRenderTargetView( ctx, rtv, color );
        hr = IDXGISwapChain_Present( sc, 0, 0 );
        if (!i || FAILED(hr)) printf( "  Present %d: %#lx\n", i, hr );
        ID3D11RenderTargetView_Release( rtv ); ID3D11Texture2D_Release( bb );
        pump( 150 );
    }
    return 0;
}

int main( int argc, char **argv )
{
    static const char *modes[] = {"gdi", "discard", "flipseq", "flipdiscard", "child-discard", "child-flipseq"};
    static const float green[] = {0, 1, 0, 1}, blue[] = {0, 0, 1, 1}, magenta[] = {1, 0, 1, 1}, yellow[] = {1, 1, 0, 1}, cyan[] = {0, 1, 1, 1};

    setvbuf( stdout, NULL, _IONBF, 0 );
    if (argc > 2)
    {
        HWND hwnd = (HWND)(ULONG_PTR)strtoull( argv[2], NULL, 16 );
        const char *mode = argv[1];
        DWORD pid = 0;

        GetWindowThreadProcessId( hwnd, &pid );
        printf( "  B: pid %lu, window %p of pid %lu\n", GetCurrentProcessId(), hwnd, pid );
        if (!strncmp( mode, "child-", 6 ))
        {
            RECT rect;
            GetClientRect( hwnd, &rect );
            hwnd = CreateWindowA( "static", NULL, WS_CHILD | WS_VISIBLE, 0, 0, rect.right, rect.bottom, hwnd, 0, 0, 0 );
            printf( "  own child %p (error %lu)\n", hwnd, hwnd ? 0 : GetLastError() );
            mode += 6;
        }
        if (!strcmp( mode, "gdi" ))
        {
            HBRUSH brush = CreateSolidBrush( RGB( 255, 0, 0 ) );
            HDC hdc = GetDC( hwnd );
            RECT rect;
            int ret;
            GetClientRect( hwnd, &rect );
            ret = FillRect( hdc, &rect, brush );
            GdiFlush();
            printf( "  GetDC %p, FillRect %d (error %lu)\n", hdc, ret, GetLastError() );
            ReleaseDC( hwnd, hdc );
            pump( 3000 );
            return 0;
        }
        if (!strcmp( mode, "discard" )) return present( hwnd, DXGI_SWAP_EFFECT_DISCARD, 1, mode == argv[1] ? green : yellow );
        if (!strcmp( mode, "flipseq" )) return present( hwnd, DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL, 2, mode == argv[1] ? blue : cyan );
        return present( hwnd, DXGI_SWAP_EFFECT_FLIP_DISCARD, 2, magenta );
    }
    else
    {
        WNDCLASSA wc = {.lpfnWndProc = DefWindowProcA, .lpszClassName = "xwin_top", .hbrBackground = GetStockObject( WHITE_BRUSH )};
        WNDCLASSA pc = {.lpfnWndProc = DefWindowProcA, .lpszClassName = "xwin_panel", .hbrBackground = GetStockObject( BLACK_BRUSH )};
        HWND top;
        UINT i;

        RegisterClassA( &wc ); RegisterClassA( &pc );
        top = CreateWindowExA( WS_EX_TOPMOST, "xwin_top", "xwin", WS_POPUP | WS_CLIPCHILDREN | WS_VISIBLE, 100, 100, 700, 200, 0, 0, 0, 0 );
        pump( 500 );
        for (i = 0; i < ARRAYSIZE(modes); i++)
        {
            STARTUPINFOA si = {sizeof(si)};
            PROCESS_INFORMATION pi;
            POINT pt = {50, 50};
            char cmd[MAX_PATH + 64];
            COLORREF before, during, after;
            HWND panel;
            HDC hdc;

            panel = CreateWindowA( "xwin_panel", NULL, WS_CHILD | WS_CLIPSIBLINGS | WS_CLIPCHILDREN | WS_VISIBLE, 10 + i * 115, 10, 100, 100, top, 0, 0, 0 );
            pump( 300 );
            ClientToScreen( panel, &pt );
            hdc = GetDC( 0 ); before = GetPixel( hdc, pt.x, pt.y ); ReleaseDC( 0, hdc );
            printf( "mode %s: panel %p\n", modes[i], panel );
            sprintf( cmd, "\"%s\" %s %p", argv[0], modes[i], panel );
            if (!CreateProcessA( NULL, cmd, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi )) { printf( "CreateProcess failed\n" ); return 1; }
            pump( 2000 );
            hdc = GetDC( 0 ); during = GetPixel( hdc, pt.x, pt.y ); ReleaseDC( 0, hdc );
            WaitForSingleObject( pi.hProcess, 10000 );
            pump( 500 );
            hdc = GetDC( 0 ); after = GetPixel( hdc, pt.x, pt.y ); ReleaseDC( 0, hdc );
            printf( "  screen pixel (RGB) before %02x%02x%02x, while B presents %02x%02x%02x, after B exited %02x%02x%02x\n",
                    GetRValue( before ), GetGValue( before ), GetBValue( before ), GetRValue( during ), GetGValue( during ), GetBValue( during ),
                    GetRValue( after ), GetGValue( after ), GetBValue( after ) );
        }
        return 0;
    }
}
