/* Review probe for fix/131: what composition swapchains answer to calls the dxgi test doesn't cover.
 * Build: x86_64-w64-mingw32-gcc -O2 -o probe1.exe probe1.c -ld3d11 -ld3d12 -ldxgi -luuid */
#define COBJMACROS
#include <initguid.h>
#include <windows.h>
#include <d3d11.h>
#include <d3d12.h>
#include <dxgi1_5.h>
#include <stdio.h>

static IDXGIFactory2 *factory;

static void base_desc(DXGI_SWAP_CHAIN_DESC1 *desc)
{
    memset(desc, 0, sizeof(*desc));
    desc->Width = 640; desc->Height = 480;
    desc->Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc->SampleDesc.Count = 1;
    desc->BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc->BufferCount = 2;
    desc->Scaling = DXGI_SCALING_STRETCH;
    desc->SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    desc->AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
}

static void try_create(const char *name, const char *what, IUnknown *device, const DXGI_SWAP_CHAIN_DESC1 *desc,
        IDXGIOutput *output)
{
    IDXGISwapChain1 *sc = (IDXGISwapChain1 *)0xdeadbeef;
    DXGI_SWAP_CHAIN_DESC1 d;
    HRESULT hr;

    hr = IDXGIFactory2_CreateSwapChainForComposition(factory, device, desc, output, &sc);
    printf("%s: create %-28s %#lx", name, what, hr);
    if (SUCCEEDED(hr))
    {
        IDXGISwapChain1_GetDesc1(sc, &d);
        printf("  -> %ux%u fmt %u bufs %u scaling %u effect %u alpha %u flags %#x usage %#x", d.Width, d.Height, d.Format,
                d.BufferCount, d.Scaling, d.SwapEffect, d.AlphaMode, d.Flags, d.BufferUsage);
        IDXGISwapChain1_Release(sc);
    }
    else printf("  sc %p", sc);
    printf("\n");
}

