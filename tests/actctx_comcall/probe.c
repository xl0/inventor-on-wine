/* DLL for the actctx_comcall probe (070). Its manifest (RT_MANIFEST 2, ISOLATIONAWARE) declares
 * CLSID_Target (plain IUnknown) and CLSID_Server (IDispatch; Invoke(1) runs probe_test on the
 * calling thread and returns the report as a BSTR). Neither class is in the registry.
 * Build: see actctx_comcall.c. */
#define COBJMACROS
#include <windows.h>
#include <ole2.h>
#include <stdio.h>

static const GUID CLSID_Target = {0xa9485c80,0,0x4000,{0x80,0,0,0,0,0,0,1}};
static const GUID CLSID_Server = {0xa9485c80,0,0x4000,{0x80,0,0,0,0,0,0,2}};
static HMODULE module;
static HANDLE create_ctx;   /* context active when the last server object was created */

static void ctx_name(HANDLE ctx, WCHAR *out)
{
    union { ACTIVATION_CONTEXT_DETAILED_INFORMATION info; char buf[4096]; } u;
    const WCHAR *p;
    if (!ctx) { wcscpy(out, L"none"); return; }
    if (!QueryActCtxW(0, ctx, NULL, ActivationContextDetailedInformation, &u, sizeof(u), NULL))
    { swprintf(out, 64, L"query-err%lu", GetLastError()); return; }
    p = u.info.lpRootManifestPath ? u.info.lpRootManifestPath : L"(null)";
    if (wcsrchr(p, '\\')) p = wcsrchr(p, '\\') + 1;
    swprintf(out, 260, L"%ls", p);
}

/* Reports the current thread's context and whether CLSID_Target resolves. */
__declspec(dllexport) void probe_test(WCHAR *out)
{
    WCHAR name[260];
    IUnknown *unk = NULL;
    HANDLE ctx = NULL;
    HRESULT hr;
    GetCurrentActCtx(&ctx);
    ctx_name(ctx, name);
    hr = CoCreateInstance(&CLSID_Target, NULL, CLSCTX_INPROC_SERVER | CLSCTX_LOCAL_SERVER | CLSCTX_REMOTE_SERVER | CLSCTX_INPROC_HANDLER,
                          &IID_IUnknown, (void **)&unk);
    swprintf(out, 512, L"tid %lu ctx %ls%ls hr %08lx", GetCurrentThreadId(), name,
             ctx && ctx == create_ctx ? L" (=creation ctx)" : L"", hr);
    if (unk) IUnknown_Release(unk);
    if (ctx) ReleaseActCtx(ctx);
}

struct obj { IDispatch IDispatch_iface; LONG ref; BOOL disp; };

static HRESULT WINAPI obj_QI(IDispatch *iface, REFIID riid, void **out)
{
    struct obj *o = CONTAINING_RECORD(iface, struct obj, IDispatch_iface);
    if (IsEqualIID(riid, &IID_IUnknown) || (o->disp && IsEqualIID(riid, &IID_IDispatch)))
    { *out = iface; IDispatch_AddRef(iface); return S_OK; }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI obj_AddRef(IDispatch *iface)
{ return InterlockedIncrement(&CONTAINING_RECORD(iface, struct obj, IDispatch_iface)->ref); }
static ULONG WINAPI obj_Release(IDispatch *iface)
{
    struct obj *o = CONTAINING_RECORD(iface, struct obj, IDispatch_iface);
    ULONG r = InterlockedDecrement(&o->ref);
    if (!r) HeapFree(GetProcessHeap(), 0, o);
    return r;
}
static HRESULT WINAPI obj_GetTypeInfoCount(IDispatch *iface, UINT *n) { *n = 0; return S_OK; }
static HRESULT WINAPI obj_GetTypeInfo(IDispatch *iface, UINT i, LCID l, ITypeInfo **ti) { return E_NOTIMPL; }
static HRESULT WINAPI obj_GetIDsOfNames(IDispatch *iface, REFIID r, LPOLESTR *n, UINT c, LCID l, DISPID *id) { return E_NOTIMPL; }
static HRESULT WINAPI obj_Invoke(IDispatch *iface, DISPID id, REFIID riid, LCID lcid, WORD flags,
                                 DISPPARAMS *params, VARIANT *res, EXCEPINFO *ei, UINT *argerr)
{
    WCHAR buf[512];
    probe_test(buf);
    if (res) { V_VT(res) = VT_BSTR; V_BSTR(res) = SysAllocString(buf); }
    return S_OK;
}
static IDispatchVtbl obj_vtbl = { obj_QI, obj_AddRef, obj_Release, obj_GetTypeInfoCount,
                                        obj_GetTypeInfo, obj_GetIDsOfNames, obj_Invoke };

static IDispatch *new_obj(BOOL disp)
{
    struct obj *o = HeapAlloc(GetProcessHeap(), 0, sizeof(*o));
    o->IDispatch_iface.lpVtbl = &obj_vtbl;
    o->ref = 1;
    o->disp = disp;
    if (disp)
    {
        if (create_ctx) ReleaseActCtx(create_ctx);
        create_ctx = NULL;
        GetCurrentActCtx(&create_ctx);
    }
    return &o->IDispatch_iface;
}

/* Creates a server object directly, without COM. */
__declspec(dllexport) IDispatch *probe_create(void) { return new_obj(TRUE); }

static HRESULT WINAPI cf_QI(IClassFactory *iface, REFIID riid, void **out)
{
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IClassFactory))
    { *out = iface; return S_OK; }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI cf_AddRef(IClassFactory *iface) { return 2; }
static ULONG WINAPI cf_Release(IClassFactory *iface) { return 1; }
static HRESULT WINAPI cf_LockServer(IClassFactory *iface, BOOL lock) { return S_OK; }
static HRESULT WINAPI cf_CreateInstance(IClassFactory *iface, IUnknown *outer, REFIID riid, void **out);
static IClassFactoryVtbl cf_vtbl = { cf_QI, cf_AddRef, cf_Release, cf_CreateInstance, cf_LockServer };
static IClassFactory cf_target = { &cf_vtbl }, cf_server = { &cf_vtbl };

static HRESULT WINAPI cf_CreateInstance(IClassFactory *iface, IUnknown *outer, REFIID riid, void **out)
{
    IDispatch *d = new_obj(iface == &cf_server);
    HRESULT hr = IDispatch_QueryInterface(d, riid, out);
    IDispatch_Release(d);
    return hr;
}

HRESULT WINAPI DllGetClassObject(REFCLSID clsid, REFIID riid, void **out)
{
    if (IsEqualCLSID(clsid, &CLSID_Target)) return IClassFactory_QueryInterface(&cf_target, riid, out);
    if (IsEqualCLSID(clsid, &CLSID_Server)) return IClassFactory_QueryInterface(&cf_server, riid, out);
    return CLASS_E_CLASSNOTAVAILABLE;
}

HRESULT WINAPI DllCanUnloadNow(void) { return S_FALSE; }

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, void *reserved)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        WCHAR buf[512];
        module = inst;
        DisableThreadLibraryCalls(inst);
        /* no COM here: just the context the loader gives DllMain */
        {
            HANDLE ctx = NULL; WCHAR name[260];
            GetCurrentActCtx(&ctx); ctx_name(ctx, name);
            swprintf(buf, 512, L"DllMain ctx %ls", name);
            printf("%ls\n", buf);
            if (ctx) ReleaseActCtx(ctx);
        }
    }
    return TRUE;
}
