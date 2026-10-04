/* 165 / 150: D3D11 swapchain whose window is destroyed, then Present and Release, while the
 * process still holds a D3D11 buffer that detach.dll releases at process detach (as Inventor's
 * DLLs do). Windows: every call succeeds, exit 0.
 * Unfixed Wine with the Vulkan renderer: the present on the dead window fails to recreate the
 * Vulkan swapchain and leaves it half destroyed, a later present or the swapchain's destruction
 * faults inside a Vulkan Unix call on wined3d's command stream thread, winevulkan calls
 * ExitProcess(3) there, and the detach-time Release then runs on the command stream thread:
 * "Assertion failed: cs->thread_id != GetCurrentThreadId()" (wined3d_not_from_cs()).
 *
 * deadwin.exe [presents] [keep|detach]   presents after DestroyWindow (default 2). The buffer is released
 *   at process detach only if the process exits before main() is done; keep = never; detach = also on
 *   a normal exit.
 * Build: x86_64-w64-mingw32-gcc -O2 -o deadwin.exe deadwin.c -ld3d11 -ldxgi -luuid
 *        x86_64-w64-mingw32-gcc -O2 -shared -o detach.dll detach.c  (next to the exe) */
#define COBJMACROS
#include <windows.h>
#include <d3d11.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static ID3D11Buffer *detach_buffer;

static void pump(void)
{
    MSG msg;
    while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) DispatchMessageA(&msg);
}

int main(int argc, char **argv)
{
    static const float colour[4] = {0.25f, 0.5f, 0.75f, 1.0f};
    DXGI_SWAP_CHAIN_DESC desc = {{0}};
    D3D11_BUFFER_DESC buffer_desc = {0};
    ID3D11RenderTargetView *rtv;
    ID3D11DeviceContext *context;
    ID3D11Texture2D *backbuffer;
    IDXGISwapChain *swapchain;
    ID3D11Device *device;
    unsigned int i, presents = argc > 1 ? atoi(argv[1]) : 2;
    ULONG refcount;
    HRESULT hr;
    HWND hwnd;

    void (*release_at_detach)(IUnknown *) = (void *)GetProcAddress(LoadLibraryA("detach.dll"), "release_at_detach");

    setvbuf(stdout, NULL, _IONBF, 0);

    hwnd = CreateWindowA("static", "deadwin", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 100, 100, 320, 240, 0, 0, 0, NULL);
    pump();
    desc.BufferCount = 1;
    desc.BufferDesc.Width = 300;
    desc.BufferDesc.Height = 200;
    desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.OutputWindow = hwnd;
    desc.SampleDesc.Count = 1;
    desc.Windowed = TRUE;
    if (FAILED(hr = D3D11CreateDeviceAndSwapChain(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, NULL, 0,
            D3D11_SDK_VERSION, &desc, &swapchain, &device, NULL, &context)))
    {
        printf("D3D11CreateDeviceAndSwapChain failed, hr %#lx\n", hr);
        return 2;
    }
    buffer_desc.ByteWidth = 256;
    buffer_desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    hr = ID3D11Device_CreateBuffer(device, &buffer_desc, NULL, &detach_buffer);
    printf("CreateBuffer %#lx\n", hr);
    if (release_at_detach && !(argc > 2 && !strcmp(argv[2], "keep")))
        release_at_detach((IUnknown *)detach_buffer);
    else
        printf("not releasing the buffer at process detach\n");

    IDXGISwapChain_GetBuffer(swapchain, 0, &IID_ID3D11Texture2D, (void **)&backbuffer);
    ID3D11Device_CreateRenderTargetView(device, (ID3D11Resource *)backbuffer, NULL, &rtv);
    ID3D11DeviceContext_ClearRenderTargetView(context, rtv, colour);
    hr = IDXGISwapChain_Present(swapchain, 0, 0);
    printf("Present %#lx\n", hr);

    DestroyWindow(hwnd);
    pump();
    for (i = 0; i < presents; ++i)
    {
        ID3D11DeviceContext_ClearRenderTargetView(context, rtv, colour);
        hr = IDXGISwapChain_Present(swapchain, 0, 0);
        printf("Present %u on the swapchain of a destroyed window %#lx\n", i, hr);
    }
    ID3D11DeviceContext_Flush(context);
    Sleep(200);

    ID3D11RenderTargetView_Release(rtv);
    ID3D11Texture2D_Release(backbuffer);
    refcount = IDXGISwapChain_Release(swapchain);
    printf("swapchain released, refcount %lu\n", refcount);
    ID3D11DeviceContext_Release(context);
    refcount = ID3D11Device_Release(device);
    printf("device released, refcount %lu (the buffer holds it)\n", refcount);
    if (argc > 2 && !strcmp(argv[2], "detach"))
    {
        /* Windows: fine. Wine: the main thread waits forever for the command stream thread, which
         * ExitProcess() already killed. */
        printf("done, the buffer is released at process detach\n");
        return 0;
    }
    if (release_at_detach) release_at_detach(NULL);
    refcount = ID3D11Buffer_Release(detach_buffer);
    printf("done, buffer refcount %lu\n", refcount);
    return 0;
}