static void creates(const char *name, IUnknown *device)
{
    DXGI_SWAP_CHAIN_DESC1 desc;
    IDXGIAdapter *adapter;
    IDXGIOutput *output = NULL;

    base_desc(&desc); try_create(name, "base", device, &desc, NULL);
    base_desc(&desc); desc.Width = 0; try_create(name, "0x480", device, &desc, NULL);
    base_desc(&desc); desc.Height = 0; try_create(name, "640x0", device, &desc, NULL);
    base_desc(&desc); desc.Scaling = DXGI_SCALING_ASPECT_RATIO_STRETCH; try_create(name, "aspect stretch", device, &desc, NULL);
    base_desc(&desc); desc.Scaling = 3; try_create(name, "scaling 3", device, &desc, NULL);
    base_desc(&desc); desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD; try_create(name, "flip discard", device, &desc, NULL);
    base_desc(&desc); desc.SwapEffect = DXGI_SWAP_EFFECT_SEQUENTIAL; try_create(name, "blt sequential", device, &desc, NULL);
    base_desc(&desc); desc.BufferCount = 1; try_create(name, "1 buffer", device, &desc, NULL);
    base_desc(&desc); desc.BufferCount = 16; try_create(name, "16 buffers", device, &desc, NULL);
    base_desc(&desc); desc.BufferCount = 17; try_create(name, "17 buffers", device, &desc, NULL);
    base_desc(&desc); desc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED; try_create(name, "alpha unspecified", device, &desc, NULL);
    base_desc(&desc); desc.AlphaMode = DXGI_ALPHA_MODE_STRAIGHT; try_create(name, "alpha straight", device, &desc, NULL);
    base_desc(&desc); desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE; try_create(name, "alpha ignore", device, &desc, NULL);
    base_desc(&desc); desc.SampleDesc.Count = 4; try_create(name, "4 samples", device, &desc, NULL);
    base_desc(&desc); desc.Flags = DXGI_SWAP_CHAIN_FLAG_GDI_COMPATIBLE; try_create(name, "gdi compatible", device, &desc, NULL);
    base_desc(&desc); desc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH; try_create(name, "allow mode switch", device, &desc, NULL);
    base_desc(&desc); desc.Flags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT; try_create(name, "waitable", device, &desc, NULL);
    base_desc(&desc); desc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING; try_create(name, "allow tearing", device, &desc, NULL);
    base_desc(&desc); desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; try_create(name, "rgba8", device, &desc, NULL);
    base_desc(&desc); desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB; try_create(name, "rgba8 srgb", device, &desc, NULL);
    base_desc(&desc); desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT; try_create(name, "rgba16f", device, &desc, NULL);
    base_desc(&desc); desc.Format = DXGI_FORMAT_R10G10B10A2_UNORM; try_create(name, "rgb10a2", device, &desc, NULL);
    base_desc(&desc); desc.Format = DXGI_FORMAT_UNKNOWN; try_create(name, "format unknown", device, &desc, NULL);
    base_desc(&desc); desc.Format = DXGI_FORMAT_NV12; try_create(name, "nv12", device, &desc, NULL);
    base_desc(&desc); desc.Format = DXGI_FORMAT_YUY2; try_create(name, "yuy2", device, &desc, NULL);
    base_desc(&desc); desc.Stereo = TRUE; try_create(name, "stereo", device, &desc, NULL);
    base_desc(&desc); desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT | DXGI_USAGE_SHADER_INPUT; try_create(name, "shader input", device, &desc, NULL);
    base_desc(&desc); desc.BufferUsage = 0; try_create(name, "usage 0", device, &desc, NULL);
    base_desc(&desc); desc.Width = 16384; desc.Height = 16384; try_create(name, "16384x16384", device, &desc, NULL);
    base_desc(&desc); desc.Width = 1; desc.Height = 1; try_create(name, "1x1", device, &desc, NULL);

    if (SUCCEEDED(IDXGIFactory2_EnumAdapters(factory, 0, &adapter)))
    {
        IDXGIAdapter_EnumOutputs(adapter, 0, &output);
        IDXGIAdapter_Release(adapter);
    }
    base_desc(&desc); try_create(name, output ? "restrict output" : "restrict output (none!)", device, &desc, output);
    if (output) IDXGIOutput_Release(output);
}

