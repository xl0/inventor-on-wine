/* com_rundown.exe run MODE [clients] [children] [monitor_s]: does COM release the
 * references of an out-of-process client that exits without releasing them?
 * The server (this process, STA with a message loop) table-marshals a class factory to a
 * file; each client process unmarshals it, creates `children` objects through it (each
 * returned as a NORMAL-marshaled proxy), QIs each for IClassFactory, then ends by MODE:
 *   kill    TerminateProcess(self) holding everything
 *   exit    ExitProcess(0) holding everything, no CoUninitialize
 *   uninit  CoUninitialize() without releasing, then exit
 *   release release everything, CoUninitialize, exit
 * The server prints every 15 s: live child objects, strong external connections of the
 * children, process handle count. Build: x86_64-w64-mingw32-gcc -O2 -o com_rundown.exe
 * com_rundown.c -lole32 -luuid */
#define COBJMACROS
#include <windows.h>
#include <objbase.h>
#include <stdio.h>
#include <stdlib.h>

static LONG live, strong;

struct obj { IClassFactory IClassFactory_iface; IExternalConnection IExternalConnection_iface; LONG refs; BOOL child; };

static struct obj *from_cf(IClassFactory *iface) { return CONTAINING_RECORD(iface, struct obj, IClassFactory_iface); }
static struct obj *from_ec(IExternalConnection *iface) { return CONTAINING_RECORD(iface, struct obj, IExternalConnection_iface); }

static HRESULT WINAPI cf_QueryInterface(IClassFactory *iface, REFIID iid, void **out)
{
    struct obj *o = from_cf(iface);
    if (IsEqualIID(iid, &IID_IUnknown) || IsEqualIID(iid, &IID_IClassFactory)) *out = iface;
    else if (IsEqualIID(iid, &IID_IExternalConnection) && o->child) *out = &o->IExternalConnection_iface;
    else { *out = NULL; return E_NOINTERFACE; }
    IUnknown_AddRef((IUnknown *)*out);
    return S_OK;
}
static ULONG WINAPI cf_AddRef(IClassFactory *iface) { return InterlockedIncrement(&from_cf(iface)->refs); }
static ULONG WINAPI cf_Release(IClassFactory *iface)
{
    struct obj *o = from_cf(iface);
    ULONG r = InterlockedDecrement(&o->refs);
    if (!r) { if (o->child) InterlockedDecrement(&live); free(o); }
    return r;
}
static const IClassFactoryVtbl cf_vtbl;
static HRESULT WINAPI cf_CreateInstance(IClassFactory *iface, IUnknown *outer, REFIID iid, void **out)
{
    struct obj *o = calloc(1, sizeof(*o));
    extern const IExternalConnectionVtbl ec_vtbl;
    HRESULT hr;
    o->IClassFactory_iface.lpVtbl = &cf_vtbl;
    o->IExternalConnection_iface.lpVtbl = &ec_vtbl;
    o->refs = 1;
    o->child = TRUE;
    InterlockedIncrement(&live);
    hr = IClassFactory_QueryInterface(&o->IClassFactory_iface, iid, out);
    IClassFactory_Release(&o->IClassFactory_iface);
    return hr;
}
static HRESULT WINAPI cf_LockServer(IClassFactory *iface, BOOL lock) { return S_OK; }
static const IClassFactoryVtbl cf_vtbl = { cf_QueryInterface, cf_AddRef, cf_Release, cf_CreateInstance, cf_LockServer };

static HRESULT WINAPI ec_QueryInterface(IExternalConnection *iface, REFIID iid, void **out)
{ return cf_QueryInterface(&from_ec(iface)->IClassFactory_iface, iid, out); }
static ULONG WINAPI ec_AddRef(IExternalConnection *iface) { return cf_AddRef(&from_ec(iface)->IClassFactory_iface); }
static ULONG WINAPI ec_Release(IExternalConnection *iface) { return cf_Release(&from_ec(iface)->IClassFactory_iface); }
static DWORD WINAPI ec_AddConnection(IExternalConnection *iface, DWORD type, DWORD reserved)
{ if (type == EXTCONN_STRONG) InterlockedIncrement(&strong); return 0; }
static DWORD WINAPI ec_ReleaseConnection(IExternalConnection *iface, DWORD type, DWORD reserved, BOOL last)
{ if (type == EXTCONN_STRONG) InterlockedDecrement(&strong); return 0; }
const IExternalConnectionVtbl ec_vtbl = { ec_QueryInterface, ec_AddRef, ec_Release, ec_AddConnection, ec_ReleaseConnection };

static DWORD start;
static void sample(const char *what)
{
    DWORD handles = 0;
    GetProcessHandleCount(GetCurrentProcess(), &handles);
    printf("%6.1f %-10s live %ld strong %ld handles %lu\n", (GetTickCount() - start) / 1000.0, what, live, strong, handles);
    fflush(stdout);
}

