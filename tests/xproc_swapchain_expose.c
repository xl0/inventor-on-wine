/* Borderless popup whose client area is entirely covered by D3D11 swapchains
 * of another process (like Chromium's GPU process drawing on the browser HWND):
 * the owner's window surface clip region is empty. The child process presents
 * red on the popup and green on a child window of it (which makes the popup's
 * client surface offscreen, composited onto the toplevel X window) once.
 * After an X expose the popup must not show the owner's (grey/white) window
 * surface (issue 006); black (lost, not re-presented) is expected on master.
 * Run with WINE_D3D_CONFIG=renderer=vulkan.
 * Build: x86_64-w64-mingw32-gcc -O2 -o X.exe X.c -ld3d11 -luuid */
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

static void present( HWND hwnd, UINT size, const float color[4] )
{
    DXGI_SWAP_CHAIN_DESC sd = {0};
    ID3D11RenderTargetView *rtv;
    ID3D11DeviceContext *ctx;
    ID3D11Texture2D *bb;
    IDXGISwapChain *sc;
    ID3D11Device *dev;

    sd.BufferCount = 2;
    sd.BufferDesc.Width = size; sd.BufferDesc.Height = size;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hwnd; sd.SampleDesc.Count = 1; sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    CHECK(D3D11CreateDeviceAndSwapChain( NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, NULL, 0,
                                         D3D11_SDK_VERSION, &sd, &sc, &dev, NULL, &ctx ));
    CHECK(IDXGISwapChain_GetBuffer( sc, 0, &IID_ID3D11Texture2D, (void **)&bb ));
    CHECK(ID3D11Device_CreateRenderTargetView( dev, (ID3D11Resource *)bb, NULL, &rtv ));
    ID3D11DeviceContext_ClearRenderTargetView( ctx, rtv, color );
    CHECK(IDXGISwapChain_Present( sc, 0, 0 ));
}

int main( int argc, char **argv )
{
    static const float red[4] = {1, 0, 0, 1}, green[4] = {0, 1, 0, 1};
    STARTUPINFOA si = { sizeof(si) };
    PROCESS_INFORMATION pi;
    char cmd[MAX_PATH + 32];
    HWND hwnd, child;

    setvbuf( stdout, NULL, _IONBF, 0 );
    if (argc > 1)  /* child process: draw into the parent's window */
    {
        hwnd = (HWND)(ULONG_PTR)strtoull( argv[1], NULL, 16 );
        child = CreateWindowA( "static", NULL, WS_CHILD | WS_VISIBLE, 0, 0, 20, 20, hwnd, 0, 0, 0 );
        present( hwnd, 200, red );
        present( child, 20, green );
        printf( "presented\n" );
        pump( 20000 );
        return 0;
    }

    hwnd = CreateWindowA( "static", "xproc", WS_POPUP | WS_CLIPCHILDREN | WS_VISIBLE, 100, 100, 200, 200, 0, 0, 0, 0 );
    pump( 500 );
    sprintf( cmd, "\"%s\" %p", argv[0], hwnd );
    if (!CreateProcessA( NULL, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi )) return 1;
    pump( 3000 );
    /* the child's pixel format doesn't update our surface clip, force it */
    SetWindowPos( hwnd, 0, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED );
    pump( 17000 );
    return 0;
}
