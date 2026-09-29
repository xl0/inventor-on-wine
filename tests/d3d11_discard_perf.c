/* Map(WRITE_DISCARD) throughput for a large dynamic buffer (060).
 * Each iteration: Map(WRITE_DISCARD), write TOUCH bytes (default all), Unmap,
 * copy 64 KiB of it into a default buffer (so the GPU uses every version),
 * Flush, then wait until the GPU is at most LATENCY iterations behind (event
 * queries, like a present with frame latency 3). With "deferred" the map/copy
 * go through a deferred context + FinishCommandList/ExecuteCommandList, like
 * Inventor's OGS renderer.
 * Prints ms per iteration (p50/max over ITER) and iterations/s.
 * Usage: d3d11_discard_perf.exe [MiB=52] [iterations=200] [deferred] [touch=BYTES] [latency=3] */
#define COBJMACROS
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include <d3d11.h>

static int cmp(const void *a, const void *b)
{
    double x = *(const double *)a, y = *(const double *)b;
    return x < y ? -1 : x > y;
}

int main(int argc, char **argv)
{
    unsigned int mib = argc > 1 ? atoi(argv[1]) : 52, count = argc > 2 ? atoi(argv[2]) : 200, latency = 3, i;
    D3D11_QUERY_DESC query_desc = {D3D11_QUERY_EVENT, 0};
    ID3D11Query *queries[16];
    D3D11_BUFFER_DESC desc = {0};
    ID3D11DeviceContext *immediate, *context, *deferred = NULL;
    D3D_FEATURE_LEVEL fl = D3D_FEATURE_LEVEL_11_0;
    D3D11_MAPPED_SUBRESOURCE map;
    ID3D11Buffer *dynamic, *dst;
    ID3D11CommandList *list;
    LARGE_INTEGER f, t0, t1, start;
    D3D11_BOX box = {0, 0, 0, 65536, 1, 1};
    ID3D11Device *device;
    size_t touch;
    double *ms, total;
    HRESULT hr;

    desc.ByteWidth = mib << 20;
    touch = desc.ByteWidth;
    for (i = 3; i < argc; ++i)
    {
        if (!strcmp(argv[i], "deferred")) deferred = (ID3D11DeviceContext *)1;
        else if (!strncmp(argv[i], "touch=", 6)) touch = strtoull(argv[i] + 6, NULL, 0);
        else if (!strncmp(argv[i], "latency=", 8)) latency = atoi(argv[i] + 8);
    }
    if (!latency || latency > ARRAYSIZE(queries)) latency = 3;
    if (touch > desc.ByteWidth) touch = desc.ByteWidth;

    if (FAILED(hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, &fl, 1,
            D3D11_SDK_VERSION, &device, NULL, &immediate)))
    {
        printf("D3D11CreateDevice failed, hr %#lx\n", hr);
        return 2;
    }
    context = immediate;
    if (deferred && FAILED(hr = ID3D11Device_CreateDeferredContext(device, 0, &deferred)))
    {
        printf("CreateDeferredContext failed, hr %#lx\n", hr);
        return 2;
    }
    if (deferred) context = deferred;

    desc.Usage = D3D11_USAGE_DYNAMIC;
    desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    if (FAILED(hr = ID3D11Device_CreateBuffer(device, &desc, NULL, &dynamic)))
    {
        printf("CreateBuffer(dynamic) failed, hr %#lx\n", hr);
        return 2;
    }
    desc.ByteWidth = 65536;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.CPUAccessFlags = 0;
    ID3D11Device_CreateBuffer(device, &desc, NULL, &dst);
    for (i = 0; i < latency; ++i)
        ID3D11Device_CreateQuery(device, &query_desc, &queries[i]);

    ms = malloc(count * sizeof(*ms));
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&start);
    for (i = 0; i < count; ++i)
    {
        QueryPerformanceCounter(&t0);
        if (i >= latency)
            while (ID3D11DeviceContext_GetData(immediate, (ID3D11Asynchronous *)queries[i % latency],
                    NULL, 0, 0) == S_FALSE) Sleep(0);
        if (FAILED(hr = ID3D11DeviceContext_Map(context, (ID3D11Resource *)dynamic, 0,
                D3D11_MAP_WRITE_DISCARD, 0, &map)))
        {
            printf("Map failed, hr %#lx\n", hr);
            return 1;
        }
        memset(map.pData, i, touch);
        ID3D11DeviceContext_Unmap(context, (ID3D11Resource *)dynamic, 0);
        ID3D11DeviceContext_CopySubresourceRegion(context, (ID3D11Resource *)dst, 0, 0, 0, 0,
                (ID3D11Resource *)dynamic, 0, &box);
        if (deferred)
        {
            ID3D11DeviceContext_FinishCommandList(deferred, FALSE, &list);
            ID3D11DeviceContext_ExecuteCommandList(immediate, list, FALSE);
            ID3D11CommandList_Release(list);
        }
        ID3D11DeviceContext_End(immediate, (ID3D11Asynchronous *)queries[i % latency]);
        ID3D11DeviceContext_Flush(immediate);
        QueryPerformanceCounter(&t1);
        ms[i] = (t1.QuadPart - t0.QuadPart) * 1000.0 / f.QuadPart;
    }
    total = (t1.QuadPart - start.QuadPart) * 1000.0 / f.QuadPart;
    qsort(ms, count, sizeof(*ms), cmp);
    printf("%u MiB %s touch %zu latency %u: %u iterations, %.2f ms/iter p50 %.2f max %.2f, %.1f iter/s\n",
            mib, deferred ? "deferred" : "immediate", touch, latency, count, total / count,
            ms[count / 2], ms[count - 1], count * 1000.0 / total);
    return 0;
}
