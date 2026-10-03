/* Minimal D3D11 smoke test: window + swapchain, clear + present N frames,
 * read back the backbuffer center pixel. Prints adapter, fps, pixel. Exit 0 = ok. */
#define COBJMACROS
#include <windows.h>
#include <d3d11.h>
#include <stdio.h>
#include <stdlib.h>

#define CHECK(x) do { HRESULT hr_ = (x); if (FAILED(hr_)) { printf("FAIL %s: 0x%08lx\n", #x, hr_); return 1; } } while (0)

/* Cross-process swapchain (Chromium/WebView2 GPU process draws into a window of the browser process).
 * wl_xswap.exe            -> process A: window with a child covering the client area, prints "child=HWND", pumps 40 s
 * wl_xswap.exe HWND       -> process B: D3D11 swapchain on that HWND, presents a magenta clear for ~12 s
 * Expect magenta inside A's window (X11: yes). */
int main(int argc, char **argv)
{
    MSG msg; DWORD end = GetTickCount() + 40000;
    if (argc < 2)
    {
        HWND parent = CreateWindowA("static", "wl_xswap A", WS_OVERLAPPEDWINDOW | WS_VISIBLE | WS_CLIPCHILDREN, 300, 300, 640, 480, NULL, NULL, NULL, NULL);
        RECT r; HWND child; GetClientRect(parent, &r);
        child = CreateWindowExA(0x80000 | 0x200000 | 0x20, "static", "child", WS_CHILD | WS_VISIBLE, 0, 0, r.right, r.bottom, parent, NULL, NULL, NULL);
        printf("child=%p\n", child); fflush(stdout);
        while (GetTickCount() < end) { Sleep(10); while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) DispatchMessageA(&msg); }
        return 0;
    }
    else
    {
        const float color[4] = {0.9f, 0.1f, 0.6f, 1.0f};
        DXGI_SWAP_CHAIN_DESC sd = {0};
        ID3D11Device *dev; ID3D11DeviceContext *ctx; IDXGISwapChain *sc;
        ID3D11Texture2D *bb; ID3D11RenderTargetView *rtv; RECT r; int i;
        HWND child = (HWND)(ULONG_PTR)strtoull(argv[1], NULL, 16);
        GetClientRect(child, &r);
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
        for (i = 0; i < 120; i++)
        {
            ID3D11DeviceContext_ClearRenderTargetView(ctx, rtv, color);
            CHECK(IDXGISwapChain_Present(sc, 0, 0));
            Sleep(100);
        }
    }
    return 0;
}
