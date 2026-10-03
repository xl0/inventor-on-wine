/* Review probe for fix/131: buffer contents of composition swapchains across presents (2..4 buffers, both flip
 * effects), mixed with a window swapchain on the same device, window destruction, resize, a reader thread.
 * Exit code = number of mismatches.
 * Build: x86_64-w64-mingw32-gcc -O2 -o probe2.exe probe2.c -ld3d11 -ldxgi -luuid -lgdi32 */
#define COBJMACROS
#include <initguid.h>
#include <windows.h>
#include <d3d11_4.h>
#include <dxgi1_5.h>
#include <stdio.h>
#include <stdlib.h>
#include <d3dcompiler.h>

static IDXGIFactory2 *factory;
static ID3D11Device *device;
static ID3D11DeviceContext *context;
static int failures;

static DWORD colour(unsigned int k) /* BGRA, as read from a B8G8R8A8 map */
{
    return 0xff000000 | ((k * 37 + 11) & 0xff) << 16 | ((k * 91 + 5) & 0xff) << 8 | ((k * 53 + 3) & 0xff);
}

static BOOL use_draw;
static ID3D11VertexShader *vs;
static ID3D11PixelShader *ps;
static ID3D11Buffer *cb;

static void init_draw(void)
{
    static const char vs_code[] = "float4 main(uint id : SV_VertexID) : SV_Position\n"
            "{ return float4((id == 1) ? 3.0 : -1.0, (id == 2) ? -3.0 : 1.0, 0.0, 1.0); }";
    static const char ps_code[] = "float4 c; float4 main() : SV_Target { return c; }";
    D3D11_BUFFER_DESC desc = {16, D3D11_USAGE_DEFAULT, D3D11_BIND_CONSTANT_BUFFER};
    ID3DBlob *blob;

    if (FAILED(D3DCompile(vs_code, sizeof(vs_code) - 1, "vs", NULL, NULL, "main", "vs_4_0", 0, 0, &blob, NULL))
            || FAILED(ID3D11Device_CreateVertexShader(device, ID3D10Blob_GetBufferPointer(blob),
            ID3D10Blob_GetBufferSize(blob), NULL, &vs)))
    { printf("vs failed\n"); ++failures; return; }
    if (FAILED(D3DCompile(ps_code, sizeof(ps_code) - 1, "ps", NULL, NULL, "main", "ps_4_0", 0, 0, &blob, NULL))
            || FAILED(ID3D11Device_CreatePixelShader(device, ID3D10Blob_GetBufferPointer(blob),
            ID3D10Blob_GetBufferSize(blob), NULL, &ps)))
    { printf("ps failed\n"); ++failures; return; }
    ID3D11Device_CreateBuffer(device, &desc, NULL, &cb);
    use_draw = TRUE;
}

