/* A child process keeps presenting D3D11 frames on its own child window inside a
 * container window of ours (like Chromium's GPU process inside Inventor's Autodesk
 * Assistant panel). We hide the container: its area must show our window again,
 * the other process' presents must stop reaching the screen (issue 065: the child
 * process' cached DC visible region wasn't invalidated by the cross-process hide).
 * Prints the screen pixel in the container area before and after the hide.
 * Exit 0 = ok. Run with WINE_D3D_CONFIG=renderer=vulkan.
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

static COLORREF screen_pixel( int x, int y )
{
    HDC hdc = GetDC( 0 );
    COLORREF c = GetPixel( hdc, x, y );
    ReleaseDC( 0, hdc );
    return c;
}

int main( int argc, char **argv )
{
    static const float green[4] = {0, 1, 0, 1};
    STARTUPINFOA si = { sizeof(si) };
    DXGI_SWAP_CHAIN_DESC sd = {0};
    ID3D11RenderTargetView *rtv;
    ID3D11DeviceContext *ctx;
    PROCESS_INFORMATION pi;
    HWND hwnd, panel, child;
    char cmd[MAX_PATH + 32];
    ID3D11Texture2D *bb;
    COLORREF before, after;
    IDXGISwapChain *sc;
    ID3D11Device *dev;
    POINT pt = {100, 100};

    setvbuf( stdout, NULL, _IONBF, 0 );
    if (argc > 1)  /* child process: present green on our child of the parent's panel, forever */
    {
        WNDCLASSA wc = {.lpfnWndProc = DefWindowProcA, .lpszClassName = "gpu_child"};

        /* no CS_PARENTDC like "static": the DCX_PARENTCLIP flip on hide would refresh the DC */
        RegisterClassA( &wc );
        panel = (HWND)(ULONG_PTR)strtoull( argv[1], NULL, 16 );
        child = CreateWindowA( "gpu_child", NULL, WS_CHILD | WS_VISIBLE, 0, 0, 200, 200, panel, 0, 0, 0 );
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
        for (;;)
        {
            ID3D11DeviceContext_OMSetRenderTargets( ctx, 1, &rtv, NULL );
            ID3D11DeviceContext_ClearRenderTargetView( ctx, rtv, green );
            IDXGISwapChain_Present( sc, 0, 0 );
            pump( 30 );
        }
    }

    hwnd = CreateWindowA( "static", "xproc_hidden_present", WS_POPUP | WS_CLIPCHILDREN | WS_VISIBLE,
                          100, 100, 400, 200, 0, 0, 0, 0 );
    panel = CreateWindowA( "static", NULL, WS_CHILD | WS_CLIPSIBLINGS | WS_VISIBLE, 200, 0, 200, 200, hwnd, 0, 0, 0 );
    pump( 500 );
    sprintf( cmd, "\"%s\" %p", argv[0], panel );
    if (!CreateProcessA( NULL, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi )) return 1;
    pump( 3000 );
    ClientToScreen( panel, &pt );
    before = screen_pixel( pt.x, pt.y );

    ShowWindow( panel, SW_HIDE );
    pump( 2000 );
    after = screen_pixel( pt.x, pt.y );

    TerminateProcess( pi.hProcess, 0 );
    printf( "panel pixel before hide %06lx (expect 00ff00), after %06lx (expect not 00ff00)\n", before, after );
    return !(before == RGB(0, 255, 0) && after != RGB(0, 255, 0));
}
