/* Composition swapchain lifetime across threads (131): top-level windows of the process (hidden ones too)
 * around: create on a thread that exits, use + release on the main thread; create on the main thread,
 * release on another; a back buffer held past the swapchain's last reference, released on another thread.
 * Win11: no windows at any point, every call S_OK, exit 0.
 * Build: x86_64-w64-mingw32-gcc -O2 -o comp_threads.exe comp_threads.c -ld3d11 -ldxgi -luuid */
#define COBJMACROS
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <stdio.h>

static IDXGIFactory2 *factory;
static ID3D11Device *device;
static int failures;

static BOOL CALLBACK count_cb(HWND hwnd, LPARAM lp)
{
    DWORD pid;
    char cls[64];
    GetWindowThreadProcessId(hwnd, &pid);
    GetClassNameA(hwnd, cls, sizeof(cls));
    if (pid == GetCurrentProcessId() && lstrcmpiA(cls, "IME") && lstrcmpiA(cls, "MSCTFIME UI")) ++*(int *)lp;
    return TRUE;
}

static int count(void)
{
    int n = 0;
    EnumWindows(count_cb, (LPARAM)&n);
    return n;
}

static void check(const char *what, HRESULT hr)
{
    if (FAILED(hr)) { printf("FAIL %s %#lx\n", what, hr); failures++; }
}

static DWORD WINAPI create_proc(void *arg)
{
    DXGI_SWAP_CHAIN_DESC1 desc = {0};

    desc.Width = 640; desc.Height = 480;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 2;
    desc.Scaling = DXGI_SCALING_STRETCH;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    desc.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
    check("create", IDXGIFactory2_CreateSwapChainForComposition(factory, (IUnknown *)device, &desc, NULL, arg));
    return 0;
}

static DWORD WINAPI release_proc(void *arg)
{
    printf("  release on thread %04lx -> %lu\n", GetCurrentThreadId(), IUnknown_Release((IUnknown *)arg));
    return 0;
}

static void on_thread(LPTHREAD_START_ROUTINE proc, void *arg)
{
    HANDLE thread = CreateThread(NULL, 0, proc, arg, 0, NULL);
    WaitForSingleObject(thread, INFINITE);
    CloseHandle(thread);
}

static void draw_present(IDXGISwapChain1 *sc)
{
    static const float red[] = {1.0f, 0.0f, 0.0f, 1.0f};
    ID3D11RenderTargetView *rtv;
    ID3D11DeviceContext *context;
    ID3D11Texture2D *tex;

    ID3D11Device_GetImmediateContext(device, &context);
    check("GetBuffer", IDXGISwapChain1_GetBuffer(sc, 0, &IID_ID3D11Texture2D, (void **)&tex));
    check("CreateRenderTargetView", ID3D11Device_CreateRenderTargetView(device, (ID3D11Resource *)tex, NULL, &rtv));
    ID3D11DeviceContext_ClearRenderTargetView(context, rtv, red);
    ID3D11RenderTargetView_Release(rtv);
    ID3D11Texture2D_Release(tex);
    check("Present", IDXGISwapChain1_Present(sc, 0, 0));
    ID3D11DeviceContext_Release(context);
}

int main(void)
{
    IDXGIDevice *dxgi_device;
    IDXGIAdapter *adapter;
    ID3D11Texture2D *tex;
    IDXGISwapChain1 *sc;
    int before, n, i;
    HRESULT hr;

    hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, D3D11_CREATE_DEVICE_BGRA_SUPPORT, NULL, 0,
            D3D11_SDK_VERSION, &device, NULL, NULL);
    if (FAILED(hr)) hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_WARP, NULL, D3D11_CREATE_DEVICE_BGRA_SUPPORT, NULL, 0,
            D3D11_SDK_VERSION, &device, NULL, NULL);
    if (FAILED(hr)) { printf("D3D11CreateDevice %#lx\n", hr); return 2; }
    ID3D11Device_QueryInterface(device, &IID_IDXGIDevice, (void **)&dxgi_device);
    IDXGIDevice_GetAdapter(dxgi_device, &adapter);
    IDXGIAdapter_GetParent(adapter, &IID_IDXGIFactory2, (void **)&factory);
    before = count();
    printf("process windows before: %d (main thread %04lx)\n", before, GetCurrentThreadId());

    for (i = 0; i < 5; i++)
    {
        sc = NULL;
        on_thread(create_proc, &sc);
        if (!sc) return 2;
        draw_present(sc);
        draw_present(sc);
        IDXGISwapChain1_Release(sc);
    }
    n = count();
    printf("created on threads that exited, used + released on main: %d\n", n);
    if (n != before) failures++;

    for (i = 0; i < 5; i++)
    {
        sc = NULL;
        create_proc(&sc);
        draw_present(sc);
        on_thread(release_proc, sc);
    }
    n = count();
    printf("created on main, released on other threads: %d\n", n);
    if (n != before) failures++;

    for (i = 0; i < 5; i++)
    {
        sc = NULL;
        create_proc(&sc);
        draw_present(sc);
        check("GetBuffer", IDXGISwapChain1_GetBuffer(sc, 1, &IID_ID3D11Texture2D, (void **)&tex));
        printf("  swapchain release with a buffer held -> %lu\n", IDXGISwapChain1_Release(sc));
        on_thread(release_proc, tex);
    }
    n = count();
    printf("buffer held past the swapchain, released on other threads: %d\n", n);
    if (n != before) failures++;

    IDXGIFactory2_Release(factory);
    IDXGIAdapter_Release(adapter);
    IDXGIDevice_Release(dxgi_device);
    printf("device release -> %lu, failures %d\n", ID3D11Device_Release(device), failures);
    return !!failures;
}
