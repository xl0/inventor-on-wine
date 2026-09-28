/* A C++ exception thrown inside a catch block (MSVC EH, __CxxFrameHandler3)
 * must reach handlers further up: a C++ catch, a native __except, and COM's
 * handler around a cross-apartment call (IDispatch::Invoke via PSDispatch, and
 * IPersist::GetClassID via the combase proxy/stub). Issue 058.
 * Build (MSVC EH half with clang 14, which emits FH3; harness with mingw):
 *   clang -target x86_64-pc-windows-msvc -O1 -fexceptions -fcxx-exceptions -c cxx_catch_throw_eh.cpp -o eh.obj
 *   printf 'LIBRARY vcruntime140.dll\nEXPORTS\n_CxxThrowException\n__CxxFrameHandler3\n__C_specific_handler\n__std_terminate\n' > vcr.def
 *   x86_64-w64-mingw32-dlltool -d vcr.def -l libvcr.a
 *   printf '.data\n.globl "??_7type_info@@6B@"\n"??_7type_info@@6B@": .quad 0,0\n' > tivt.s
 *   x86_64-w64-mingw32-gcc -O2 -o cxx_catch_throw.exe cxx_catch_throw.c eh.obj tivt.s libvcr.a -lole32 -loleaut32 -luuid
 * (Windows' vcruntime140 doesn't export the type_info vtable; EH only compares names.)
 * Usage: cxx_catch_throw.exe [OUTER [MODE]]; OUTER = cxx seh disp stddisp sta,
 * MODE = 0 plain throw, 1 rethrow in catch, 2 new throw in catch, 3 throw from a callee of catch,
 * 4 callee of catch catches and rethrows.
 * Each case runs in a child; an unhandled exception prints UNHANDLED (exit 3). */
#define COBJMACROS
#include <windows.h>
#include <ole2.h>
#include <stdio.h>

void cxx_throw(int mode);
int cxx_throw_seh(int mode);
int cxx_throw_catch(int mode);

static int mode;

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
    if (mode >= 0) cxx_throw(mode);
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
static HRESULT WINAPI ow_getclassid(IPersist *d, CLSID *c)
{
    if (mode >= 0) cxx_throw(mode);
    c->Data1 = 0x1234;
    return S_OK;
}
static IPersistVtbl ow_vtbl = {ow_qi, ow_addref, ow_release, ow_getclassid};
static IPersist ow = {&ow_vtbl};

/* "stddisp": CreateStdDispatch over a plain vtable, so the call goes through
 * ITypeInfo::Invoke / DispCallFunc like Inventor's typelib-driven methods */
typedef struct { HRESULT (WINAPI *Run)(void *); } RunnerVtbl;
typedef struct { const RunnerVtbl *lpVtbl; } Runner;
static HRESULT WINAPI runner_run(void *This)
{
    if (mode >= 0) cxx_throw(mode);
    return S_OK;
}
static const RunnerVtbl runner_vtbl = {runner_run};
static Runner runner = {&runner_vtbl};

static IUnknown *create_stddisp(void)
{
    static METHODDATA md = {(OLECHAR *)L"Run", NULL, 1, 0, CC_STDCALL, 0, DISPATCH_METHOD, VT_ERROR};
    static INTERFACEDATA id = {&md, 1};
    ITypeInfo *ti;
    IUnknown *unk;
    HRESULT hr;
    if ((hr = CreateDispTypeInfo(&id, LOCALE_SYSTEM_DEFAULT, &ti))) printf("  CreateDispTypeInfo %08lx\n", hr);
    if ((hr = CreateStdDispatch(NULL, &runner, ti, &unk))) printf("  CreateStdDispatch %08lx\n", hr);
    return unk;
}

static IStream *stream;
static HANDLE ready;
static const IID *server_iid;
static IUnknown *server_obj;

static DWORD WINAPI server(void *arg)
{
    MSG msg;
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    CoMarshalInterThreadInterfaceInStream(server_iid, server_obj, &stream);
    SetEvent(ready);
    while (GetMessageW(&msg, NULL, 0, 0)) DispatchMessageW(&msg);
    return 0;
}