static void clear(ID3D11Texture2D *tex, DWORD c)
{
    float f[4] = {((c >> 16) & 0xff) / 255.0f, ((c >> 8) & 0xff) / 255.0f, (c & 0xff) / 255.0f, 1.0f};
    ID3D11RenderTargetView *rtv;

    if (use_draw)
    {
        D3D11_TEXTURE2D_DESC desc;
        D3D11_VIEWPORT vp = {0};

        /* A real draw, with the render target left bound across Present. */
        ID3D11Texture2D_GetDesc(tex, &desc);
        vp.Width = desc.Width; vp.Height = desc.Height; vp.MaxDepth = 1.0f;
        if (FAILED(ID3D11Device_CreateRenderTargetView(device, (ID3D11Resource *)tex, NULL, &rtv)))
        { printf("CreateRenderTargetView failed\n"); ++failures; return; }
        ID3D11DeviceContext_UpdateSubresource(context, (ID3D11Resource *)cb, 0, NULL, f, 0, 0);
        ID3D11DeviceContext_OMSetRenderTargets(context, 1, &rtv, NULL);
        ID3D11DeviceContext_RSSetViewports(context, 1, &vp);
        ID3D11DeviceContext_IASetPrimitiveTopology(context, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ID3D11DeviceContext_VSSetShader(context, vs, NULL, 0);
        ID3D11DeviceContext_PSSetShader(context, ps, NULL, 0);
        ID3D11DeviceContext_PSSetConstantBuffers(context, 0, 1, &cb);
        ID3D11DeviceContext_Draw(context, 3, 0);
        ID3D11RenderTargetView_Release(rtv);
        return;
    }

    if (FAILED(ID3D11Device_CreateRenderTargetView(device, (ID3D11Resource *)tex, NULL, &rtv)))
    {
        printf("CreateRenderTargetView failed\n"); ++failures; return;
    }
    ID3D11DeviceContext_ClearRenderTargetView(context, rtv, f);
    ID3D11RenderTargetView_Release(rtv);
}

static DWORD read_pixel(ID3D11Texture2D *tex, unsigned int x, unsigned int y)
{
    D3D11_MAPPED_SUBRESOURCE map;
    D3D11_TEXTURE2D_DESC desc;
    ID3D11Texture2D *staging;
    DWORD ret = 0xdeadbeef;

    ID3D11Texture2D_GetDesc(tex, &desc);
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    desc.MiscFlags = 0;
    if (FAILED(ID3D11Device_CreateTexture2D(device, &desc, NULL, &staging))) return ret;
    ID3D11DeviceContext_CopyResource(context, (ID3D11Resource *)staging, (ID3D11Resource *)tex);
    if (SUCCEEDED(ID3D11DeviceContext_Map(context, (ID3D11Resource *)staging, 0, D3D11_MAP_READ, 0, &map)))
    {
        ret = *(DWORD *)((BYTE *)map.pData + y * map.RowPitch + x * 4);
        ID3D11DeviceContext_Unmap(context, (ID3D11Resource *)staging, 0);
    }
    ID3D11Texture2D_Release(staging);
    return ret | 0xff000000;
}

static IDXGISwapChain1 *create_comp(unsigned int w, unsigned int h, unsigned int count, DXGI_SWAP_EFFECT effect, UINT flags)
{
    DXGI_SWAP_CHAIN_DESC1 desc = {0};
    IDXGISwapChain1 *sc;
    HRESULT hr;

    desc.Width = w; desc.Height = h;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = count;
    desc.Scaling = DXGI_SCALING_STRETCH;
    desc.SwapEffect = effect;
    desc.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
    desc.Flags = flags;
    if (FAILED(hr = IDXGIFactory2_CreateSwapChainForComposition(factory, (IUnknown *)device, &desc, NULL, &sc)))
    {
        printf("CreateSwapChainForComposition failed %#lx\n", hr); ++failures; return NULL;
    }
    return sc;
}

static void check(const char *what, DWORD got, DWORD expected)
{
    if (got != expected)
    {
        printf("MISMATCH %s: got %08lx, expected %08lx\n", what, got, expected);
        ++failures;
    }
}

/* Present frames k = first..first+n-1; after each, buffer count-1-i must hold frame k-i (sequential). */
static void rotation(IDXGISwapChain1 *sc, unsigned int count, BOOL sequential, unsigned int first, unsigned int n,
        unsigned int w, unsigned int h, const char *tag)
{
    ID3D11Texture2D *buf;
    unsigned int k, i;
    char what[128];
    HRESULT hr;

    for (k = first; k < first + n; ++k)
    {
        if (FAILED(hr = IDXGISwapChain1_GetBuffer(sc, 0, &IID_ID3D11Texture2D, (void **)&buf)))
        { printf("%s: GetBuffer(0) %#lx\n", tag, hr); ++failures; return; }
        clear(buf, colour(k));
        ID3D11Texture2D_Release(buf);
        if (FAILED(hr = IDXGISwapChain1_Present(sc, k & 1, 0))) { printf("Present %#lx\n", hr); ++failures; }
        for (i = 0; i < count; ++i)
        {
            if (i > k - first) break;       /* older frames don't exist yet */
            if (!sequential && !getenv("PROBE_DISCARD")) break; /* Windows: GetBuffer(>0) fails for FLIP_DISCARD */
            if (i && !sequential) break;    /* discard: only the presented frame is defined */
            if (FAILED(hr = IDXGISwapChain1_GetBuffer(sc, count - 1 - i, &IID_ID3D11Texture2D, (void **)&buf)))
            { printf("%s: GetBuffer(%u) %#lx\n", tag, count - 1 - i, hr); ++failures; break; }
            sprintf(what, "%s frame %u buffer %u", tag, k, count - 1 - i);
            check(what, read_pixel(buf, w / 2, h / 2), colour(k - i));
            check(what, read_pixel(buf, w - 1, h - 1), colour(k - i));
            ID3D11Texture2D_Release(buf);
        }
    }
}

static DWORD screen_pixel(int x, int y)
{
    HDC dc = GetDC(NULL);
    COLORREF c = GetPixel(dc, x, y);
    ReleaseDC(NULL, dc);
    return 0xff000000 | GetRValue(c) << 16 | GetGValue(c) << 8 | GetBValue(c);
}

static void pump(void)
{
    MSG msg;
    while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) DispatchMessageA(&msg);
}