static void calls(const char *name, IUnknown *device)
{
    DXGI_SWAP_CHAIN_FULLSCREEN_DESC fs_desc;
    DXGI_PRESENT_PARAMETERS params = {0};
    DXGI_MATRIX_3X2_F matrix = {0};
    DXGI_FRAME_STATISTICS stats;
    DXGI_SWAP_CHAIN_DESC1 desc;
    DXGI_MODE_ROTATION rotation;
    DXGI_MODE_DESC mode = {0};
    IDXGISwapChain2 *sc2;
    IDXGISwapChain1 *sc;
    IDXGIOutput *output;
    RECT rect, scroll;
    IUnknown *unk, *buf;
    POINT offset;
    DXGI_RGBA color;
    HANDLE handle;
    UINT w, h, count;
    HRESULT hr;
    HWND fg;

    base_desc(&desc);
    hr = IDXGIFactory2_CreateSwapChainForComposition(factory, device, &desc, NULL, &sc);
    if (FAILED(hr)) { printf("%s: create %#lx\n", name, hr); return; }

    memset(&fs_desc, 0xcc, sizeof(fs_desc));
    hr = IDXGISwapChain1_GetFullscreenDesc(sc, &fs_desc);
    printf("%s: GetFullscreenDesc %#lx Windowed %#x\n", name, hr, fs_desc.Windowed);
    fg = GetForegroundWindow();
    GetWindowRect(GetDesktopWindow(), &rect);
    mode.Width = 800; mode.Height = 600; mode.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    hr = IDXGISwapChain1_ResizeTarget(sc, &mode);
    printf("%s: ResizeTarget %#lx (foreground %s)\n", name, hr, fg == GetForegroundWindow() ? "same" : "CHANGED");
    hr = IDXGISwapChain1_ResizeTarget(sc, NULL);
    printf("%s: ResizeTarget(NULL) %#lx\n", name, hr);
    unk = (IUnknown *)0xdeadbeef;
    hr = IDXGISwapChain1_GetCoreWindow(sc, &IID_IUnknown, (void **)&unk);
    printf("%s: GetCoreWindow %#lx %p\n", name, hr, unk);
    output = (IDXGIOutput *)0xdeadbeef;
    hr = IDXGISwapChain1_GetRestrictToOutput(sc, &output);
    printf("%s: GetRestrictToOutput %#lx %p\n", name, hr, output);
    printf("%s: IsTemporaryMonoSupported %d\n", name, IDXGISwapChain1_IsTemporaryMonoSupported(sc));
    memset(&color, 0, sizeof(color));
    hr = IDXGISwapChain1_SetBackgroundColor(sc, &color);
    printf("%s: SetBackgroundColor %#lx\n", name, hr);
    hr = IDXGISwapChain1_GetBackgroundColor(sc, &color);
    printf("%s: GetBackgroundColor %#lx\n", name, hr);
    hr = IDXGISwapChain1_SetRotation(sc, DXGI_MODE_ROTATION_ROTATE90);
    printf("%s: SetRotation %#lx\n", name, hr);
    rotation = 0xcc;
    hr = IDXGISwapChain1_GetRotation(sc, &rotation);
    printf("%s: GetRotation %#lx %u\n", name, hr, rotation);
    hr = IDXGISwapChain1_GetFrameStatistics(sc, &stats);
    printf("%s: GetFrameStatistics %#lx\n", name, hr);

    if (SUCCEEDED(hr = IDXGISwapChain1_QueryInterface(sc, &IID_IDXGISwapChain2, (void **)&sc2)))
    {
        hr = IDXGISwapChain2_SetSourceSize(sc2, 320, 240);
        printf("%s: SetSourceSize(320x240) %#lx\n", name, hr);
        w = h = 0xcccc;
        hr = IDXGISwapChain2_GetSourceSize(sc2, &w, &h);
        printf("%s: GetSourceSize %#lx %ux%u\n", name, hr, w, h);
        hr = IDXGISwapChain2_SetSourceSize(sc2, 641, 480);
        printf("%s: SetSourceSize(641x480) %#lx\n", name, hr);
        hr = IDXGISwapChain2_SetSourceSize(sc2, 640, 480);
        matrix._11 = matrix._22 = 2.0f;
        hr = IDXGISwapChain2_SetMatrixTransform(sc2, &matrix);
        printf("%s: SetMatrixTransform %#lx\n", name, hr);
        memset(&matrix, 0, sizeof(matrix));
        hr = IDXGISwapChain2_GetMatrixTransform(sc2, &matrix);
        printf("%s: GetMatrixTransform %#lx %.1f %.1f\n", name, hr, matrix._11, matrix._22);
        count = 0xcc;
        hr = IDXGISwapChain2_GetMaximumFrameLatency(sc2, &count);
        printf("%s: GetMaximumFrameLatency %#lx %u\n", name, hr, count);
        hr = IDXGISwapChain2_SetMaximumFrameLatency(sc2, 2);
        printf("%s: SetMaximumFrameLatency %#lx\n", name, hr);
        handle = IDXGISwapChain2_GetFrameLatencyWaitableObject(sc2);
        printf("%s: GetFrameLatencyWaitableObject %p\n", name, handle);
        IDXGISwapChain2_Release(sc2);
    }
    else printf("%s: no IDXGISwapChain2 %#lx\n", name, hr);

    /* Presents. */
    hr = IDXGISwapChain1_Present(sc, 0, DXGI_PRESENT_TEST);
    printf("%s: Present(TEST) %#lx\n", name, hr);
    SetRect(&rect, 0, 0, 16, 16);
    params.DirtyRectsCount = 1; params.pDirtyRects = &rect;
    hr = IDXGISwapChain1_Present1(sc, 0, 0, &params);
    printf("%s: first Present1(dirty rect) %#lx\n", name, hr);
    hr = IDXGISwapChain1_Present(sc, 5, 0);
    printf("%s: Present(interval 5) %#lx\n", name, hr);
    hr = IDXGISwapChain1_Present(sc, 0, 0);
    printf("%s: Present %#lx\n", name, hr);
    hr = IDXGISwapChain1_Present1(sc, 0, 0, &params);
    printf("%s: Present1(dirty rect) %#lx\n", name, hr);
    SetRect(&scroll, 0, 0, 640, 400); offset.x = 0; offset.y = 16;
    params.pScrollRect = &scroll; params.pScrollOffset = &offset;
    hr = IDXGISwapChain1_Present1(sc, 0, 0, &params);
    printf("%s: Present1(scroll) %#lx\n", name, hr);
    hr = IDXGISwapChain1_Present(sc, 0, DXGI_PRESENT_DO_NOT_SEQUENCE);
    printf("%s: Present(DO_NOT_SEQUENCE) %#lx\n", name, hr);
    hr = IDXGISwapChain1_Present(sc, 0, DXGI_PRESENT_RESTART);
    printf("%s: Present(RESTART) %#lx\n", name, hr);
    hr = IDXGISwapChain1_Present(sc, 0, DXGI_PRESENT_DO_NOT_WAIT);
    printf("%s: Present(DO_NOT_WAIT) %#lx\n", name, hr);
    hr = IDXGISwapChain1_Present(sc, 0, DXGI_PRESENT_ALLOW_TEARING);
    printf("%s: Present(ALLOW_TEARING) %#lx\n", name, hr);
    count = 0xcc;
    hr = IDXGISwapChain1_GetLastPresentCount(sc, &count);
    printf("%s: GetLastPresentCount %#lx %u\n", name, hr, count);

    /* Buffers. */
    hr = IDXGISwapChain1_GetBuffer(sc, 2, &IID_IUnknown, (void **)&buf);
    printf("%s: GetBuffer(2 of 2) %#lx\n", name, hr);
    if (SUCCEEDED(hr)) IUnknown_Release(buf);
    hr = IDXGISwapChain1_GetBuffer(sc, 1, &IID_IUnknown, (void **)&buf);
    printf("%s: GetBuffer(1) %#lx\n", name, hr);
    if (SUCCEEDED(hr))
    {
        hr = IDXGISwapChain1_ResizeBuffers(sc, 0, 320, 240, DXGI_FORMAT_UNKNOWN, 0);
        printf("%s: ResizeBuffers with a buffer held %#lx\n", name, hr);
        IUnknown_Release(buf);
    }
    hr = IDXGISwapChain1_ResizeBuffers(sc, 0, 320, 0, DXGI_FORMAT_UNKNOWN, 0);
    IDXGISwapChain1_GetDesc1(sc, &desc);
    printf("%s: ResizeBuffers(320x0) %#lx -> %ux%u\n", name, hr, desc.Width, desc.Height);
    hr = IDXGISwapChain1_ResizeBuffers(sc, 3, 320, 240, DXGI_FORMAT_R8G8B8A8_UNORM, 0);
    IDXGISwapChain1_GetDesc1(sc, &desc);
    printf("%s: ResizeBuffers(3, 320x240, rgba8) %#lx -> %ux%u bufs %u fmt %u\n", name, hr, desc.Width, desc.Height,
            desc.BufferCount, desc.Format);
    hr = IDXGISwapChain1_ResizeBuffers(sc, 0, 0, 0, DXGI_FORMAT_UNKNOWN, 0);
    IDXGISwapChain1_GetDesc1(sc, &desc);
    printf("%s: ResizeBuffers(0x0) %#lx -> %ux%u\n", name, hr, desc.Width, desc.Height);
    hr = IDXGISwapChain1_Present(sc, 0, 0);
    printf("%s: Present after resizes %#lx\n", name, hr);
    printf("%s: release -> %lu\n", name, IDXGISwapChain1_Release(sc));
}

