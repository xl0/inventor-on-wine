/* What a composition swapchain answers to the window-related IDXGISwapChain1 calls (131), for a
 * D3D11 device and a D3D12 command queue: GetHwnd, GetCoreWindow, GetDesc().OutputWindow, GetContainingOutput,
 * Get/SetFullscreenState, ResizeTarget, ResizeBuffers (same size / 0x0), Present, creation with 0x0,
 * and the calling thread's windows around it.
 * Build: x86_64-w64-mingw32-gcc -O2 -o comp_probe.exe comp_probe.c -ld3d11 -ld3d12 -ldxgi -luuid */
#define COBJMACROS
#include <initguid.h>
#include <windows.h>
#include <d3d11.h>
#include <d3d12.h>
#include <dxgi1_4.h>
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

static void probe(const char *name, IDXGIFactory2 *factory, IUnknown *device)
{
    DXGI_SWAP_CHAIN_FULLSCREEN_DESC fs_desc;
    DXGI_SWAP_CHAIN_DESC1 desc = {0}, desc1;
    DXGI_MODE_DESC mode = {0};
    DXGI_SWAP_CHAIN_DESC old_desc;
    IDXGISwapChain1 *sc, *sc2;
    IDXGIOutput *output;
    IUnknown *unk;
    BOOL fullscreen;
    int before;
    HRESULT hr;
    HWND hwnd;

    desc.Width = 640; desc.Height = 480;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 2;
    desc.Scaling = DXGI_SCALING_STRETCH;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    desc.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;

    before = count();
    hr = IDXGIFactory2_CreateSwapChainForComposition(factory, device, &desc, NULL, &sc);
    printf("%s: create %#lx, thread windows %d -> %d\n", name, hr, before, count());
    if (FAILED(hr)) return;

    hwnd = (HWND)0xdeadbeef;
    hr = IDXGISwapChain1_GetHwnd(sc, &hwnd);
    printf("%s: GetHwnd %#lx hwnd %p\n", name, hr, hwnd);
    unk = (IUnknown *)0xdeadbeef;
    hr = IDXGISwapChain1_GetCoreWindow(sc, &IID_IUnknown, (void **)&unk);
    printf("%s: GetCoreWindow %#lx %p\n", name, hr, unk);
    memset(&old_desc, 0xcc, sizeof(old_desc));
    hr = IDXGISwapChain1_GetDesc(sc, &old_desc);
    printf("%s: GetDesc %#lx OutputWindow %p Windowed %d %ux%u\n", name, hr, old_desc.OutputWindow,
            old_desc.Windowed, old_desc.BufferDesc.Width, old_desc.BufferDesc.Height);
    memset(&desc1, 0xcc, sizeof(desc1));
    hr = IDXGISwapChain1_GetDesc1(sc, &desc1);
    printf("%s: GetDesc1 %#lx %ux%u scaling %u alpha %u flags %#x\n", name, hr, desc1.Width, desc1.Height,
            desc1.Scaling, desc1.AlphaMode, desc1.Flags);
    memset(&fs_desc, 0xcc, sizeof(fs_desc));
    hr = IDXGISwapChain1_GetFullscreenDesc(sc, &fs_desc);
    printf("%s: GetFullscreenDesc %#lx Windowed %d\n", name, hr, fs_desc.Windowed);
    output = (IDXGIOutput *)0xdeadbeef;
    hr = IDXGISwapChain1_GetContainingOutput(sc, &output);
    printf("%s: GetContainingOutput %#lx %p\n", name, hr, output);
    if (SUCCEEDED(hr) && output) IDXGIOutput_Release(output);
    fullscreen = 0xcc; output = (IDXGIOutput *)0xdeadbeef;
    hr = IDXGISwapChain1_GetFullscreenState(sc, &fullscreen, &output);
    printf("%s: GetFullscreenState %#lx fullscreen %d output %p\n", name, hr, fullscreen, output);
    if (SUCCEEDED(hr) && output && output != (IDXGIOutput *)0xdeadbeef) IDXGIOutput_Release(output);
    hr = IDXGISwapChain1_SetFullscreenState(sc, TRUE, NULL);
    printf("%s: SetFullscreenState(TRUE) %#lx\n", name, hr);
    hr = IDXGISwapChain1_SetFullscreenState(sc, FALSE, NULL);
    printf("%s: SetFullscreenState(FALSE) %#lx\n", name, hr);
    mode.Width = 800; mode.Height = 600; mode.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    hr = IDXGISwapChain1_ResizeTarget(sc, &mode);
    printf("%s: ResizeTarget %#lx\n", name, hr);
    hr = IDXGISwapChain1_Present(sc, 0, 0);
    printf("%s: Present %#lx\n", name, hr);
    hr = IDXGISwapChain1_ResizeBuffers(sc, 0, 320, 240, DXGI_FORMAT_UNKNOWN, 0);
    printf("%s: ResizeBuffers(320x240) %#lx\n", name, hr);
    hr = IDXGISwapChain1_ResizeBuffers(sc, 0, 0, 0, DXGI_FORMAT_UNKNOWN, 0);
    IDXGISwapChain1_GetDesc1(sc, &desc1);
    printf("%s: ResizeBuffers(0x0) %#lx -> %ux%u\n", name, hr, desc1.Width, desc1.Height);
    hr = IDXGISwapChain1_Present(sc, 0, 0);
    printf("%s: Present %#lx\n", name, hr);
    printf("%s: release -> %lu, thread windows %d\n", name, IDXGISwapChain1_Release(sc), count());

    desc.Width = desc.Height = 0;
    sc2 = (IDXGISwapChain1 *)0xdeadbeef;
    hr = IDXGIFactory2_CreateSwapChainForComposition(factory, device, &desc, NULL, &sc2);
    printf("%s: create 0x0 %#lx %p, thread windows %d\n", name, hr, sc2, count());
    if (SUCCEEDED(hr)) IDXGISwapChain1_Release(sc2);
    desc.Width = 640; desc.Height = 480;
    desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    hr = IDXGIFactory2_CreateSwapChainForComposition(factory, device, &desc, NULL, &sc2);
    printf("%s: create blt-model %#lx, thread windows %d\n", name, hr, count());
    if (SUCCEEDED(hr)) IDXGISwapChain1_Release(sc2);
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    desc.Scaling = DXGI_SCALING_NONE;
    hr = IDXGIFactory2_CreateSwapChainForComposition(factory, device, &desc, NULL, &sc2);
    printf("%s: create scaling none %#lx, thread windows %d\n", name, hr, count());
    if (SUCCEEDED(hr)) IDXGISwapChain1_Release(sc2);
    hr = IDXGIFactory2_CreateSwapChainForComposition(factory, device, NULL, NULL, &sc2);
    printf("%s: create NULL desc %#lx\n", name, hr);
    hr = IDXGIFactory2_CreateSwapChainForComposition(factory, NULL, &desc, NULL, &sc2);
    printf("%s: create NULL device %#lx\n", name, hr);
}