static IDXGISwapChain1 *create_hwnd_sc(HWND hwnd)
{
    DXGI_SWAP_CHAIN_DESC1 desc = {0};
    IDXGISwapChain1 *sc;
    HRESULT hr;

    desc.Width = 200; desc.Height = 200;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 1;
    desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    if (FAILED(hr = IDXGIFactory2_CreateSwapChainForHwnd(factory, (IUnknown *)device, hwnd, &desc, NULL, NULL, &sc)))
    {
        printf("CreateSwapChainForHwnd failed %#lx\n", hr); ++failures; return NULL;
    }
    return sc;
}

static void present_hwnd(IDXGISwapChain1 *sc, DWORD c, int x, const char *what)
{
    ID3D11Texture2D *buf;

    IDXGISwapChain1_GetBuffer(sc, 0, &IID_ID3D11Texture2D, (void **)&buf);
    clear(buf, c);
    ID3D11Texture2D_Release(buf);
    IDXGISwapChain1_Present(sc, 0, 0);
    ID3D11DeviceContext_Flush(context);
    Sleep(300);
    pump();
    check(what, screen_pixel(x + 100, 100), c);
}

static volatile LONG stop_reader;
static IDXGISwapChain1 *reader_sc;
static unsigned int reader_count, reader_reads;

static DWORD WINAPI reader_thread(void *arg)
{
    ID3D11Multithread *mt;
    ID3D11Texture2D *buf;
    DWORD c;

    ID3D11DeviceContext_QueryInterface(context, &IID_ID3D11Multithread, (void **)&mt);
    while (!stop_reader)
    {
        ID3D11Multithread_Enter(mt);
        if (SUCCEEDED(IDXGISwapChain1_GetBuffer(reader_sc, reader_count - 1, &IID_ID3D11Texture2D, (void **)&buf)))
        {
            c = read_pixel(buf, 1, 1);
            if (c != read_pixel(buf, 60, 40)) { printf("MISMATCH reader: torn frame\n"); InterlockedIncrement((LONG *)&failures); }
            ID3D11Texture2D_Release(buf);
            ++reader_reads;
        }
        ID3D11Multithread_Leave(mt);
        Sleep(1);
    }
    ID3D11Multithread_Release(mt);
    return 0;
}

