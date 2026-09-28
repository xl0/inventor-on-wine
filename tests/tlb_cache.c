/* Typelib cache / typelib marshaler probe + benchmark (issue 032).
 * Builds a big typelib (NTYPES dispinterfaces + NTYPES dual interfaces, NFUNCS methods each),
 * registers it (needs admin), then:
 *  - load/release/load: same pointer? file still in use after the last release?
 *  - refcounts of the typelib / a typeinfo while a proxy/stub for its IID exists;
 *  - timing: marshal (stub) + unmarshal (proxy) once per IID, MTA object -> STA proxy.
 * Args: "hold" keeps a typelib ref during the benchmark, "rename" moves the typelib file away
 * before it, "n=N" marshals only N IIDs per kind (and skips load cycles), anything else is another typelib for load/release timing (e.g. RxInventor.tlb).
 * Build: x86_64-w64-mingw32-gcc -O2 -o tlb_cache.exe tlb_cache.c -loleaut32 -lole32 -luuid */
#define COBJMACROS
#include <windows.h>
#include <stdio.h>
#include <oleauto.h>

#define NTYPES 1000
#define NFUNCS 30

static const GUID PSDispatch = {0x00020420, 0, 0, {0xc0, 0, 0, 0, 0, 0, 0, 0x46}};
static const GUID PSOAInterface = {0x00020424, 0, 0, {0xc0, 0, 0, 0, 0, 0, 0, 0x46}};
static GUID libid = {0x2d1a0b3c, 0x5e6f, 0x4a7b, {0x8c, 0x9d, 0x0e, 0x1f, 0x2a, 0x3b, 0x4c, 0x00}};
static GUID iids[2 * NTYPES];
static WCHAR path[MAX_PATH];
static int cycles = 10, bench_count = NTYPES - 1;

static double now(void)
{
    LARGE_INTEGER c, f;
    QueryPerformanceCounter(&c); QueryPerformanceFrequency(&f);
    return (double)c.QuadPart / f.QuadPart;
}

static ULONG refs(IUnknown *u)
{
    IUnknown_AddRef(u);
    return IUnknown_Release(u);
}

#define CHECK(x) do { HRESULT hr_ = (x); if (FAILED(hr_)) { printf("%s: %#lx (line %d)\n", #x, hr_, __LINE__); exit(1); } } while (0)

static void build_typelib(void)
{
    ICreateTypeLib2 *ctl;
    ITypeLib *std;
    ITypeInfo *disp;
    WCHAR name[64], pname[] = L"arg";
    OLECHAR *names[2];
    int i, j;

    CHECK(LoadTypeLib(L"stdole2.tlb", &std));
    CHECK(ITypeLib_GetTypeInfoOfGuid(std, &IID_IDispatch, &disp));
    CHECK(CreateTypeLib2(sizeof(void *) == 8 ? SYS_WIN64 : SYS_WIN32, path, &ctl));
    CHECK(ICreateTypeLib2_SetGuid(ctl, &libid));
    CHECK(ICreateTypeLib2_SetName(ctl, (OLECHAR *)L"TlbCacheBench"));
    CHECK(ICreateTypeLib2_SetVersion(ctl, 1, 0));
    CHECK(ICreateTypeLib2_SetLcid(ctl, LOCALE_NEUTRAL));
    for (i = 0; i < 2 * NTYPES; i++)
    {
        BOOL dual = i >= NTYPES;
        ICreateTypeInfo *cti;
        HREFTYPE href;

        swprintf(name, 64, L"%ls%d", dual ? L"IDual" : L"IDisp", i);
        CHECK(ICreateTypeLib2_CreateTypeInfo(ctl, name, dual ? TKIND_INTERFACE : TKIND_DISPATCH, &cti));
        iids[i] = libid;
        iids[i].Data4[6] = 1 + dual;
        iids[i].Data4[7] = i;
        iids[i].Data3 = i;
        CHECK(ICreateTypeInfo_SetGuid(cti, &iids[i]));
        CHECK(ICreateTypeInfo_SetTypeFlags(cti, dual ? TYPEFLAG_FDUAL | TYPEFLAG_FOLEAUTOMATION : TYPEFLAG_FDISPATCHABLE));
        CHECK(ICreateTypeInfo_AddRefTypeInfo(cti, disp, &href));
        CHECK(ICreateTypeInfo_AddImplType(cti, 0, href));
        for (j = 0; j < NFUNCS; j++)
        {
            FUNCDESC fd = {0};
            ELEMDESC param = {0};
            WCHAR fname[32];

            param.tdesc.vt = VT_BSTR;
            param.paramdesc.wParamFlags = PARAMFLAG_FIN;
            fd.memid = 0x60020000 + j;
            fd.funckind = dual ? FUNC_PUREVIRTUAL : FUNC_DISPATCH;
            fd.invkind = INVOKE_FUNC;
            fd.callconv = CC_STDCALL;
            fd.cParams = 1;
            fd.lprgelemdescParam = &param;
            fd.elemdescFunc.tdesc.vt = dual ? VT_HRESULT : VT_I4;
            fd.oVft = (7 + j) * sizeof(void *);
            CHECK(ICreateTypeInfo_AddFuncDesc(cti, j, &fd));
            swprintf(fname, 32, L"Method%d_%d", i, j);
            names[0] = fname; names[1] = pname;
            CHECK(ICreateTypeInfo_SetFuncAndParamNames(cti, j, names, 2));
        }
        CHECK(ICreateTypeInfo_LayOut(cti));
        ICreateTypeInfo_Release(cti);
    }
    CHECK(ICreateTypeLib2_SaveAllChanges(ctl));
    ICreateTypeLib2_Release(ctl);
    ITypeInfo_Release(disp);
    ITypeLib_Release(std);
}

