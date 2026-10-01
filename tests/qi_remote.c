/* Which QueryInterface calls on a cross-process proxy reach the object (097)?
 * The parent (STA) marshals an object that only implements IUnknown and logs every QI it
 * gets; a child process unmarshals it in an STA and QIs the proxy for a list of IIDs
 * (twice each, then again through a second unmarshal of the same object).
 * .NET's RCW creation QIs every new object for IManagedObject, IProvideClassInfo,
 * IInspectable, INoMarshal, IAgileObject and IRpcOptions; each one that goes remote is a
 * round trip into the server's STA. Also: IRpcOptions Query/Set on the proxy (cross-process and,
 * in the parent, cross-apartment), and the cost of 1000 remote QIs.
 * Usage: qi_remote.exe            (parent; spawns itself as "qi_remote.exe client FILE")
 * Build: x86_64-w64-mingw32-gcc -O2 -o qi_remote.exe qi_remote.c -lole32 -luuid */
#define COBJMACROS
#include <windows.h>
#include <objbase.h>
#include <stdio.h>

static const struct { const char *name; GUID iid; } iids[] =
{
    { "IUnknown",           {0x00000000,0,0,{0xc0,0,0,0,0,0,0,0x46}} },
    { "IMarshal",           {0x00000003,0,0,{0xc0,0,0,0,0,0,0,0x46}} },
    { "IMultiQI",           {0x00000020,0,0,{0xc0,0,0,0,0,0,0,0x46}} },
    { "IClientSecurity",    {0x0000013d,0,0,{0xc0,0,0,0,0,0,0,0x46}} },
    { "IRpcOptions",        {0x00000144,0,0,{0xc0,0,0,0,0,0,0,0x46}} },
    { "ICallFactory",       {0x1c733a30,0x2a1c,0x11ce,{0xad,0xe5,0x00,0xaa,0x00,0x44,0x77,0x3d}} },
    { "IAgileObject",       {0x94ea2b94,0xe9cc,0x49e0,{0xc0,0xff,0xee,0x64,0xca,0x8f,0x5b,0x90}} },
    { "INoMarshal",         {0xecc8691b,0xc1db,0x4dc0,{0x85,0x5e,0x65,0xf6,0xc5,0x51,0xaf,0x49}} },
    { "IInspectable",       {0xaf86e2e0,0xb12d,0x4c6a,{0x9c,0x5a,0xd7,0xaa,0x65,0x10,0x1e,0x90}} },
    { "IManagedObject",     {0xc3fcc19e,0xa970,0x11d2,{0x8b,0x5a,0x00,0xa0,0xc9,0xb7,0xc9,0xc4}} },
    { "IProvideClassInfo",  {0xb196b283,0xbab4,0x101a,{0xb6,0x9c,0x00,0xaa,0x00,0x34,0x1d,0x07}} },
    { "IStdMarshalInfo",    {0x00000018,0,0,{0xc0,0,0,0,0,0,0,0x46}} },
    { "IExternalConnection",{0x00000019,0,0,{0xc0,0,0,0,0,0,0,0x46}} },
    { "IPersist",           {0x0000010c,0,0,{0xc0,0,0,0,0,0,0,0x46}} },
    { "IDispatch",          {0x00020400,0,0,{0xc0,0,0,0,0,0,0,0x46}} },
    { "random",             {0x12345678,0x1234,0x1234,{1,2,3,4,5,6,7,8}} },
};
#define N_IIDS (sizeof(iids) / sizeof(iids[0]))

static LONG qi_count[N_IIDS + 1], logging;
static GUID other[16];
static LONG n_other;

