/* msedgewebview2's GPU process (hardware path) QIs the IDCompositionDesktopDevice from
 * DCompositionCreateDevice3(<d3d11 device>) for the undocumented {4ca97a18-...} and CHECK-fails
 * on E_NOINTERFACE. Win11: create3, IDCompositionDevice3 and 4ca97a18 all S_OK.
 * Build: x86_64-w64-mingw32-gcc -O2 -o dcomp_qi.exe dcomp_qi.c -ldcomp -ld3d11 -luuid */
#define COBJMACROS
#include <windows.h>
#include <stdio.h>
#include <initguid.h>
#include <d3d11.h>
HRESULT WINAPI DCompositionCreateDevice3(IUnknown *rendering_device, REFIID iid, void **device);
DEFINE_GUID(IID_Desktop, 0x5f4633fe, 0x1e08, 0x4cb8, 0x8c, 0x75, 0xce, 0x24, 0x33, 0x3f, 0x56, 0x02);
DEFINE_GUID(IID_Dev3, 0x0987cb06, 0xf916, 0x48bf, 0x8d, 0x35, 0xce, 0x76, 0x41, 0x78, 0x1b, 0xd9);
DEFINE_GUID(IID_X, 0x4ca97a18, 0xcbfd, 0x4b0d, 0x89, 0xe1, 0xf7, 0xfa, 0x86, 0xd8, 0xd6, 0x3e);
int main(void)
{
    ID3D11Device *d3d; IDXGIDevice *dxgi; IUnknown *dev, *x; HRESULT hr;
    hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, D3D11_CREATE_DEVICE_BGRA_SUPPORT, NULL, 0, D3D11_SDK_VERSION, &d3d, NULL, NULL);
    printf("d3d11 %#lx\n", hr); if (FAILED(hr)) return 1;
    ID3D11Device_QueryInterface(d3d, &IID_IDXGIDevice, (void **)&dxgi);
    hr = DCompositionCreateDevice3((IUnknown *)dxgi, &IID_Desktop, (void **)&dev);
    printf("create3 %#lx\n", hr); if (FAILED(hr)) return 1;
    hr = IUnknown_QueryInterface(dev, &IID_Dev3, (void **)&x); printf("qi dev3 %#lx\n", hr); if (SUCCEEDED(hr)) IUnknown_Release(x);
    hr = IUnknown_QueryInterface(dev, &IID_X, (void **)&x); printf("qi 4ca97a18 %#lx\n", hr); if (SUCCEEDED(hr)) IUnknown_Release(x);
    return 0;
}