/* Object answering QI for IUnknown, IDispatch and all our IIDs; lives in the MTA. */
static LONG obj_refs = 1;
static HRESULT WINAPI obj_QI(IDispatch *iface, REFIID iid, void **out)
{
    int i;
    *out = NULL;
    if (IsEqualIID(iid, &IID_IUnknown) || IsEqualIID(iid, &IID_IDispatch)) *out = iface;
    for (i = 0; i < 2 * NTYPES; i++) if (IsEqualIID(iid, &iids[i])) *out = iface;
    if (!*out) return E_NOINTERFACE;
    IDispatch_AddRef(iface);
    return S_OK;
}
static ULONG WINAPI obj_AddRef(IDispatch *iface) { return InterlockedIncrement(&obj_refs); }
static ULONG WINAPI obj_Release(IDispatch *iface) { return InterlockedDecrement(&obj_refs); }
static HRESULT WINAPI obj_GetTypeInfoCount(IDispatch *iface, UINT *n) { *n = 0; return S_OK; }
static HRESULT WINAPI obj_GetTypeInfo(IDispatch *iface, UINT i, LCID l, ITypeInfo **ti) { return E_NOTIMPL; }
static HRESULT WINAPI obj_GetIDsOfNames(IDispatch *iface, REFIID r, LPOLESTR *n, UINT c, LCID l, DISPID *d) { return E_NOTIMPL; }
static HRESULT WINAPI obj_Invoke(IDispatch *iface, DISPID d, REFIID r, LCID l, WORD f, DISPPARAMS *p, VARIANT *v, EXCEPINFO *e, UINT *a) { return E_NOTIMPL; }
static const IDispatchVtbl obj_vtbl = { obj_QI, obj_AddRef, obj_Release, obj_GetTypeInfoCount, obj_GetTypeInfo, obj_GetIDsOfNames, obj_Invoke };
static IDispatch obj = { (IDispatchVtbl *)&obj_vtbl };

static IStream *streams[2 * NTYPES];
static int first, count;
static double unmarshal_time;
static IUnknown *probe_tl, *probe_ti;