int main(int argc, char **argv)
{
    IDXGISwapChain1 *comp, *comp2, *win_sc, *win_sc2;
    ID3D11Multithread *mt;
    ID3D11Texture2D *buf;
    unsigned int count, i;
    HWND hwnd, hwnd2;
    HANDLE thread;
    char tag[64];
    HRESULT hr;
    HDC dc;
    RECT r;

    setvbuf(stdout, NULL, _IONBF, 0);
    printf("start\n");
    CreateDXGIFactory1(&IID_IDXGIFactory2, (void **)&factory);
    hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, D3D11_CREATE_DEVICE_BGRA_SUPPORT, NULL, 0,
            D3D11_SDK_VERSION, &device, NULL, &context);
    if (FAILED(hr)) hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_WARP, NULL, D3D11_CREATE_DEVICE_BGRA_SUPPORT, NULL, 0,
            D3D11_SDK_VERSION, &device, NULL, &context);
    if (FAILED(hr)) { printf("D3D11CreateDevice %#lx\n", hr); return 100; }

    if (getenv("PROBE_DRAW") || (argc > 1 && strstr(argv[1], "draw"))) init_draw();
    printf("fill by %s\n", use_draw ? "draw" : "clear");

    /* 1. The first swapchain of the device is a composition one. */
    for (count = 2; count <= 4; ++count)
    {
        sprintf(tag, "seq %u bufs", count);
        if (!(comp = create_comp(64, 48, count, DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL, 0))) continue;
        rotation(comp, count, TRUE, 1, 9, 64, 48, tag);
        hr = IDXGISwapChain1_ResizeBuffers(comp, 0, 128, 96, DXGI_FORMAT_UNKNOWN, 0);
        if (FAILED(hr)) { printf("ResizeBuffers %#lx\n", hr); ++failures; }
        sprintf(tag, "seq %u bufs resized", count);
        rotation(comp, count, TRUE, 20, 7, 128, 96, tag);
        IDXGISwapChain1_Release(comp);

        sprintf(tag, "discard %u bufs", count);
        if (!(comp = create_comp(64, 48, count, DXGI_SWAP_EFFECT_FLIP_DISCARD, 0))) continue;
        rotation(comp, count, FALSE, 1, 9, 64, 48, tag);
        IDXGISwapChain1_Release(comp);
    }
    printf("rotation: %d failures so far\n", failures);

    /* 2. Window swapchains and composition swapchains interleaved on one device. */
    hwnd = CreateWindowA("static", "probe2 a", WS_POPUP | WS_VISIBLE, 0, 0, 200, 200, NULL, NULL, NULL, NULL);
    hwnd2 = CreateWindowA("static", "probe2 b", WS_POPUP | WS_VISIBLE, 300, 0, 200, 200, NULL, NULL, NULL, NULL);
    pump();
    win_sc = create_hwnd_sc(hwnd);
    win_sc2 = create_hwnd_sc(hwnd2);
    comp = create_comp(64, 48, 2, DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL, 0);
    comp2 = create_comp(320, 240, 3, DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL, 0);
    if (win_sc && win_sc2 && comp && comp2)
    {
        present_hwnd(win_sc, 0xffff0000, 0, "window a red");
        rotation(comp, 2, TRUE, 1, 3, 64, 48, "mixed comp");
        check("window a after comp presents", screen_pixel(100, 100), 0xffff0000);
        present_hwnd(win_sc2, 0xff00ff00, 300, "window b green");
        rotation(comp2, 3, TRUE, 1, 4, 320, 240, "mixed comp2");
        present_hwnd(win_sc, 0xff0000ff, 0, "window a blue");
        check("window b after a", screen_pixel(400, 100), 0xff00ff00);
        rotation(comp, 2, TRUE, 10, 3, 64, 48, "mixed comp again");
        check("window a after comp presents 2", screen_pixel(100, 100), 0xff0000ff);

        /* The window of the swapchain the context last presented to goes away. */
        present_hwnd(win_sc2, 0xffffff00, 300, "window b yellow");
        IDXGISwapChain1_Release(win_sc2); win_sc2 = NULL;
        DestroyWindow(hwnd2);
        pump();
        rotation(comp2, 3, TRUE, 30, 4, 320, 240, "comp2 after window b destroyed");
        rotation(comp, 2, TRUE, 30, 4, 64, 48, "comp after window b destroyed");
        present_hwnd(win_sc, 0xffff00ff, 0, "window a magenta");
        rotation(comp, 2, TRUE, 40, 2, 64, 48, "comp after window a present");

        /* Window destroyed while its swapchain lives, composition goes on. */
        DestroyWindow(hwnd);
        pump();
        rotation(comp, 2, TRUE, 50, 3, 64, 48, "comp after window a destroyed (swapchain alive)");
        if (argc > 1 && strstr(argv[1], "deadwin"))
        {
            hr = IDXGISwapChain1_Present(win_sc, 0, 0);
            printf("Present on a swapchain whose window is gone: %#lx\n", hr);
            rotation(comp2, 3, TRUE, 50, 4, 320, 240, "comp2 after dead-window present");
        }
    }
    if (win_sc) IDXGISwapChain1_Release(win_sc);
    if (win_sc2) IDXGISwapChain1_Release(win_sc2);
    printf("mixed: %d failures so far\n", failures);

    /* 3. GDI-compatible buffers: GetDC on buffer 0, present, read the presented frame. */
    if (comp) IDXGISwapChain1_Release(comp);
    if ((comp = create_comp(64, 48, 2, DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL, DXGI_SWAP_CHAIN_FLAG_GDI_COMPATIBLE)))
    {
        IDXGISurface1 *surface;
        HBRUSH brush;

        for (i = 0; i < 4; ++i)
        {
            DWORD c = colour(100 + i);

            IDXGISwapChain1_GetBuffer(comp, 0, &IID_IDXGISurface1, (void **)&surface);
            if (FAILED(hr = IDXGISurface1_GetDC(surface, i == 0, &dc))) { printf("GetDC %#lx\n", hr); ++failures; }
            else
            {
                SetRect(&r, 0, 0, 64, 48);
                brush = CreateSolidBrush(RGB((c >> 16) & 0xff, (c >> 8) & 0xff, c & 0xff));
                FillRect(dc, &r, brush);
                DeleteObject(brush);
                IDXGISurface1_ReleaseDC(surface, NULL);
            }
            IDXGISurface1_Release(surface);
            IDXGISwapChain1_Present(comp, 0, 0);
            IDXGISwapChain1_GetBuffer(comp, 1, &IID_ID3D11Texture2D, (void **)&buf);
            sprintf(tag, "gdi frame %u", i);
            check(tag, read_pixel(buf, 32, 24) & 0x00ffffff, c & 0x00ffffff);
            ID3D11Texture2D_Release(buf);
        }
        IDXGISwapChain1_Release(comp);
    }
    printf("gdi: %d failures so far\n", failures);

    /* 4. Another thread reads the presented buffer while this one presents; buffer held past the swapchain. */
    if (comp2)
    {
        ID3D11DeviceContext_QueryInterface(context, &IID_ID3D11Multithread, (void **)&mt);
        ID3D11Multithread_SetMultithreadProtected(mt, TRUE);
        reader_sc = comp2; reader_count = 3;
        thread = CreateThread(NULL, 0, reader_thread, NULL, 0, NULL);
        for (i = 0; i < 300; ++i)
        {
            ID3D11Multithread_Enter(mt);
            IDXGISwapChain1_GetBuffer(comp2, 0, &IID_ID3D11Texture2D, (void **)&buf);
            clear(buf, colour(i));
            ID3D11Texture2D_Release(buf);
            ID3D11Multithread_Leave(mt);
            if (FAILED(hr = IDXGISwapChain1_Present(comp2, 0, 0))) { printf("Present %#lx\n", hr); ++failures; break; }
        }
        stop_reader = 1;
        WaitForSingleObject(thread, INFINITE);
        printf("reader: %u reads during 300 presents\n", reader_reads);
        IDXGISwapChain1_GetBuffer(comp2, 2, &IID_ID3D11Texture2D, (void **)&buf);
        printf("release with a buffer held -> %lu\n", IDXGISwapChain1_Release(comp2));
        check("held buffer after swapchain release", read_pixel(buf, 5, 5), colour(299));
        ID3D11Texture2D_Release(buf);
        ID3D11Multithread_Release(mt);
    }

    ID3D11DeviceContext_Release(context);
    printf("device release -> %lu\n", ID3D11Device_Release(device));
    printf("%d failures\n", failures);
    return failures;
}
