/* CreateDeferredContext on one thread while another thread resets the
 * immediate context state (ClearState / ExecuteCommandList(restore=FALSE)).
 * Windows: every CreateDeferredContext succeeds. Wine before the 047 fix:
 * sporadic E_FAIL (feature level read from a state being reset).
 * Usage: d3d11_deferred_race.exe [iterations] [warp]; exit 0 = no failures. */
#define COBJMACROS
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include <d3d11.h>

static ID3D11Device *device;
static ID3D11DeviceContext *immediate;
static volatile LONG done;

static DWORD WINAPI reset_thread(void *arg)
{
    /* Only this thread uses the immediate context; device methods are free-threaded. */
    while (!done)
        ID3D11DeviceContext_ClearState(immediate);
    return 0;
}

int main(int argc, char **argv)
{
    unsigned int i, count = argc > 1 ? atoi(argv[1]) : 20000, failures = 0;
    BOOL warp = argc > 2 && !strcmp(argv[2], "warp");
    D3D_FEATURE_LEVEL fl = D3D_FEATURE_LEVEL_11_0, got;
    ID3D11DeviceContext *deferred;
    HANDLE thread;
    HRESULT hr;

    hr = D3D11CreateDevice(NULL, warp ? D3D_DRIVER_TYPE_WARP : D3D_DRIVER_TYPE_HARDWARE, NULL, 0,
            &fl, 1, D3D11_SDK_VERSION, &device, &got, &immediate);
    if (FAILED(hr))
    {
        printf("D3D11CreateDevice failed, hr %#lx\n", hr);
        return 2;
    }

    thread = CreateThread(NULL, 0, reset_thread, NULL, 0, NULL);
    for (i = 0; i < count; ++i)
    {
        deferred = NULL;
        hr = ID3D11Device_CreateDeferredContext(device, 0, &deferred);
        if (FAILED(hr) || !deferred)
        {
            if (failures++ < 5)
                printf("iteration %u: hr %#lx, context %p\n", i, hr, deferred);
        }
        if (deferred)
            ID3D11DeviceContext_Release(deferred);
    }
    done = 1;
    WaitForSingleObject(thread, INFINITE);

    printf("%u/%u CreateDeferredContext calls failed\n", failures, count);
    ID3D11DeviceContext_Release(immediate);
    ID3D11Device_Release(device);
    return failures ? 1 : 0;
}
