/* Wayland: toplevel whose client area is fully covered by a swapchain child (WebView2-like).
 * Args (any order): popup = WS_POPUP parent; layered = child is WS_EX_LAYERED|WS_EX_NOREDIRECTIONBITMAP|WS_EX_TRANSPARENT
 * with no SetLayeredWindowAttributes call, like Chromium/WebView2 "Intermediate D3D Window". */

#define COBJMACROS
#include <windows.h>
#include <d3d11.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { HRESULT hr_ = (x); if (FAILED(hr_)) { printf("FAIL %s: 0x%08lx\n", #x, hr_); return 1; } } while (0)

int main(int argc, char **argv)
{
    int popup = argc > 1 && (!strcmp(argv[1], "popup") || (argc > 2 && !strcmp(argv[2], "popup")));
    int layered = argc > 1 && (!strcmp(argv[1], "layered") || (argc > 2 && !strcmp(argv[2], "layered")));
    /* Top-level window whose whole client area is a child window with the swapchain (like WebView2 / Chromium):
     * the parent itself paints nothing. Presents a magenta-ish clear for ~15 s; look at the screen. */
    const float color[4] = {0.9f, 0.1f, 0.6f, 1.0f};
    DXGI_SWAP_CHAIN_DESC sd = {0};
    ID3D11Device *dev; ID3D11DeviceContext *ctx; IDXGISwapChain *sc;
    ID3D11Texture2D *bb; ID3D11RenderTargetView *rtv; int i;
    DWORD pstyle = (popup ? WS_POPUP : WS_OVERLAPPEDWINDOW) | WS_VISIBLE | WS_CLIPCHILDREN;
    HWND parent = CreateWindowA("static", "wl_childswap", pstyle, 300, 300, 640, 480, NULL, NULL, NULL, NULL);
    RECT r; HWND child; GetClientRect(parent, &r);
    child = CreateWindowExA(layered ? 0x80000 | 0x200000 | 0x20 : 0, "static", "child", WS_CHILD | WS_VISIBLE, 0, 0, r.right, r.bottom, parent, NULL, NULL, NULL);
    sd.BufferCount = 2;
    sd.BufferDesc.Width = r.right; sd.BufferDesc.Height = r.bottom;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = child; sd.SampleDesc.Count = 1; sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    CHECK(D3D11CreateDeviceAndSwapChain(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, NULL, 0,
                                        D3D11_SDK_VERSION, &sd, &sc, &dev, NULL, &ctx));
    CHECK(IDXGISwapChain_GetBuffer(sc, 0, &IID_ID3D11Texture2D, (void **)&bb));
    CHECK(ID3D11Device_CreateRenderTargetView(dev, (ID3D11Resource *)bb, NULL, &rtv));
    for (i = 0; i < 150; i++)
    {
        MSG msg;
        while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) DispatchMessageA(&msg);
        ID3D11DeviceContext_ClearRenderTargetView(ctx, rtv, color);
        CHECK(IDXGISwapChain_Present(sc, 0, 0));
        Sleep(100);
    }
    return 0;
}
