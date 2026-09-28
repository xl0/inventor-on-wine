/* Exception thrown inside a COM server method called through a proxy/stub:
 * does COM catch it and return an HRESULT to the caller, or does it stay
 * unhandled? Each case runs in a child process ("stub_exception.exe N MODE")
 * so an unhandled one only kills the child.
 * Cases raise: 0 = MSVC C++ exception code 0xE06D7363 with
 * EXCEPTION_NONCONTINUABLE (as _CxxThrowException does), 1 = same code,
 * continuable, 2 = access violation, 3 = 0x80001234 noncontinuable,
 * 4 = RPC_X_BAD_STUB_DATA continuable.
 * Modes:
 *   disp   cross-apartment IDispatch::Invoke (MTA client, STA server, oleaut32
 *          PSDispatch proxy/stub); prints EXCEPINFO, then a second call
 *   sta    IPersist::GetClassID, MTA client, STA server
 *   mta    same, STA client, MTA server
 *   direct IRpcStubBuffer::Invoke of the IPersist stub called directly
 *          with a fake channel (does the rpcrt4 stub itself catch?)
 *   wrap   "sta" with a custom PS factory whose stub Invoke raises itself
 *          (does the channel catch?)
 * A vectored handler prints each first-chance exception, the unhandled
 * exception filter prints UNHANDLED and exits 3. */
#define COBJMACROS
#include <windows.h>
#include <ole2.h>
#include <stdio.h>

static const struct { DWORD code, flags; } cases[] = {
    {0xe06d7363, EXCEPTION_NONCONTINUABLE}, {0xe06d7363, 0}, {EXCEPTION_ACCESS_VIOLATION, 0},
    {0x80001234, EXCEPTION_NONCONTINUABLE}, {RPC_X_BAD_STUB_DATA, 0},
};
static int which, do_raise = 1;

static void raise(void)
{
    ULONG_PTR args[4] = {0x19930520, 0, 0, 0};
    if (!do_raise) return;
    RaiseException(cases[which].code, cases[which].flags, cases[which].code == 0xe06d7363 ? 4 : 0, args);
}

static HRESULT WINAPI qi(IDispatch *d, REFIID iid, void **out)
{
    if (IsEqualIID(iid, &IID_IUnknown) || IsEqualIID(iid, &IID_IDispatch)) { *out = d; return S_OK; }
    *out = NULL; return E_NOINTERFACE;
}
static ULONG WINAPI addref(IDispatch *d) { return 2; }
static ULONG WINAPI release(IDispatch *d) { return 1; }
static HRESULT WINAPI tic(IDispatch *d, UINT *n) { *n = 0; return S_OK; }
static HRESULT WINAPI ti(IDispatch *d, UINT i, LCID l, ITypeInfo **t) { return E_NOTIMPL; }
static HRESULT WINAPI ids(IDispatch *d, REFIID r, LPOLESTR *n, UINT c, LCID l, DISPID *id) { return E_NOTIMPL; }
static HRESULT WINAPI invoke(IDispatch *d, DISPID id, REFIID r, LCID l, WORD f, DISPPARAMS *p, VARIANT *v,
                             EXCEPINFO *e, UINT *a)
{
    raise();
    return S_OK;
}
static IDispatchVtbl vtbl = {qi, addref, release, tic, ti, ids, invoke};
static IDispatch obj = {&vtbl};

static HRESULT WINAPI ow_qi(IPersist *d, REFIID iid, void **out)
{
    if (IsEqualIID(iid, &IID_IUnknown) || IsEqualIID(iid, &IID_IPersist)) { *out = d; return S_OK; }
    *out = NULL; return E_NOINTERFACE;
}
static ULONG WINAPI ow_addref(IPersist *d) { return 2; }
static ULONG WINAPI ow_release(IPersist *d) { return 1; }
static HRESULT WINAPI ow_getclassid(IPersist *d, CLSID *c) { raise(); c->Data1 = 0x1234; return S_OK; }
static IPersistVtbl ow_vtbl = {ow_qi, ow_addref, ow_release, ow_getclassid};
static IPersist ow = {&ow_vtbl};

