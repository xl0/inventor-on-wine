/* Minimal D3D11 smoke test: window + swapchain, clear + present N frames,
 * read back the backbuffer center pixel. Prints adapter, fps, pixel. Exit 0 = ok. */
#define COBJMACROS
#include <windows.h>
#include <d3d11.h>
#include <stdio.h>
#include <stdlib.h>

#define CHECK(x) do { HRESULT hr_ = (x); if (FAILED(hr_)) { printf("FAIL %s: 0x%08lx\n", #x, hr_); return 1; } } while (0)

int main(void)
{
    const float color[4] = {0.25f, 0.5f, 0.75f, 1.0f};
    const int frames = 300;
    DXGI_SWAP_CHAIN_DESC sd = {0};
    ID3D11Device *dev; ID3D11DeviceContext *ctx; IDXGISwapChain *sc;
    ID3D11Texture2D *bb, *staging; ID3D11RenderTargetView *rtv;
    D3D11_TEXTURE2D_DESC td; D3D11_MAPPED_SUBRESOURCE map;
    IDXGIDevice *dxgi_dev; IDXGIAdapter *adapter; DXGI_ADAPTER_DESC ad;
    LARGE_INTEGER f, t0, t1; BYTE *px; int i;
    HWND hwnd = CreateWindowA("static", "d3d11_present", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                              0, 0, 640, 480, NULL, NULL, NULL, NULL);

    sd.BufferCount = 2;
    sd.BufferDesc.Width = 640; sd.BufferDesc.Height = 480;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hwnd; sd.SampleDesc.Count = 1; sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

    CHECK(D3D11CreateDeviceAndSwapChain(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, NULL, 0,
                                        D3D11_SDK_VERSION, &sd, &sc, &dev, NULL, &ctx));
    CHECK(ID3D11Device_QueryInterface(dev, &IID_IDXGIDevice, (void **)&dxgi_dev));
    CHECK(IDXGIDevice_GetAdapter(dxgi_dev, &adapter));
    CHECK(IDXGIAdapter_GetDesc(adapter, &ad));
    printf("adapter: %ls %04x:%04x\n", ad.Description, ad.VendorId, ad.DeviceId);

    CHECK(IDXGISwapChain_GetBuffer(sc, 0, &IID_ID3D11Texture2D, (void **)&bb));
    CHECK(ID3D11Device_CreateRenderTargetView(dev, (ID3D11Resource *)bb, NULL, &rtv));

    QueryPerformanceFrequency(&f); QueryPerformanceCounter(&t0);
    for (i = 0; i < frames; i++)
    {
        ID3D11DeviceContext_ClearRenderTargetView(ctx, rtv, color);
        CHECK(IDXGISwapChain_Present(sc, 0, 0));
    }
    QueryPerformanceCounter(&t1);
    printf("fps: %.0f\n", frames * (double)f.QuadPart / (t1.QuadPart - t0.QuadPart));

    /* Read back after a final clear (flip-discard backbuffer contents are undefined after Present). */
    ID3D11DeviceContext_ClearRenderTargetView(ctx, rtv, color);
    ID3D11Texture2D_GetDesc(bb, &td);
    td.Usage = D3D11_USAGE_STAGING; td.BindFlags = 0; td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    CHECK(ID3D11Device_CreateTexture2D(dev, &td, NULL, &staging));
    ID3D11DeviceContext_CopyResource(ctx, (ID3D11Resource *)staging, (ID3D11Resource *)bb);
    CHECK(ID3D11DeviceContext_Map(ctx, (ID3D11Resource *)staging, 0, D3D11_MAP_READ, 0, &map));
    px = (BYTE *)map.pData + (td.Height / 2) * map.RowPitch + (td.Width / 2) * 4;
    printf("pixel: %u %u %u %u (expect 64 128 191 255)\n", px[0], px[1], px[2], px[3]);
    return !(abs(px[0] - 64) <= 1 && abs(px[1] - 128) <= 1 && abs(px[2] - 191) <= 1);
}
