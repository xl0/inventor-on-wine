/* D3D11 frame loop for wined3d command-stream (CS) thread costs (108).
 * Usage: cs_spin_bench.exe FPS DRAWS SECS [sync|-] [GAP_US]
 *   FPS 0 = unthrottled; each frame: DRAWS x (UpdateSubresource of a 16-byte constant buffer + Draw(3)),
 *   Present(0). "sync": after each Present, copy a 1x1 texture to a staging texture and Map it for reading
 *   (a full round trip through the CS thread and the GPU), timed. (Copying from the back buffer instead
 *   takes ~14 ms on wined3d-vk.) GAP_US: busy-wait this long before each draw
 *   (application work between submissions).
 * Prints achieved fps, process and main-thread CPU (% of one core), sync latency p50/p90/max in us.
 * Build: x86_64-w64-mingw32-gcc -O2 -o X.exe X.c -ld3d11 -ld3dcompiler_47 -lwinmm -luuid */
#define _WIN32_WINNT 0x0A00
#define COBJMACROS
#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 2
#endif
#include <stdio.h>
#include <stdlib.h>
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>

#define CHECK(x) do { HRESULT hr_ = (x); if (FAILED(hr_)) { printf("FAIL %s: 0x%08lx\n", #x, hr_); exit(1); } } while (0)

static const char vs_src[] = "cbuffer c : register(b0) { float4 o; }\n"
    "float4 main(uint id : SV_VertexID) : SV_Position\n"
    "{ return float4(o.x + (id == 1) * 0.1, o.y + (id == 2) * 0.1, 0, 1); }\n";
static const char ps_src[] = "float4 main() : SV_Target { return float4(1, 0.5, 0, 1); }\n";

static ID3DBlob *compile( const char *src, const char *target )
{
    ID3DBlob *blob, *err = NULL;
    if (FAILED(D3DCompile( src, strlen( src ), NULL, NULL, NULL, "main", target, 0, 0, &blob, &err )))
    {
        printf( "compile %s: %s\n", target, err ? (char *)ID3D10Blob_GetBufferPointer( err ) : "?" );
        exit( 1 );
    }
    return blob;
}

static double cpu_s( FILETIME k, FILETIME u )
{
    return (((ULONGLONG)k.dwHighDateTime << 32 | k.dwLowDateTime) + ((ULONGLONG)u.dwHighDateTime << 32 | u.dwLowDateTime)) / 1e7;
}

static int cmp( const void *a, const void *b ) { double x = *(double *)a, y = *(double *)b; return x < y ? -1 : x > y; }