/* fake channel for "direct" */
static HRESULT WINAPI ch_qi(IRpcChannelBuffer *c, REFIID r, void **o) { *o = c; return S_OK; }
static ULONG WINAPI ch_addref(IRpcChannelBuffer *c) { return 2; }
static ULONG WINAPI ch_release(IRpcChannelBuffer *c) { return 1; }
static HRESULT WINAPI ch_getbuffer(IRpcChannelBuffer *c, RPCOLEMESSAGE *m, REFIID r)
{
    m->Buffer = HeapAlloc(GetProcessHeap(), 0, m->cbBuffer + 16);
    return S_OK;
}
static HRESULT WINAPI ch_sendreceive(IRpcChannelBuffer *c, RPCOLEMESSAGE *m, ULONG *s) { return E_NOTIMPL; }
static HRESULT WINAPI ch_freebuffer(IRpcChannelBuffer *c, RPCOLEMESSAGE *m) { return S_OK; }
static HRESULT WINAPI ch_getdestctx(IRpcChannelBuffer *c, DWORD *ctx, void **p) { *ctx = MSHCTX_INPROC; *p = NULL; return S_OK; }
static HRESULT WINAPI ch_isconnected(IRpcChannelBuffer *c) { return S_OK; }
static IRpcChannelBufferVtbl ch_vtbl = {ch_qi, ch_addref, ch_release, ch_getbuffer, ch_sendreceive,
                                        ch_freebuffer, ch_getdestctx, ch_isconnected};
static IRpcChannelBuffer chan = {&ch_vtbl};

/* custom PS factory for "wrap": real proxy, stub whose Invoke raises */
static IPSFactoryBuffer *real_psf;
static HRESULT WINAPI sb_qi(IRpcStubBuffer *s, REFIID r, void **o)
{
    if (IsEqualIID(r, &IID_IUnknown) || IsEqualIID(r, &IID_IRpcStubBuffer)) { *o = s; return S_OK; }
    *o = NULL; return E_NOINTERFACE;
}
static ULONG WINAPI sb_addref(IRpcStubBuffer *s) { return 2; }
static ULONG WINAPI sb_release(IRpcStubBuffer *s) { return 1; }
static HRESULT WINAPI sb_connect(IRpcStubBuffer *s, IUnknown *u) { return S_OK; }
static void WINAPI sb_disconnect(IRpcStubBuffer *s) {}
static HRESULT WINAPI sb_invoke(IRpcStubBuffer *s, RPCOLEMESSAGE *m, IRpcChannelBuffer *c)
{
    HRESULT hr;
    raise();
    m->cbBuffer = 8;
    hr = IRpcChannelBuffer_GetBuffer(c, m, &IID_IPersist);
    if (SUCCEEDED(hr)) memset(m->Buffer, 0, 8);
    return hr;
}
static IRpcStubBuffer *WINAPI sb_isiid(IRpcStubBuffer *s, REFIID r) { return NULL; }
static ULONG WINAPI sb_countrefs(IRpcStubBuffer *s) { return 0; }
static HRESULT WINAPI sb_dsqi(IRpcStubBuffer *s, void **p) { return E_NOTIMPL; }
static void WINAPI sb_dsrelease(IRpcStubBuffer *s, void *p) {}
static IRpcStubBufferVtbl sb_vtbl = {sb_qi, sb_addref, sb_release, sb_connect, sb_disconnect, sb_invoke,
                                     sb_isiid, sb_countrefs, sb_dsqi, sb_dsrelease};
