/* Colour-keyed layered toplevel (key red, filled red with "layered red") in this process;
 * a child process creates its own WS_CHILD window in it and presents green through D3D11
 * (like Chromium GPU process in AdskIdentityManager). Prints screen pixels in and outside the child.
 * Win11: child 00ff00 in all modes. CHILDEX=<hex> sets the child ex style (00280024 = Chromium).
 * Build: x86_64-w64-mingw32-gcc -O2 -o layered_child_gpu.exe layered_child_gpu.c -ld3d11 -ldxgi -lgdi32 -luuid */
#define COBJMACROS
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <d3d11.h>
#include <dxgi1_2.h>
int main(int argc, char **argv)
{
    if (argc > 1 && !strcmp(argv[1], "gpu"))
    {
        HWND parent = (HWND)strtoull(argv[2], NULL, 16), w;
        ID3D11Device *d; ID3D11DeviceContext *c; IDXGIDevice *dd; IDXGIAdapter *a; IDXGIFactory2 *f; IDXGISwapChain1 *sc;
        ID3D11Texture2D *bb; ID3D11RenderTargetView *rtv; DXGI_SWAP_CHAIN_DESC1 desc = {0}; float green[] = {0, 1, 0, 1}; int i;
        w = CreateWindowExA(getenv("CHILDEX") ? strtoul(getenv("CHILDEX"), NULL, 16) : 0, "static", NULL, WS_CHILD | WS_VISIBLE | (getenv("CHILDEX") ? WS_DISABLED | WS_CLIPSIBLINGS | WS_CLIPCHILDREN : 0), 0, 0, 300, 200, parent, 0, 0, 0);
        D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, NULL, 0, D3D11_SDK_VERSION, &d, NULL, &c);
        ID3D11Device_QueryInterface(d, &IID_IDXGIDevice, (void **)&dd); IDXGIDevice_GetAdapter(dd, &a); IDXGIAdapter_GetParent(a, &IID_IDXGIFactory2, (void **)&f);
        desc.Width = 300; desc.Height = 200; desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM; desc.SampleDesc.Count = 1;
        desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; desc.BufferCount = 1; desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
        printf("swapchain %#lx\n", IDXGIFactory2_CreateSwapChainForHwnd(f, (IUnknown *)d, w, &desc, NULL, NULL, &sc));
        IDXGISwapChain1_GetBuffer(sc, 0, &IID_ID3D11Texture2D, (void **)&bb);
        ID3D11Device_CreateRenderTargetView(d, (ID3D11Resource *)bb, NULL, &rtv);
        for (i = 0; i < 100; i++)
        {
            MSG m; while (PeekMessageA(&m, 0, 0, 0, PM_REMOVE)) DispatchMessageA(&m);
            ID3D11DeviceContext_ClearRenderTargetView(c, rtv, green);
            IDXGISwapChain1_Present(sc, 0, 0);
            Sleep(100);
        }
        return 0;
    }
    else
    {
        BOOL layered = argc > 1 && !strcmp(argv[1], "layered");
        WNDCLASSA wc = {0}; HWND top; char cmd[256]; STARTUPINFOA si = {sizeof(si)}; PROCESS_INFORMATION pi; MSG m; DWORD end;
        wc.lpfnWndProc = DefWindowProcA; wc.lpszClassName = "lrepro"; wc.hbrBackground = CreateSolidBrush(argc > 2 ? RGB(255, 0, 0) : RGB(0, 0, 0));
        RegisterClassA(&wc);
        top = CreateWindowExA(layered ? WS_EX_LAYERED : 0, "lrepro", NULL, WS_POPUP, 100, 100, 400, 300, 0, 0, 0, 0);
        if (layered) SetLayeredWindowAttributes(top, RGB(255, 0, 0), 0, LWA_COLORKEY);
        ShowWindow(top, SW_SHOW); UpdateWindow(top);
        sprintf(cmd, "\"%s\" gpu %p", argv[0], top);
        CreateProcessA(NULL, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi);
        end = GetTickCount() + 12000;
        {
            DWORD t = GetTickCount() + 6000; HDC dc;
            while (GetTickCount() < t) { while (PeekMessageA(&m, 0, 0, 0, PM_REMOVE)) DispatchMessageA(&m); Sleep(10); }
            dc = GetDC(0);
            printf("mode %s: pixel in child %06lx, in host outside child %06lx\n", argc > 1 ? argv[1] : "plain",
                   GetPixel(dc, 150, 150), GetPixel(dc, 450, 350));
            ReleaseDC(0, dc);
            fflush(stdout);
        }
        while (GetTickCount() < end)
        {
            while (PeekMessageA(&m, 0, 0, 0, PM_REMOVE)) DispatchMessageA(&m);
            Sleep(10);
            if (argc > 1 && !strcmp(argv[1], "late") && GetTickCount() > end - 8000)
            {
                SetWindowLongA(top, GWL_EXSTYLE, WS_EX_LAYERED);
                SetLayeredWindowAttributes(top, RGB(255, 0, 0), 0, LWA_COLORKEY);
                argv[1] = "done";
            }
        }
        return 0;
    }
}
