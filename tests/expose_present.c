/* A D3D11 child window (offscreen client surface on Wine/X11) presents once and then
 * stops, like Inventor's viewport between redraws. Another top-level window covers it
 * and goes away again (X Expose on Wine): the screen must show the last presented
 * frame, not black (issue 061). The child ignores WM_PAINT, as Inventor's OGS does.
 * Prints the screen pixel in the child before the cover and after it's gone.
 * Exit 0 = ok. Run with WINE_D3D_CONFIG=renderer=vulkan.
 * Build: x86_64-w64-mingw32-gcc -O2 -o X.exe X.c -ld3d11 -lgdi32 -luuid */
#define COBJMACROS
#include <stdio.h>
#include <stdlib.h>
#include <windows.h>
#include <d3d11.h>

#define CHECK(x) do { HRESULT hr_ = (x); if (FAILED(hr_)) { printf("FAIL %s: 0x%08lx\n", #x, hr_); exit(1); } } while (0)

static LRESULT CALLBACK child_proc( HWND hwnd, UINT msg, WPARAM wp, LPARAM lp )
{
    if (msg == WM_ERASEBKGND) return 1;
    if (msg == WM_PAINT)  /* validate without drawing or presenting */
    {
        PAINTSTRUCT ps;
        BeginPaint( hwnd, &ps );
        EndPaint( hwnd, &ps );
        return 0;
    }
    return DefWindowProcA( hwnd, msg, wp, lp );
}

static void pump( DWORD ms )
{
    DWORD end = GetTickCount() + ms;
    MSG msg;
    for (; GetTickCount() < end; Sleep( 10 ))
        while (PeekMessageA( &msg, 0, 0, 0, PM_REMOVE )) DispatchMessageA( &msg );
}

static COLORREF screen_pixel( int x, int y )
{
    HDC hdc = GetDC( 0 );
    COLORREF c = GetPixel( hdc, x, y );
    ReleaseDC( 0, hdc );
    return c;
}

int main( int argc, char **argv )
{
    static const float red[4] = {1, 0, 0, 1}, green[4] = {0, 1, 0, 1};
    WNDCLASSA wc = {.lpfnWndProc = child_proc, .lpszClassName = "gpu_child"};
    DXGI_SWAP_CHAIN_DESC sd = {0};
    ID3D11RenderTargetView *rtv;
    ID3D11DeviceContext *ctx;
    HWND hwnd, child, cover;
    ID3D11Texture2D *bb;
    COLORREF before, after;
    IDXGISwapChain *sc;
    ID3D11Device *dev;
    POINT pt = {100, 100};

    setvbuf( stdout, NULL, _IONBF, 0 );
    RegisterClassA( &wc );
    hwnd = CreateWindowA( "static", "expose_present", WS_POPUP | WS_CLIPCHILDREN | WS_VISIBLE,
                          100, 100, 400, 300, 0, 0, 0, 0 );
    child = CreateWindowA( "gpu_child", NULL, WS_CHILD | WS_VISIBLE, 100, 50, 200, 200, hwnd, 0, 0, 0 );
    sd.BufferCount = 2;
    sd.BufferDesc.Width = 200; sd.BufferDesc.Height = 200;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = child; sd.SampleDesc.Count = 1; sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    CHECK(D3D11CreateDeviceAndSwapChain( NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, NULL, 0,
                                         D3D11_SDK_VERSION, &sd, &sc, &dev, NULL, &ctx ));
    CHECK(IDXGISwapChain_GetBuffer( sc, 0, &IID_ID3D11Texture2D, (void **)&bb ));
    CHECK(ID3D11Device_CreateRenderTargetView( dev, (ID3D11Resource *)bb, NULL, &rtv ));
    pump( 500 );
    ClientToScreen( child, &pt );
    /* the first present of a new offscreen child doesn't reach the screen on Wine: present twice */
    for (int i = 0; i < 2; i++)
    {
        ID3D11DeviceContext_OMSetRenderTargets( ctx, 1, &rtv, NULL );
        ID3D11DeviceContext_ClearRenderTargetView( ctx, rtv, i ? red : green );
        IDXGISwapChain_Present( sc, 0, 0 );
        pump( 500 );
    }
    before = screen_pixel( pt.x, pt.y );

    /* cover the child with another top-level window, then take it away */
    cover = CreateWindowExA( WS_EX_TOPMOST | WS_EX_TOOLWINDOW, "static", NULL, WS_POPUP | WS_VISIBLE,
                             pt.x - 50, pt.y - 50, 100, 100, 0, 0, 0, 0 );
    pump( 500 );
    DestroyWindow( cover );
    pump( 1000 );
    after = screen_pixel( pt.x, pt.y );

    printf( "child pixel before the cover %06lx, after %06lx (expect 0000ff = red)\n", before, after );
    return !(before == RGB(255, 0, 0) && after == RGB(255, 0, 0));
}