static IRpcStubBuffer stubbuf = {&sb_vtbl};
static HRESULT WINAPI ps_qi(IPSFactoryBuffer *p, REFIID r, void **o)
{
    if (IsEqualIID(r, &IID_IUnknown) || IsEqualIID(r, &IID_IPSFactoryBuffer)) { *o = p; return S_OK; }
    *o = NULL; return E_NOINTERFACE;
}
static ULONG WINAPI ps_addref(IPSFactoryBuffer *p) { return 2; }
static ULONG WINAPI ps_release(IPSFactoryBuffer *p) { return 1; }
static HRESULT WINAPI ps_proxy(IPSFactoryBuffer *p, IUnknown *o, REFIID r, IRpcProxyBuffer **pb, void **ppv)
{
    return IPSFactoryBuffer_CreateProxy(real_psf, o, r, pb, ppv);
}
static HRESULT WINAPI ps_stub(IPSFactoryBuffer *p, REFIID r, IUnknown *srv, IRpcStubBuffer **sb)
{
    *sb = &stubbuf;
    return S_OK;
}
static IPSFactoryBufferVtbl ps_vtbl = {ps_qi, ps_addref, ps_release, ps_proxy, ps_stub};
static IPSFactoryBuffer psf = {&ps_vtbl};
static const CLSID CLSID_wrap = {0x5ef5a1a0, 0x1234, 0x4c1e, {0x9a, 0x11, 0x05, 0x60, 0x05, 0x60, 0x00, 0x01}};

static IStream *stream;
static HANDLE ready;
static const IID *server_iid;
static IUnknown *server_obj;

static BOOL wrap;
static void register_wrap(void)
{
    DWORD cookie;
    CLSID cls;
    HRESULT hr;
    if (!wrap) return;
    CoGetPSClsid(&IID_IPersist, &cls);
    if (!real_psf)
    {
        hr = CoGetClassObject(&cls, CLSCTX_INPROC_SERVER, NULL, &IID_IPSFactoryBuffer, (void **)&real_psf);
        if (hr) printf("  get psfactory %08lx\n", hr);
    }
    CoRegisterClassObject(&CLSID_wrap, (IUnknown *)&psf, CLSCTX_INPROC_SERVER, REGCLS_MULTIPLEUSE, &cookie);
    CoRegisterPSClsid(&IID_IPersist, &CLSID_wrap);
}

static DWORD WINAPI server(void *arg)
{
    MSG msg;
    CoInitializeEx(NULL, arg ? COINIT_MULTITHREADED : COINIT_APARTMENTTHREADED);
    register_wrap();
    CoMarshalInterThreadInterfaceInStream(server_iid, server_obj, &stream);
    SetEvent(ready);
    if (arg) Sleep(INFINITE);
    while (GetMessageW(&msg, NULL, 0, 0)) DispatchMessageW(&msg);
    return 0;
}

static LONG WINAPI veh(EXCEPTION_POINTERS *ep)
{
    DWORD code = ep->ExceptionRecord->ExceptionCode;
    if (code == 0x406d1388 || code == 0x40010006 || code == 0x4001000a) return EXCEPTION_CONTINUE_SEARCH;
    printf("  first-chance %08lx flags %lx tid %04lx\n", code, ep->ExceptionRecord->ExceptionFlags,
           GetCurrentThreadId());
    fflush(stdout);
    return EXCEPTION_CONTINUE_SEARCH;
}

static LONG WINAPI unhandled(EXCEPTION_POINTERS *ep)
{
    printf("  UNHANDLED %08lx\n", ep->ExceptionRecord->ExceptionCode);
    fflush(stdout);
    ExitProcess(3);
}

static void *start_server(const IID *iid, IUnknown *o, BOOL mta)
{
    void *proxy = NULL;
    HRESULT hr;
    server_iid = iid;
    server_obj = o;
    ready = CreateEventW(NULL, TRUE, FALSE, NULL);
    CreateThread(NULL, 0, server, (void *)(INT_PTR)mta, 0, NULL);
    WaitForSingleObject(ready, INFINITE);
    hr = CoGetInterfaceAndReleaseStream(stream, iid, &proxy);
    if (hr) printf("  unmarshal %08lx\n", hr);
    return proxy;
}