/* Presents per second with sync interval 1 and 0; the swapchain isn't shown anywhere. */
static void pacing(const char *name, IUnknown *device, UINT flags)
{
    DXGI_SWAP_CHAIN_DESC1 desc;
    IDXGISwapChain1 *sc;
    unsigned int n, interval;
    DWORD start;
    HRESULT hr;

    base_desc(&desc);
    desc.Flags = flags;
    hr = IDXGIFactory2_CreateSwapChainForComposition(factory, device, &desc, NULL, &sc);
    if (FAILED(hr)) { printf("%s: create %#lx\n", name, hr); return; }
    for (interval = 1; interval != ~0u; --interval)
    {
        start = GetTickCount();
        for (n = 0; GetTickCount() - start < 2000; ++n)
        {
            if (FAILED(hr = IDXGISwapChain1_Present(sc, interval, 0))) { printf("%s: Present %#lx\n", name, hr); break; }
        }
        printf("%s: flags %#x interval %u: %u presents in %lu ms\n", name, flags, interval, n, GetTickCount() - start);
    }
    IDXGISwapChain1_Release(sc);
}

int main(int argc, char **argv)
{
    D3D12_COMMAND_QUEUE_DESC queue_desc = {0};
    ID3D12CommandQueue *queue;
    ID3D12Device *device12;
    ID3D11Device *device;
    IDXGIAdapter *adapter;
    DXGI_ADAPTER_DESC adesc;
    IDXGIDevice *dxgi_device;
    HRESULT hr;
    BOOL pace = argc > 1 && !strcmp(argv[1], "pace");

    setvbuf(stdout, NULL, _IONBF, 0);
    hr = CreateDXGIFactory1(&IID_IDXGIFactory2, (void **)&factory);
    if (FAILED(hr)) { printf("CreateDXGIFactory1 %#lx\n", hr); return 2; }

    hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, D3D11_CREATE_DEVICE_BGRA_SUPPORT, NULL, 0,
            D3D11_SDK_VERSION, &device, NULL, NULL);
    if (FAILED(hr)) hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_WARP, NULL, D3D11_CREATE_DEVICE_BGRA_SUPPORT, NULL, 0,
            D3D11_SDK_VERSION, &device, NULL, NULL);
    if (FAILED(hr)) printf("D3D11CreateDevice %#lx\n", hr);
    else
    {
        ID3D11Device_QueryInterface(device, &IID_IDXGIDevice, (void **)&dxgi_device);
        IDXGIDevice_GetAdapter(dxgi_device, &adapter);
        IDXGIAdapter_GetDesc(adapter, &adesc);
        printf("d3d11 adapter: %ls\n", adesc.Description);
        IDXGIAdapter_Release(adapter);
        IDXGIDevice_Release(dxgi_device);
        if (pace)
        {
            pacing("d3d11", (IUnknown *)device, 0);
            pacing("d3d11", (IUnknown *)device, DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT);
        }
        else
        {
            creates("d3d11", (IUnknown *)device);
            calls("d3d11", (IUnknown *)device);
        }
        ID3D11Device_Release(device);
    }

    if (argc > 1 && !strcmp(argv[1], "no12")) return 0;
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
    if (pace)
    {
        pacing("d3d12", (IUnknown *)queue, 0);
        pacing("d3d12", (IUnknown *)queue, DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT);
    }
    else
    {
        creates("d3d12", (IUnknown *)queue);
        calls("d3d12", (IUnknown *)queue);
    }
    ID3D12CommandQueue_Release(queue);
    ID3D12Device_Release(device12);
    return 0;
}
