/* Windows of the calling thread around IDXGIFactory2::CreateSwapChainForComposition (131):
 * Win11 creates none; Wine's dxgi hack backs each swapchain with a hidden "static" popup.
 * Build: x86_64-w64-mingw32-gcc -O2 -o comp_windows.exe comp_windows.c -ld3d11 -ldxgi -luuid */
#define COBJMACROS
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <stdio.h>

static BOOL CALLBACK count_cb(HWND hwnd, LPARAM lp)
{
    char cls[64];
    GetClassNameA(hwnd, cls, sizeof(cls));
    if (lstrcmpiA(cls, "IME") && lstrcmpiA(cls, "MSCTFIME UI")) ++*(int *)lp;
    return TRUE;
}

static int count(void)
{
    int n = 0;
    EnumThreadWindows(GetCurrentThreadId(), count_cb, (LPARAM)&n);
    return n;
}

int main(void)
{
    DXGI_SWAP_CHAIN_DESC1 desc = {0};
    IDXGISwapChain1 *sc[10];
    IDXGIFactory2 *factory;
    IDXGIAdapter *adapter;
    IDXGIDevice *dxgi_device;
    ID3D11Device *device;
    int i, before, created, released;
    HRESULT hr;
    HWND hwnd;

    hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, D3D11_CREATE_DEVICE_BGRA_SUPPORT, NULL, 0,
            D3D11_SDK_VERSION, &device, NULL, NULL);
    if (FAILED(hr)) hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_WARP, NULL, D3D11_CREATE_DEVICE_BGRA_SUPPORT, NULL, 0,
            D3D11_SDK_VERSION, &device, NULL, NULL);
    if (FAILED(hr)) { printf("D3D11CreateDevice %#lx\n", hr); return 2; }
    ID3D11Device_QueryInterface(device, &IID_IDXGIDevice, (void **)&dxgi_device);
    IDXGIDevice_GetAdapter(dxgi_device, &adapter);
    IDXGIAdapter_GetParent(adapter, &IID_IDXGIFactory2, (void **)&factory);

    desc.Width = 640; desc.Height = 480;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 2;
    desc.Scaling = DXGI_SCALING_STRETCH;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    desc.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;

    before = count();
    for (i = 0; i < 10; i++)
    {
        hr = IDXGIFactory2_CreateSwapChainForComposition(factory, (IUnknown *)device, &desc, NULL, &sc[i]);
        if (FAILED(hr)) { printf("CreateSwapChainForComposition %#lx\n", hr); return 2; }
    }
    hwnd = (HWND)0xdeadbeef;
    hr = IDXGISwapChain1_GetHwnd(sc[0], &hwnd);
    created = count();
    for (i = 0; i < 10; i++) IDXGISwapChain1_Release(sc[i]);
    released = count();
    printf("thread windows: before %d, 10 composition swapchains %d, released %d; GetHwnd %#lx hwnd %p\n",
            before, created, released, hr, hwnd);
    return released != before;
}
