/* Composition swapchain (FLIP_SEQUENTIAL, 2 buffers) painted via IDXGISurface1::GetDC:
 * which buffer holds the last presented frame. Win11 and Wine agree: after Present1, the
 * presented frame is in buffer BufferCount - 1 and buffer 0 is the next back buffer
 * (so wine-staging's "dcomp: Always use the front buffer" reads a stale frame).
 * Build: x86_64-w64-mingw32-gcc -O2 -o dxgi_comp_swapchain.exe dxgi_comp_swapchain.c -ld3d11 -ldxgi -lgdi32 -luuid */
#define COBJMACROS
#include <windows.h>
#include <stdio.h>
#include <initguid.h>
#include <d3d11.h>
#include <dxgi1_2.h>
static void dump(ID3D11Device *d, ID3D11DeviceContext *c, IDXGISwapChain1 *sc, const char *what)
{
    UINT i;
    for (i = 0; i < 2; i++)
    {
        ID3D11Texture2D *t, *st; D3D11_TEXTURE2D_DESC td; D3D11_MAPPED_SUBRESOURCE m; HRESULT hr;
        hr = IDXGISwapChain1_GetBuffer(sc, i, &IID_ID3D11Texture2D, (void **)&t);
        if (FAILED(hr)) { printf("%s buf%u: GetBuffer %#lx\n", what, i, hr); continue; }
        ID3D11Texture2D_GetDesc(t, &td); td.Usage = D3D11_USAGE_STAGING; td.BindFlags = 0; td.CPUAccessFlags = D3D11_CPU_ACCESS_READ; td.MiscFlags = 0;
        ID3D11Device_CreateTexture2D(d, &td, NULL, &st);
        ID3D11DeviceContext_CopyResource(c, (ID3D11Resource *)st, (ID3D11Resource *)t);
        ID3D11DeviceContext_Map(c, (ID3D11Resource *)st, 0, D3D11_MAP_READ, 0, &m);
        printf("%s buf%u (%p): %08lx\n", what, i, t, *(DWORD *)((BYTE *)m.pData + m.RowPitch * 10 + 40));
        ID3D11DeviceContext_Unmap(c, (ID3D11Resource *)st, 0);
        ID3D11Texture2D_Release(st); ID3D11Texture2D_Release(t);
    }
}
static void paint(IDXGISwapChain1 *sc, COLORREF col)
{
    IDXGISurface1 *s; HDC dc; RECT r = {0, 0, 64, 64}; HBRUSH b = CreateSolidBrush(col); HRESULT hr;
    IDXGISwapChain1_GetBuffer(sc, 0, &IID_IDXGISurface1, (void **)&s);
    hr = IDXGISurface1_GetDC(s, FALSE, &dc); if (FAILED(hr)) printf("GetDC %#lx\n", hr);
    FillRect(dc, &r, b);
    IDXGISurface1_ReleaseDC(s, NULL); IDXGISurface1_Release(s); DeleteObject(b);
}
int main(void)
{
    ID3D11Device *d; ID3D11DeviceContext *c; IDXGIDevice *dd; IDXGIAdapter *a; IDXGIFactory2 *f; IDXGISwapChain1 *sc;
    DXGI_SWAP_CHAIN_DESC1 desc = {0}; DXGI_PRESENT_PARAMETERS pp = {0}; HRESULT hr;
    hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, D3D11_CREATE_DEVICE_BGRA_SUPPORT, NULL, 0, D3D11_SDK_VERSION, &d, NULL, &c);
    if (FAILED(hr)) return 1;
    ID3D11Device_QueryInterface(d, &IID_IDXGIDevice, (void **)&dd); IDXGIDevice_GetAdapter(dd, &a); IDXGIAdapter_GetParent(a, &IID_IDXGIFactory2, (void **)&f);
    desc.Width = 64; desc.Height = 64; desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM; desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; desc.BufferCount = 2; desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    desc.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED; desc.Flags = DXGI_SWAP_CHAIN_FLAG_GDI_COMPATIBLE;
    hr = IDXGIFactory2_CreateSwapChainForComposition(f, (IUnknown *)d, &desc, NULL, &sc);
    printf("CreateSwapChainForComposition %#lx\n", hr); if (FAILED(hr)) return 1;
    dump(d, c, sc, "initial");
    paint(sc, RGB(255, 0, 0)); dump(d, c, sc, "painted red");
    hr = IDXGISwapChain1_Present1(sc, 0, 0, &pp); printf("Present1 %#lx\n", hr); dump(d, c, sc, "presented");
    paint(sc, RGB(0, 255, 0)); dump(d, c, sc, "painted green");
    hr = IDXGISwapChain1_Present1(sc, 0, 0, &pp); dump(d, c, sc, "presented");
    return 0;
}
