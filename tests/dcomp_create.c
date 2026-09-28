/* DCompositionCreateDevice/2/3 with a NULL rendering device, as
 * msedgewebview2's GPU process calls it. Prints each HRESULT; exit 0 = all S_OK.
 * Build: x86_64-w64-mingw32-gcc -O2 -o dcomp_create.exe dcomp_create.c -ldcomp -luuid */
#define COBJMACROS
#include <windows.h>
#include <stdio.h>
#include <initguid.h>
/* mingw's dcomp.h is C++-only. */
HRESULT WINAPI DCompositionCreateDevice(IUnknown *dxgi_device, REFIID iid, void **device);
HRESULT WINAPI DCompositionCreateDevice2(IUnknown *rendering_device, REFIID iid, void **device);
HRESULT WINAPI DCompositionCreateDevice3(IUnknown *rendering_device, REFIID iid, void **device);

DEFINE_GUID(IID_IDCompositionDevice_, 0xc37ea93a, 0xe7aa, 0x450d, 0xb1, 0x6f, 0x97, 0x46, 0xcb, 0x04, 0x07, 0xf3);
DEFINE_GUID(IID_IDCompositionDesktopDevice_, 0x5f4633fe, 0x1e08, 0x4cb8, 0x8c, 0x75, 0xce, 0x24, 0x33, 0x3f, 0x56, 0x02);

int main(void)
{
    IUnknown *dev;
    HRESULT hr[3];
    int i, fail = 0;

    hr[0] = DCompositionCreateDevice(NULL, &IID_IDCompositionDevice_, (void **)&dev);
    if (SUCCEEDED(hr[0])) IUnknown_Release(dev);
    hr[1] = DCompositionCreateDevice2(NULL, &IID_IDCompositionDesktopDevice_, (void **)&dev);
    if (SUCCEEDED(hr[1])) IUnknown_Release(dev);
    hr[2] = DCompositionCreateDevice3(NULL, &IID_IDCompositionDesktopDevice_, (void **)&dev);
    if (SUCCEEDED(hr[2])) IUnknown_Release(dev);
    for (i = 0; i < 3; i++)
    {
        printf("DCompositionCreateDevice%s(NULL) -> %#lx\n", i ? (i == 1 ? "2" : "3") : "", hr[i]);
        if (hr[i] != S_OK) fail = 1;
    }
    return fail;
}