static int child(const char *mode)
{
    HRESULT hr;
    AddVectoredExceptionHandler(1, veh);
    SetUnhandledExceptionFilter(unhandled);

    if (!strcmp(mode, "disp"))
    {
        DISPPARAMS dp = {0};
        EXCEPINFO ei;
        VARIANT v;
        UINT arg = 0xdead;
        IDispatch *proxy;
        CoInitializeEx(NULL, COINIT_MULTITHREADED);
        proxy = start_server(&IID_IDispatch, (IUnknown *)&obj, FALSE);
        memset(&ei, 0xcc, sizeof(ei));
        VariantInit(&v);
        hr = IDispatch_Invoke(proxy, 1, &IID_NULL, 0, DISPATCH_METHOD, &dp, &v, &ei, &arg);
        printf("  Invoke returned %08lx; excepinfo code %x scode %08lx src %p desc %p; vt %d argerr %x\n", hr,
               ei.wCode, ei.scode, ei.bstrSource, ei.bstrDescription, V_VT(&v), arg);
        do_raise = 0;
        hr = IDispatch_Invoke(proxy, 1, &IID_NULL, 0, DISPATCH_METHOD, &dp, NULL, NULL, NULL);
        printf("  second Invoke %08lx\n", hr);
    }
    else if (!strcmp(mode, "sta") || !strcmp(mode, "mta") || !strcmp(mode, "wrap"))
    {
        BOOL mta = !strcmp(mode, "mta");
        IPersist *proxy;
        CLSID clsid;
        CoInitializeEx(NULL, mta ? COINIT_APARTMENTTHREADED : COINIT_MULTITHREADED);
        wrap = !strcmp(mode, "wrap");
        register_wrap();
        proxy = start_server(&IID_IPersist, (IUnknown *)&ow, mta);
        memset(&clsid, 0xcc, sizeof(clsid));
        hr = IPersist_GetClassID(proxy, &clsid);
        printf("  GetClassID returned %08lx clsid.Data1 %08lx\n", hr, clsid.Data1);
        do_raise = 0;
        hr = IPersist_GetClassID(proxy, &clsid);
        printf("  second GetClassID %08lx clsid.Data1 %08lx\n", hr, clsid.Data1);
    }
    else if (!strcmp(mode, "direct"))
    {
        IPSFactoryBuffer *ps;
        IRpcStubBuffer *stub;
        RPCOLEMESSAGE msg;
        CLSID cls;
        CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
        CoGetPSClsid(&IID_IPersist, &cls);
        hr = CoGetClassObject(&cls, CLSCTX_INPROC_SERVER, NULL, &IID_IPSFactoryBuffer, (void **)&ps);
        if (hr) printf("  get psfactory %08lx\n", hr);
        hr = IPSFactoryBuffer_CreateStub(ps, &IID_IPersist, (IUnknown *)&ow, &stub);
        if (hr) printf("  CreateStub %08lx\n", hr);
        memset(&msg, 0, sizeof(msg));
        msg.dataRepresentation = NDR_LOCAL_DATA_REPRESENTATION;
        msg.iMethod = 3;
        hr = IRpcStubBuffer_Invoke(stub, &msg, &chan);
        printf("  stub Invoke returned %08lx\n", hr);
    }
    fflush(stdout);
    return 0;
}

int main(int argc, char **argv)
{
    static const char *modes[] = {"disp", "sta", "mta", "direct", "wrap"};
    char cmd[MAX_PATH + 32], self[MAX_PATH];
    int i, m;

    if (argc > 2)
    {
        which = atoi(argv[1]);
        return child(argv[2]);
    }
    GetModuleFileNameA(NULL, self, MAX_PATH);
    SetErrorMode(SEM_NOGPFAULTERRORBOX);
    for (m = 0; m < ARRAYSIZE(modes); m++)
    for (i = 0; i < ARRAYSIZE(cases); i++)
    {
        STARTUPINFOA si = {sizeof(si)};
        PROCESS_INFORMATION pi;
        DWORD code;
        if (argc > 1 && strcmp(argv[1], modes[m])) continue;
        printf("%s case %d: code %08lx flags %lx:\n", modes[m], i, cases[i].code, cases[i].flags);
        fflush(stdout);
        sprintf(cmd, "\"%s\" %d %s", self, i, modes[m]);
        CreateProcessA(NULL, cmd, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi);
        if (WaitForSingleObject(pi.hProcess, 30000)) { printf("  child timeout\n"); TerminateProcess(pi.hProcess, 1); }
        GetExitCodeProcess(pi.hProcess, &code);
        printf("  child exit %08lx\n", code);
    }
    return 0;
}