static HRESULT WINAPI obj_QueryInterface(IUnknown *iface, REFIID riid, void **out)
{
    unsigned int i;
    for (i = 0; i < N_IIDS; i++) if (IsEqualGUID(riid, &iids[i].iid)) break;
    if (logging) InterlockedIncrement(&qi_count[i]);
    if (logging && i == N_IIDS && n_other < 16) other[n_other++] = *riid;
    if (IsEqualGUID(riid, &IID_IUnknown)) { *out = iface; return S_OK; }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI obj_AddRef(IUnknown *iface) { return 2; }
static ULONG WINAPI obj_Release(IUnknown *iface) { return 1; }
static IUnknownVtbl obj_vtbl = { obj_QueryInterface, obj_AddRef, obj_Release };
static IUnknown obj = { &obj_vtbl };

static IUnknown *unmarshal(const char *file)
{
    char buf[4096];
    DWORD size;
    HANDLE f = CreateFileA(file, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    HGLOBAL mem;
    IStream *stream;
    IUnknown *unk = NULL;
    HRESULT hr;

    ReadFile(f, buf, sizeof(buf), &size, NULL);
    CloseHandle(f);
    mem = GlobalAlloc(GMEM_MOVEABLE, size);
    memcpy(GlobalLock(mem), buf, size);
    GlobalUnlock(mem);
    CreateStreamOnHGlobal(mem, TRUE, &stream);
    hr = CoUnmarshalInterface(stream, &IID_IUnknown, (void **)&unk);
    if (FAILED(hr)) printf("CoUnmarshalInterface %#lx\n", hr);
    IStream_Release(stream);
    return unk;
}

static DWORD WINAPI local_thread(void *file)
{
    IUnknown *unk;
    IRpcOptions *opts;
    ULONG_PTR v = 0xdead;
    HRESULT hr;

    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    unk = unmarshal(file);
    hr = IUnknown_QueryInterface(unk, &iids[4].iid, (void **)&opts);
    if (SUCCEEDED(hr))
    {
        hr = IRpcOptions_Query(opts, unk, COMBND_SERVER_LOCALITY, &v);
        IRpcOptions_Release(opts);
    }
    printf("cross-apartment proxy: IRpcOptions locality hr %#lx value %#Ix\n", hr, v);
    IUnknown_Release(unk);
    CoUninitialize();
    return 0;
}

static int client(const char *file)
{
    IUnknown *unk, *unk2, *p;
    unsigned int i, round;
    LARGE_INTEGER f, t0, t1;

    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    QueryPerformanceFrequency(&f);
    unk = unmarshal(file);
    if (!unk) return 1;
    for (round = 0; round < 2; round++)
        for (i = 0; i < N_IIDS; i++)
        {
            HRESULT hr;
            QueryPerformanceCounter(&t0);
            hr = IUnknown_QueryInterface(unk, &iids[i].iid, (void **)&p);
            QueryPerformanceCounter(&t1);
            printf("client round %u %-20s hr %#010lx %7.1f us\n", round, iids[i].name, hr,
                   (t1.QuadPart - t0.QuadPart) * 1e6 / f.QuadPart);
            if (SUCCEEDED(hr)) IUnknown_Release(p);
        }
    if (SUCCEEDED(IUnknown_QueryInterface(unk, &iids[4].iid, (void **)&p)))
    {
        IRpcOptions *opts = (IRpcOptions *)p, *opts2;
        IUnknown *back;
        ULONG_PTR v;
        HRESULT hr;
        DWORD prop;

        for (prop = 0; prop <= 3; prop++)
        {
            v = 0xdead;
            hr = IRpcOptions_Query(opts, unk, prop, &v);
            printf("IRpcOptions Query(proxy, %lu) hr %#lx value %#Ix\n", prop, hr, v);
        }
        v = 0xdead;
        hr = IRpcOptions_Query(opts, NULL, COMBND_SERVER_LOCALITY, &v);
        printf("IRpcOptions Query(NULL, locality) hr %#lx value %#Ix\n", hr, v);
        hr = IRpcOptions_Set(opts, unk, COMBND_RPCTIMEOUT, 1234);
        printf("IRpcOptions Set(timeout) hr %#lx\n", hr);
        v = 0xdead;
        hr = IRpcOptions_Query(opts, unk, COMBND_RPCTIMEOUT, &v);
        printf("IRpcOptions Query(timeout) after Set hr %#lx value %#Ix\n", hr, v);
        hr = IRpcOptions_Set(opts, unk, COMBND_RPCTIMEOUT, 7);
        printf("IRpcOptions Set(timeout 7) hr %#lx\n", hr);
        v = 0xdead;
        hr = IRpcOptions_Query(opts, unk, COMBND_RPCTIMEOUT, &v);
        printf("IRpcOptions Query(timeout) after Set(7) hr %#lx value %#Ix\n", hr, v);
        hr = IRpcOptions_Set(opts, unk, COMBND_SERVER_LOCALITY, 0);
        printf("IRpcOptions Set(locality) hr %#lx\n", hr);
        IRpcOptions_QueryInterface(opts, &IID_IUnknown, (void **)&back);
        printf("IRpcOptions QI(IUnknown) is proxy identity %d\n", back == unk);
        IUnknown_Release(back);
        IUnknown_QueryInterface(unk, &iids[4].iid, (void **)&opts2);
        printf("IRpcOptions same pointer twice %d\n", opts2 == opts);
        IRpcOptions_Release(opts2);
        IRpcOptions_Release(opts);
    }
    QueryPerformanceCounter(&t0);
    for (i = 0; i < 1000; i++) IUnknown_QueryInterface(unk, &iids[N_IIDS - 1].iid, (void **)&p);
    QueryPerformanceCounter(&t1);
    printf("1000 remote QIs: %.1f us each\n", (t1.QuadPart - t0.QuadPart) * 1e6 / f.QuadPart / 1000);
    unk2 = unmarshal(file);
    printf("second unmarshal: same proxy %d\n", unk2 == unk);
    if (unk2) IUnknown_Release(unk2);
    IUnknown_Release(unk);
    CoUninitialize();
    return 0;
}

int main(int argc, char **argv)
{
    char cmd[MAX_PATH * 2], file[MAX_PATH], exe[MAX_PATH];
    STARTUPINFOA si = { sizeof(si) };
    PROCESS_INFORMATION pi;
    IStream *stream;
    HGLOBAL mem;
    HANDLE f;
    DWORD written;
    unsigned int i;
    HRESULT hr;

    if (argc > 2 && !strcmp(argv[1], "client")) return client(argv[2]);

    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    CreateStreamOnHGlobal(NULL, TRUE, &stream);
    /* table-strong so that both unmarshals in the client work */
    hr = CoMarshalInterface(stream, &IID_IUnknown, &obj, MSHCTX_LOCAL, NULL, MSHLFLAGS_TABLESTRONG);
    if (FAILED(hr)) { printf("CoMarshalInterface %#lx\n", hr); return 1; }
    GetHGlobalFromStream(stream, &mem);
    GetTempPathA(MAX_PATH, file);
    strcat(file, "qi_remote.bin");
    f = CreateFileA(file, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    WriteFile(f, GlobalLock(mem), GlobalSize(mem), &written, NULL);
    GlobalUnlock(mem);
    CloseHandle(f);

    logging = 1;
    GetModuleFileNameA(NULL, exe, sizeof(exe));
    sprintf(cmd, "\"%s\" client \"%s\"", exe, file);
    if (!CreateProcessA(exe, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) return 1;
    for (;;)
    {
        MSG msg;
        DWORD ret = MsgWaitForMultipleObjects(1, &pi.hProcess, FALSE, 30000, QS_ALLINPUT);
        if (ret != WAIT_OBJECT_0 + 1) break;
        while (PeekMessageA(&msg, 0, 0, 0, PM_REMOVE)) DispatchMessageA(&msg);
    }
    logging = 0;
    {
        HANDLE thread = CreateThread(NULL, 0, local_thread, file, 0, NULL);
        for (;;)
        {
            MSG msg;
            DWORD ret = MsgWaitForMultipleObjects(1, &thread, FALSE, 30000, QS_ALLINPUT);
            if (ret != WAIT_OBJECT_0 + 1) break;
            while (PeekMessageA(&msg, 0, 0, 0, PM_REMOVE)) DispatchMessageA(&msg);
        }
    }
    for (i = 0; i <= N_IIDS; i++)
        if (qi_count[i]) printf("object got QI %-20s x%ld\n", i < N_IIDS ? iids[i].name : "(other)", qi_count[i]);
    for (i = 0; i < n_other; i++)
        printf("other IID {%08lx-%04x-%04x-%02x%02x-...}\n", other[i].Data1, other[i].Data2, other[i].Data3,
               other[i].Data4[0], other[i].Data4[1]);
    printf("done\n");
    return 0;
}