/* wait for a handle while pumping messages (STA server) */
static void pump(HANDLE h, DWORD ms)
{
    DWORD end = GetTickCount() + ms, now;
    MSG msg;
    while ((now = GetTickCount()) < end || !ms)
    {
        DWORD r = MsgWaitForMultipleObjects(h ? 1 : 0, &h, FALSE, ms ? end - now : INFINITE, QS_ALLINPUT);
        if (r == WAIT_OBJECT_0 && h) return;
        if (r == WAIT_TIMEOUT) return;
        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) DispatchMessageW(&msg);
    }
}

static char file[MAX_PATH];

static int client(const char *mode, int n)
{
    IClassFactory *cf, *sub;
    IUnknown **objs = calloc(n, sizeof(*objs));
    IStream *stream;
    HGLOBAL mem;
    HANDLE f;
    DWORD size, rd;
    HRESULT hr;
    int i;

    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    f = CreateFileA(file, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    size = GetFileSize(f, NULL);
    mem = GlobalAlloc(GMEM_MOVEABLE, size);
    ReadFile(f, GlobalLock(mem), size, &rd, NULL);
    GlobalUnlock(mem);
    CloseHandle(f);
    CreateStreamOnHGlobal(mem, TRUE, &stream);
    hr = CoUnmarshalInterface(stream, &IID_IClassFactory, (void **)&cf);
    if (hr != S_OK) { printf("client: CoUnmarshalInterface %#lx\n", hr); return 1; }
    IStream_Release(stream);
    for (i = 0; i < n; i++)
    {
        if ((hr = IClassFactory_CreateInstance(cf, NULL, &IID_IUnknown, (void **)&objs[i])) != S_OK)
        { printf("client: CreateInstance %#lx\n", hr); return 1; }
        IUnknown_QueryInterface(objs[i], &IID_IClassFactory, (void **)&sub); /* second ifstub, refs kept */
    }
    if (!strcmp(mode, "kill")) TerminateProcess(GetCurrentProcess(), 0);
    if (!strcmp(mode, "exit")) ExitProcess(0);
    if (!strcmp(mode, "release"))
    {
        for (i = 0; i < n; i++) { IUnknown_QueryInterface(objs[i], &IID_IClassFactory, (void **)&sub);
            IClassFactory_Release(sub); IClassFactory_Release(sub); IUnknown_Release(objs[i]); }
        IClassFactory_Release(cf);
    }
    CoUninitialize();
    return 0;
}

int main(int argc, char **argv)
{
    int clients, children, monitor, i;
    char cmd[2 * MAX_PATH];
    IStream *stream;
    IClassFactory *cf;
    struct obj *o;
    HGLOBAL mem;
    HANDLE f;
    DWORD wr;

    GetTempPathA(MAX_PATH, file);
    if (argc > 4 && !strcmp(argv[1], "client"))
    {
        sprintf(file + strlen(file), "com_rundown-%s.bin", argv[4]);
        return client(argv[2], atoi(argv[3]));
    }
    sprintf(file + strlen(file), "com_rundown-%lu.bin", GetCurrentProcessId());
    if (argc < 3 || strcmp(argv[1], "run")) { printf("usage: run kill|exit|uninit|release [clients] [children] [monitor_s]\n"); return 2; }
    clients = argc > 3 ? atoi(argv[3]) : 10;
    children = argc > 4 ? atoi(argv[4]) : 20;
    monitor = argc > 5 ? atoi(argv[5]) : 720;
    start = GetTickCount();

    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    o = calloc(1, sizeof(*o));
    o->IClassFactory_iface.lpVtbl = &cf_vtbl;
    o->refs = 1;
    cf = &o->IClassFactory_iface;
    CreateStreamOnHGlobal(NULL, TRUE, &stream);
    CoMarshalInterface(stream, &IID_IClassFactory, (IUnknown *)cf, MSHCTX_LOCAL, NULL, MSHLFLAGS_TABLESTRONG);
    GetHGlobalFromStream(stream, &mem);
    f = CreateFileA(file, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    WriteFile(f, GlobalLock(mem), GlobalSize(mem), &wr, NULL);
    GlobalUnlock(mem);
    CloseHandle(f);
    sample("start");

    for (i = 0; i < clients; i++)
    {
        STARTUPINFOA si = {sizeof(si)};
        PROCESS_INFORMATION pi;
        sprintf(cmd, "\"%s\" client %s %d %lu", argv[0], argv[2], children, GetCurrentProcessId());
        if (!CreateProcessA(NULL, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) { printf("CreateProcess %lu\n", GetLastError()); return 1; }
        pump(pi.hProcess, 0);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        if (i == 0) sample("client1");
    }
    sample("clients");
    for (i = 15; i <= monitor; i += 15)
    {
        pump(NULL, 15000);
        sample("wait");
        if (!live) break;
    }
    return 0;
}
