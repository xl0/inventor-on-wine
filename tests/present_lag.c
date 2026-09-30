/* Present lag of an offscreen client surface (D3D11 child window, FLIP_DISCARD):
 * N presents of red/green/blue, the screen pixel is read right after each Present.
 * Counts reads showing the previous frame ("one frame late") and anything else, then
 * checks the last frame is on screen 500 ms later (exit 0 = it is).
 * On Wine/X11 the present is copied from the child's redirected X window right after
 * vkQueuePresentKHR returns, unordered with the driver's own X/PRIME present (078): a
 * last frame that never shows means the copy raced the driver. Immediate reads also race
 * wined3d's CS thread (Present returns before vkQueuePresentKHR), so late/other counts
 * are noisy on Wine and only the final check is a verdict.
 * Usage: present_lag.exe [N=30] [SYNCINTERVAL=0]. Run with WINE_D3D_CONFIG=renderer=vulkan.
 * Build: x86_64-w64-mingw32-gcc -O2 -o X.exe X.c -ld3d11 -lgdi32 -luuid */
#define COBJMACROS
#include <stdio.h>
#include <stdlib.h>
#include <windows.h>
#include <d3d11.h>

#define CHECK(x) do { HRESULT hr_ = (x); if (FAILED(hr_)) { printf("FAIL %s: 0x%08lx\n", #x, hr_); exit(1); } } while (0)

static void pump( DWORD ms )
{
    DWORD end = GetTickCount() + ms;
    MSG msg;
    for (; GetTickCount() < end; Sleep( 10 ))
        while (PeekMessageA( &msg, 0, 0, 0, PM_REMOVE )) DispatchMessageA( &msg );
}

int main( int argc, char **argv )
{
    static const float colors[3][4] = {{1, 0, 0, 1}, {0, 1, 0, 1}, {0, 0, 1, 1}};
    static const COLORREF expect[3] = {RGB(255, 0, 0), RGB(0, 255, 0), RGB(0, 0, 255)};
    int n = argc > 1 ? atoi( argv[1] ) : 30, interval = argc > 2 ? atoi( argv[2] ) : 0, late = 0, other = 0;
    DXGI_SWAP_CHAIN_DESC sd = {0};
    ID3D11RenderTargetView *rtv;
    ID3D11DeviceContext *ctx;
    ID3D11Texture2D *bb;
    IDXGISwapChain *sc;
    ID3D11Device *dev;
    POINT pt = {100, 100};
    HWND hwnd, child;
    COLORREF final;
    HDC hdc;

    setvbuf( stdout, NULL, _IONBF, 0 );
    hwnd = CreateWindowA( "static", "present_lag", WS_POPUP | WS_CLIPCHILDREN | WS_VISIBLE,
                          100, 100, 400, 300, 0, 0, 0, 0 );
    child = CreateWindowA( "static", NULL, WS_CHILD | WS_VISIBLE, 100, 50, 200, 200, hwnd, 0, 0, 0 );
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
    hdc = GetDC( 0 );
    for (int i = 0; i < n; i++)
    {
        COLORREF c;
        ID3D11DeviceContext_OMSetRenderTargets( ctx, 1, &rtv, NULL );
        ID3D11DeviceContext_ClearRenderTargetView( ctx, rtv, colors[i % 3] );
        IDXGISwapChain_Present( sc, interval, 0 );
        c = GetPixel( hdc, pt.x, pt.y );
        if (c == expect[i % 3]) continue;
        if (i && c == expect[(i + 2) % 3]) late++;
        else other++;
        if (late + other <= 5) printf( "present %d: %06lx\n", i, c );
    }
    pump( 500 );
    final = GetPixel( hdc, pt.x, pt.y );
    ReleaseDC( 0, hdc );
    printf( "%d presents (sync interval %d): %d one frame late, %d other; 500 ms after the last %06lx "
            "(expect %06lx)\n", n, interval, late, other, final, expect[(n - 1) % 3] );
    return final != expect[(n - 1) % 3];
}