static DWORD WINAPI sta_thread(void *arg)
{
    double t;
    int i;

    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    t = now();
    for (i = first; i < first + count; i++)
    {
        IUnknown *proxy;
            HRESULT hr = CoGetInterfaceAndReleaseStream(streams[i], &iids[i], (void **)&proxy);
        if (FAILED(hr)) { printf("unmarshal %d: %#lx\n", i, hr); continue; }
        if (i == first) printf("proxy %d vtbl %p, [7] %p\n", i, *(void **)proxy, (*(void ***)proxy)[7]);
        if (arg) printf("%s: +proxy: typelib refs %lu, typeinfo refs %lu\n", (char *)arg, refs(probe_tl), refs(probe_ti));
        IUnknown_Release(proxy);
        if (arg) printf("%s: proxy released: typelib refs %lu, typeinfo refs %lu\n", (char *)arg, refs(probe_tl), refs(probe_ti));
    }
    unmarshal_time = now() - t;
    CoUninitialize();
    return 0;
}

static void marshal(int from, int n, double *marshal_time)
{
    double t = now();
    int i;
    for (i = from; i < from + n; i++)
    {
        HRESULT hr = CoMarshalInterThreadInterfaceInStream(&iids[i], (IUnknown *)&obj, &streams[i]);
        if (FAILED(hr)) { printf("marshal %d: %#lx\n", i, hr); exit(1); }
    }
    if (marshal_time) *marshal_time = now() - t;
    first = from; count = n;
}

static void bench(const char *what, int from, int n)
{
    double mt;
    HANDLE th;

    marshal(from, n, &mt);
    th = CreateThread(NULL, 0, sta_thread, NULL, 0, NULL);
    WaitForSingleObject(th, INFINITE);
    CloseHandle(th);
    printf("%-16s %4d IIDs: marshal (stubs) %.3f s, unmarshal (proxies) %.3f s\n", what, n, mt, unmarshal_time);
}

static void probe_proxy(const char *what, int idx)
{
    ITypeLib *tl;
    ITypeInfo *ti;
    HANDLE th;

    CHECK(LoadTypeLibEx(path, REGKIND_NONE, &tl));
    CHECK(ITypeLib_GetTypeInfoOfGuid(tl, &iids[idx], &ti));
    printf("%s: before: typelib refs %lu, typeinfo refs %lu\n", what, refs((IUnknown *)tl), refs((IUnknown *)ti));
    marshal(idx, 1, NULL);
    printf("%s: stub:   typelib refs %lu, typeinfo refs %lu\n", what, refs((IUnknown *)tl), refs((IUnknown *)ti));
    probe_tl = (IUnknown *)tl; probe_ti = (IUnknown *)ti;
    th = CreateThread(NULL, 0, sta_thread, (void *)what, 0, NULL);
    WaitForSingleObject(th, INFINITE);
    CloseHandle(th);
    printf("%s: stub:   typelib refs %lu, typeinfo refs %lu (after proxy gone)\n", what, refs((IUnknown *)tl), refs((IUnknown *)ti));
    ITypeInfo_Release(ti);
    ITypeLib_Release(tl);
}

static void load_cycle(const WCHAR *file)
{
    ITypeLib *a, *b;
    ULONG r;
    double t;
    int i;

    t = now();
    CHECK(LoadTypeLibEx(file, REGKIND_NONE, &a));
    printf("%ls: first load %.3f s, refs %lu\n", file, now() - t, refs((IUnknown *)a));
    r = ITypeLib_Release(a);
    t = now();
    CHECK(LoadTypeLibEx(file, REGKIND_NONE, &b));
    printf("  release -> %lu; reload %.3f s, same pointer %d, refs %lu\n", r, now() - t, a == b, refs((IUnknown *)b));
    ITypeLib_Release(b);
    t = now();
    for (i = 0; i < cycles; i++)
    {
        CHECK(LoadTypeLibEx(file, REGKIND_NONE, &b));
        ITypeLib_Release(b);
    }
    printf("  %d load+release cycles %.3f s\n", cycles, now() - t);
}