int main( int argc, char **argv )
{
    int fps = argc > 1 ? atoi( argv[1] ) : 60, draws = argc > 2 ? atoi( argv[2] ) : 100;
    double secs = argc > 3 ? atof( argv[3] ) : 10, *lat = NULL, t0, t, pcpu0, tcpu0;
    BOOL sync = argc > 4 && !strcmp( argv[4], "sync" );
    double gap = argc > 5 ? atof( argv[5] ) : 0;
    LONGLONG gap_ticks;
    D3D11_TEXTURE2D_DESC td = {1, 1, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, {1, 0}, D3D11_USAGE_STAGING, 0, D3D11_CPU_ACCESS_READ, 0};
    D3D11_BUFFER_DESC bd = {16, D3D11_USAGE_DEFAULT, D3D11_BIND_CONSTANT_BUFFER, 0, 0, 0};
    D3D11_BOX box = {0, 0, 0, 1, 1, 1};
    static const float clear[4] = {0, 0, 0.2f, 1};
    LARGE_INTEGER freq, now, due;
    DXGI_SWAP_CHAIN_DESC sd = {0};
    D3D11_MAPPED_SUBRESOURCE map;
    FILETIME ft[4];
    ID3D11RenderTargetView *rtv;
    ID3D11VertexShader *vs;
    ID3D11PixelShader *ps;
    ID3D11DeviceContext *ctx;
    ID3D11Texture2D *bb, *staging, *src;
    ID3D11Buffer *cb;
    IDXGISwapChain *sc;
    ID3D11Device *dev;
    D3D11_VIEWPORT vp = {0, 0, 640, 480, 0, 1};
    ID3DBlob *vsb, *psb;
    HANDLE timer;
    HWND hwnd;
    MSG msg;
    int frames = 0, nlat = 0;

    setvbuf( stdout, NULL, _IONBF, 0 );
    timeBeginPeriod( 1 );
    hwnd = CreateWindowA( "static", "cs_spin_bench", WS_POPUP | WS_VISIBLE, 0, 0, 640, 480, 0, 0, 0, 0 );
    sd.BufferCount = 2;
    sd.BufferDesc.Width = 640; sd.BufferDesc.Height = 480;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hwnd; sd.SampleDesc.Count = 1; sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    CHECK(D3D11CreateDeviceAndSwapChain( NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, NULL, 0,
                                         D3D11_SDK_VERSION, &sd, &sc, &dev, NULL, &ctx ));
    CHECK(IDXGISwapChain_GetBuffer( sc, 0, &IID_ID3D11Texture2D, (void **)&bb ));
    CHECK(ID3D11Device_CreateRenderTargetView( dev, (ID3D11Resource *)bb, NULL, &rtv ));
    CHECK(ID3D11Device_CreateTexture2D( dev, &td, NULL, &staging ));
    td.Usage = D3D11_USAGE_DEFAULT; td.CPUAccessFlags = 0;
    CHECK(ID3D11Device_CreateTexture2D( dev, &td, NULL, &src ));
    CHECK(ID3D11Device_CreateBuffer( dev, &bd, NULL, &cb ));
    vsb = compile( vs_src, "vs_4_0" );
    psb = compile( ps_src, "ps_4_0" );
    CHECK(ID3D11Device_CreateVertexShader( dev, ID3D10Blob_GetBufferPointer( vsb ), ID3D10Blob_GetBufferSize( vsb ), NULL, &vs ));
    CHECK(ID3D11Device_CreatePixelShader( dev, ID3D10Blob_GetBufferPointer( psb ), ID3D10Blob_GetBufferSize( psb ), NULL, &ps ));
    timer = CreateWaitableTimerExW( NULL, NULL, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS );
    if (sync) lat = malloc( sizeof(*lat) * (size_t)(secs * (fps ? fps : 5000) + 100) );

    QueryPerformanceFrequency( &freq );
    gap_ticks = (LONGLONG)(gap * freq.QuadPart / 1e6);
    for (int warm = 1; warm >= 0; warm--)
    {
        QueryPerformanceCounter( &now );
        t0 = (double)now.QuadPart / freq.QuadPart;
        due.QuadPart = now.QuadPart;
        GetProcessTimes( GetCurrentProcess(), &ft[0], &ft[1], &ft[2], &ft[3] );
        pcpu0 = cpu_s( ft[2], ft[3] );
        GetThreadTimes( GetCurrentThread(), &ft[0], &ft[1], &ft[2], &ft[3] );
        tcpu0 = cpu_s( ft[2], ft[3] );
        frames = nlat = 0;
        for (t = t0; t - t0 < (warm ? 2 : secs); frames++)
        {
            while (PeekMessageA( &msg, 0, 0, 0, PM_REMOVE )) DispatchMessageA( &msg );
            ID3D11DeviceContext_OMSetRenderTargets( ctx, 1, &rtv, NULL );
            ID3D11DeviceContext_RSSetViewports( ctx, 1, &vp );
            ID3D11DeviceContext_IASetPrimitiveTopology( ctx, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST );
            ID3D11DeviceContext_VSSetShader( ctx, vs, NULL, 0 );
            ID3D11DeviceContext_PSSetShader( ctx, ps, NULL, 0 );
            ID3D11DeviceContext_VSSetConstantBuffers( ctx, 0, 1, &cb );
            ID3D11DeviceContext_ClearRenderTargetView( ctx, rtv, clear );
            for (int i = 0; i < draws; i++)
            {
                float o[4] = {(i % 19) * 0.1f - 1, (i / 19 % 19) * 0.1f - 1, 0, 0};
                if (gap_ticks)
                {
                    LARGE_INTEGER g0, g1;
                    QueryPerformanceCounter( &g0 );
                    do QueryPerformanceCounter( &g1 ); while (g1.QuadPart - g0.QuadPart < gap_ticks);
                }
                ID3D11DeviceContext_UpdateSubresource( ctx, (ID3D11Resource *)cb, 0, NULL, o, 0, 0 );
                ID3D11DeviceContext_Draw( ctx, 3, 0 );
            }
            IDXGISwapChain_Present( sc, 0, 0 );
            if (sync)
            {
                LARGE_INTEGER a, b;
                QueryPerformanceCounter( &a );
                ID3D11DeviceContext_CopySubresourceRegion( ctx, (ID3D11Resource *)staging, 0, 0, 0, 0, (ID3D11Resource *)src, 0, &box );
                CHECK(ID3D11DeviceContext_Map( ctx, (ID3D11Resource *)staging, 0, D3D11_MAP_READ, 0, &map ));
                ID3D11DeviceContext_Unmap( ctx, (ID3D11Resource *)staging, 0 );
                QueryPerformanceCounter( &b );
                if (!warm) lat[nlat++] = (double)(b.QuadPart - a.QuadPart) * 1e6 / freq.QuadPart;
            }
            if (fps)
            {
                due.QuadPart += freq.QuadPart / fps;
                QueryPerformanceCounter( &now );
                if (due.QuadPart > now.QuadPart)
                {
                    LARGE_INTEGER rel = {.QuadPart = -(LONGLONG)((due.QuadPart - now.QuadPart) * 10000000 / freq.QuadPart)};
                    SetWaitableTimer( timer, &rel, 0, NULL, NULL, FALSE );
                    WaitForSingleObject( timer, INFINITE );
                }
                else due = now;
            }
            QueryPerformanceCounter( &now );
            t = (double)now.QuadPart / freq.QuadPart;
        }
    }
    GetProcessTimes( GetCurrentProcess(), &ft[0], &ft[1], &ft[2], &ft[3] );
    pcpu0 = cpu_s( ft[2], ft[3] ) - pcpu0;
    GetThreadTimes( GetCurrentThread(), &ft[0], &ft[1], &ft[2], &ft[3] );
    tcpu0 = cpu_s( ft[2], ft[3] ) - tcpu0;
    printf( "fps_target=%d draws=%d gap=%.0f secs=%.1f fps=%.1f process_cpu=%.1f%% main_cpu=%.1f%%", fps, draws, gap, t - t0,
            frames / (t - t0), pcpu0 * 100 / (t - t0), tcpu0 * 100 / (t - t0) );
    if (nlat)
    {
        qsort( lat, nlat, sizeof(*lat), cmp );
        printf( " sync_us p50=%.0f p90=%.0f max=%.0f", lat[nlat / 2], lat[nlat * 9 / 10], lat[nlat - 1] );
    }
    printf( "\n" );
    return 0;
}