int main(void)
{
    D3D12_COMMAND_QUEUE_DESC queue_desc = {0};
    ID3D12CommandQueue *queue;
    IDXGIFactory2 *factory;
    ID3D12Device *device12;
    ID3D11Device *device;
    IDXGIAdapter *adapter;
    HRESULT hr;

    hr = CreateDXGIFactory1(&IID_IDXGIFactory2, (void **)&factory);
    if (FAILED(hr)) { printf("CreateDXGIFactory1 %#lx\n", hr); return 2; }

    hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, D3D11_CREATE_DEVICE_BGRA_SUPPORT, NULL, 0,
            D3D11_SDK_VERSION, &device, NULL, NULL);
    if (FAILED(hr)) hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_WARP, NULL, D3D11_CREATE_DEVICE_BGRA_SUPPORT, NULL, 0,
            D3D11_SDK_VERSION, &device, NULL, NULL);
    if (FAILED(hr)) printf("D3D11CreateDevice %#lx\n", hr);
    else
    {
        probe("d3d11", factory, (IUnknown *)device);
        ID3D11Device_Release(device);
    }

    hr = D3D12CreateDevice(NULL, D3D_FEATURE_LEVEL_11_0, &IID_ID3D12Device, (void **)&device12);
    if (FAILED(hr) && SUCCEEDED(IDXGIFactory4_EnumWarpAdapter((IDXGIFactory4 *)factory, &IID_IDXGIAdapter, (void **)&adapter)))
    {
        hr = D3D12CreateDevice((IUnknown *)adapter, D3D_FEATURE_LEVEL_11_0, &IID_ID3D12Device, (void **)&device12);
        IDXGIAdapter_Release(adapter);
    }
    if (FAILED(hr)) { printf("D3D12CreateDevice %#lx\n", hr); return 0; }
    queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    hr = ID3D12Device_CreateCommandQueue(device12, &queue_desc, &IID_ID3D12CommandQueue, (void **)&queue);
    if (FAILED(hr)) { printf("CreateCommandQueue %#lx\n", hr); return 0; }
    probe("d3d12", factory, (IUnknown *)queue);
    ID3D12CommandQueue_Release(queue);
    ID3D12Device_Release(device12);
    return 0;
}