int main(int argc, char **argv)
{
    WCHAR copy[MAX_PATH], other[MAX_PATH];
    ITypeLib *tl;
    double t;
    BOOL hold = FALSE, ren = FALSE, keepreg = FALSE, swap = FALSE;
    HRESULT hr;
    int i;

    GetFullPathNameW(L"tlb_cache_bench.tlb", MAX_PATH, path, NULL);
    CoInitializeEx(NULL, COINIT_MULTITHREADED);
    t = now();
    build_typelib();
    printf("built %ls (%d typeinfos x %d funcs) in %.3f s\n", path, 2 * NTYPES, NFUNCS, now() - t);

    /* file still in use after the last release? */
    GetFullPathNameW(L"tlb_cache_copy.tlb", MAX_PATH, copy, NULL);
    CopyFileW(path, copy, FALSE);
    CHECK(LoadTypeLibEx(copy, REGKIND_NONE, &tl));
    printf("copy loaded: delete %d (err %lu)\n", DeleteFileW(copy), GetLastError());
    ITypeLib_Release(tl);
    CopyFileW(path, copy, FALSE);
    CHECK(LoadTypeLibEx(copy, REGKIND_NONE, &tl));
    ITypeLib_Release(tl);
    printf("copy released: delete %d (err %lu)\n", DeleteFileW(copy), GetLastError());
    /* overwrite after the last release: does a reload see the new contents? */
    CopyFileW(path, copy, FALSE);
    CHECK(LoadTypeLibEx(copy, REGKIND_NONE, &tl));
    printf("copy: %u typeinfos", ITypeLib_GetTypeInfoCount(tl));
    ITypeLib_Release(tl);
    GetSystemDirectoryW(other, MAX_PATH);
    wcscat(other, L"\\stdole2.tlb");
    CopyFileW(other, copy, FALSE);
    CHECK(LoadTypeLibEx(copy, REGKIND_NONE, &tl));
    printf(", after release + overwrite with stdole2: %u typeinfos\n", ITypeLib_GetTypeInfoCount(tl));
    ITypeLib_Release(tl);
    DeleteFileW(copy);

    load_cycle(path);
    for (i = 1; i < argc; i++)
    {
        if (!strcmp(argv[i], "hold")) hold = TRUE;
        else if (!strcmp(argv[i], "rename")) ren = TRUE;
        else if (!strcmp(argv[i], "keepreg")) keepreg = TRUE;
        else if (!strcmp(argv[i], "swap")) swap = TRUE;
        else if (!strncmp(argv[i], "n=", 2)) cycles = 0, bench_count = atoi(argv[i] + 2);
        else
        {
            MultiByteToWideChar(CP_ACP, 0, argv[i], -1, other, MAX_PATH);
            load_cycle(other);
        }
    }

    CHECK(LoadTypeLibEx(path, REGKIND_NONE, &tl));
    hr = RegisterTypeLib(tl, path, NULL);
    ITypeLib_Release(tl);
    printf("RegisterTypeLib %#lx\n", hr);
    if (FAILED(hr)) return 1;
    if (swap) /* register dispinterfaces with PSOAInterface and duals with PSDispatch */
    {
        for (i = 0; i < 2 * NTYPES; i++)
        {
            WCHAR key[128], iid[40];
            StringFromGUID2(&iids[i], iid, 40);
            swprintf(key, 128, L"Interface\\%ls\\ProxyStubClsid32", iid);
            StringFromGUID2(i < NTYPES ? &PSOAInterface : &PSDispatch, iid, 40);
            if (RegSetValueW(HKEY_CLASSES_ROOT, key, REG_SZ, iid, 0)) { printf("RegSetValue failed\n"); return 1; }
        }
        printf("swapped ProxyStubClsid32\n");
    }

    probe_proxy("dispinterface", 0);
    probe_proxy("dual", NTYPES);

    if (hold)
    {
        CHECK(LoadTypeLibEx(path, REGKIND_NONE, &tl));
        printf("holding a typelib reference during the benchmark\n");
    }
    if (ren)
    {
        wcscpy(other, path); wcscat(other, L".away");
        printf("typelib file moved away: %d\n", MoveFileExW(path, other, MOVEFILE_REPLACE_EXISTING));
    }

    bench("dispinterfaces", 1, bench_count);
    bench("duals", NTYPES + 1, bench_count);

    if (ren) MoveFileExW(other, path, MOVEFILE_REPLACE_EXISTING);
    if (hold) ITypeLib_Release(tl);
    if (!keepreg) UnRegisterTypeLib(&libid, 1, 0, LOCALE_NEUTRAL, sizeof(void *) == 8 ? SYS_WIN64 : SYS_WIN32);
    CoUninitialize();
    return 0;
}