static void *start_server(const IID *iid, IUnknown *o)
{
    void *proxy = NULL;
    HRESULT hr;
    server_iid = iid;
    server_obj = o;
    ready = CreateEventW(NULL, TRUE, FALSE, NULL);
    CreateThread(NULL, 0, server, NULL, 0, NULL);
    WaitForSingleObject(ready, INFINITE);
    hr = CoGetInterfaceAndReleaseStream(stream, iid, &proxy);
    if (hr) printf("  unmarshal %08lx\n", hr);
    return proxy;
}

static LONG WINAPI unhandled(EXCEPTION_POINTERS *ep)
{
    printf("  UNHANDLED %08lx\n", ep->ExceptionRecord->ExceptionCode);
    fflush(stdout);
    ExitProcess(3);
}

static void child(const char *outer)
{
    HRESULT hr;
    SetUnhandledExceptionFilter(unhandled);
    if (!strcmp(outer, "cxx")) printf("  caught %d\n", cxx_throw_catch(mode));
    else if (!strcmp(outer, "seh")) printf("  __except %d\n", cxx_throw_seh(mode));
    else if (!strcmp(outer, "disp"))
    {
        DISPPARAMS dp = {0};
        IDispatch *proxy;
        CoInitializeEx(NULL, COINIT_MULTITHREADED);
        proxy = start_server(&IID_IDispatch, (IUnknown *)&obj);
        hr = IDispatch_Invoke(proxy, 1, &IID_NULL, 0, DISPATCH_METHOD, &dp, NULL, NULL, NULL);
        printf("  Invoke %08lx\n", hr);
        mode = -1;
        hr = IDispatch_Invoke(proxy, 1, &IID_NULL, 0, DISPATCH_METHOD, &dp, NULL, NULL, NULL);
        printf("  second Invoke %08lx\n", hr);
    }
    else if (!strcmp(outer, "stddisp"))
    {
        DISPPARAMS dp = {0};
        IDispatch *proxy;
        CoInitializeEx(NULL, COINIT_MULTITHREADED);
        proxy = start_server(&IID_IDispatch, create_stddisp());
        hr = IDispatch_Invoke(proxy, 1, &IID_NULL, 0, DISPATCH_METHOD, &dp, NULL, NULL, NULL);
        printf("  Invoke %08lx\n", hr);
        mode = -1;
        hr = IDispatch_Invoke(proxy, 1, &IID_NULL, 0, DISPATCH_METHOD, &dp, NULL, NULL, NULL);
        printf("  second Invoke %08lx\n", hr);
    }
    else if (!strcmp(outer, "sta"))
    {
        IPersist *proxy;
        CLSID clsid;
        CoInitializeEx(NULL, COINIT_MULTITHREADED);
        proxy = start_server(&IID_IPersist, (IUnknown *)&ow);
        hr = IPersist_GetClassID(proxy, &clsid);
        printf("  GetClassID %08lx\n", hr);
        mode = -1;
        hr = IPersist_GetClassID(proxy, &clsid);
        printf("  second GetClassID %08lx\n", hr);
    }
    fflush(stdout);
}

int main(int argc, char **argv)
{
    static const char *outers[] = {"cxx", "seh", "disp", "stddisp", "sta"};
    char cmd[MAX_PATH + 32], self[MAX_PATH];
    int i, m;

    if (argc > 3)
    {
        mode = atoi(argv[2]);
        child(argv[1]);
        return 0;
    }
    GetModuleFileNameA(NULL, self, MAX_PATH);
    SetErrorMode(SEM_NOGPFAULTERRORBOX);
    for (i = 0; i < ARRAYSIZE(outers); i++)
    for (m = 0; m < 5; m++)
    {
        STARTUPINFOA si = {sizeof(si)};
        PROCESS_INFORMATION pi;
        DWORD code;
        if (argc > 1 && strcmp(argv[1], outers[i])) continue;
        if (argc > 2 && atoi(argv[2]) != m) continue;
        printf("%s mode %d:\n", outers[i], m);
        fflush(stdout);
        sprintf(cmd, "\"%s\" %s %d child", self, outers[i], m);
        CreateProcessA(NULL, cmd, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi);
        if (WaitForSingleObject(pi.hProcess, 30000)) { printf("  child timeout\n"); TerminateProcess(pi.hProcess, 1); }
        GetExitCodeProcess(pi.hProcess, &code);
        printf("  child exit %08lx\n", code);
    }
    return 0;
}
