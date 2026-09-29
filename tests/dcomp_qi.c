/* msedgewebview2's GPU process (hardware path) QIs the IDCompositionDesktopDevice from
 * DCompositionCreateDevice3(<d3d11 device>) for the undocumented {4ca97a18-...} and CHECK-fails
 * on E_NOINTERFACE; next it CHECK-fails on the immediate context's ID3D11VideoContext1 (085).
 * Prints QI results and which interface pointers are identical, for devices from
 * DCompositionCreateDevice (v1), DCompositionCreateDevice3 with a D3D device and with NULL.
 * Build: x86_64-w64-mingw32-gcc -O2 -o dcomp_qi.exe dcomp_qi.c -ldcomp -ld3d11 -luuid */
#define COBJMACROS
#include <windows.h>
#include <stdio.h>
#include <initguid.h>
#include <d3d11_1.h>
HRESULT WINAPI DCompositionCreateDevice(IDXGIDevice *dxgi_device, REFIID iid, void **device);
HRESULT WINAPI DCompositionCreateDevice3(IUnknown *rendering_device, REFIID iid, void **device);
DEFINE_GUID(IID_Dev1, 0xc37ea93a, 0xe7aa, 0x450d, 0xb1, 0x6f, 0x97, 0x46, 0xcb, 0x04, 0x07, 0xf3);
DEFINE_GUID(IID_Desktop, 0x5f4633fe, 0x1e08, 0x4cb8, 0x8c, 0x75, 0xce, 0x24, 0x33, 0x3f, 0x56, 0x02);
DEFINE_GUID(IID_Dev3, 0x0987cb06, 0xf916, 0x48bf, 0x8d, 0x35, 0xce, 0x76, 0x41, 0x78, 0x1b, 0xd9);
DEFINE_GUID(IID_Partner, 0xd14b6158, 0xc3fa, 0x4bce, 0x9c, 0x1f, 0xb6, 0x1d, 0x86, 0x65, 0xea, 0xb0);
DEFINE_GUID(IID_Unknown28, 0x28d6ad3d, 0xee2a, 0x4bcd, 0x94, 0x19, 0x7d, 0x54, 0x80, 0x04, 0x35, 0xb1);
DEFINE_GUID(IID_X, 0x4ca97a18, 0xcbfd, 0x4b0d, 0x89, 0xe1, 0xf7, 0xfa, 0x86, 0xd8, 0xd6, 0x3e);

static void probe(const char *name, IUnknown *dev)
{
    static const struct { const GUID *iid; const char *name; } iids[] =
    {
        {&IID_IUnknown, "IUnknown"}, {&IID_Dev1, "Device"}, {&IID_Desktop, "DesktopDevice"},
        {&IID_Dev3, "Device3"}, {&IID_Partner, "Partner(d14b6158)"}, {&IID_Unknown28, "28d6ad3d"},
        {&IID_X, "4ca97a18"},
    };
    void *p[ARRAYSIZE(iids)];
    int i, j;

    printf("%s:\n", name);
    for (i = 0; i < ARRAYSIZE(iids); i++)
    {
        HRESULT hr = IUnknown_QueryInterface(dev, iids[i].iid, &p[i]);
        if (FAILED(hr)) p[i] = NULL;
        printf("  %-18s %#lx", iids[i].name, hr);
        for (j = 0; j < i; j++) if (p[i] && p[i] == p[j]) { printf(" == %s", iids[j].name); break; }
        printf("\n");
    }
    for (i = 0; i < ARRAYSIZE(iids); i++) if (p[i]) IUnknown_Release((IUnknown *)p[i]);
}

int main(void)
{
    ID3D11Device *d3d; ID3D11DeviceContext *ctx; IDXGIDevice *dxgi; IUnknown *dev, *x; HRESULT hr;

    hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, D3D11_CREATE_DEVICE_BGRA_SUPPORT, NULL, 0, D3D11_SDK_VERSION, &d3d, NULL, &ctx);
    printf("d3d11 %#lx\n", hr); if (FAILED(hr)) return 1;
    hr = ID3D11Device_QueryInterface(d3d, &IID_ID3D11VideoDevice1, (void **)&x); printf("device VideoDevice1 %#lx\n", hr); if (SUCCEEDED(hr)) IUnknown_Release(x);
    hr = ID3D11DeviceContext_QueryInterface(ctx, &IID_ID3D11VideoContext, (void **)&x); printf("context VideoContext %#lx\n", hr); if (SUCCEEDED(hr)) IUnknown_Release(x);
    hr = ID3D11DeviceContext_QueryInterface(ctx, &IID_ID3D11VideoContext1, (void **)&x); printf("context VideoContext1 %#lx\n", hr); if (SUCCEEDED(hr)) IUnknown_Release(x);
    ID3D11Device_QueryInterface(d3d, &IID_IDXGIDevice, (void **)&dxgi);

    if (SUCCEEDED(hr = DCompositionCreateDevice(dxgi, &IID_Dev1, (void **)&dev))) { probe("CreateDevice(dxgi)", dev); IUnknown_Release(dev); }
    else printf("create1 %#lx\n", hr);
    if (SUCCEEDED(hr = DCompositionCreateDevice3((IUnknown *)dxgi, &IID_Desktop, (void **)&dev))) { probe("CreateDevice3(dxgi)", dev); IUnknown_Release(dev); }
    else printf("create3 %#lx\n", hr);
    if (SUCCEEDED(hr = DCompositionCreateDevice3(NULL, &IID_Desktop, (void **)&dev))) { probe("CreateDevice3(NULL)", dev); IUnknown_Release(dev); }
    else printf("create3(NULL) %#lx\n", hr);
    return 0;
}
