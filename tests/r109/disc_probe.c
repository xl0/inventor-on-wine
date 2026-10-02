/* disc_probe.exe MODE: A (IPersist, STA in a child process) disconnects itself in its first call and pumps.
 * newcall: a second client thread calls A while the first call is in progress.
 * double:  A calls CoDisconnectObject(A) twice in its call.
 * relmd:   a second client thread does CoReleaseMarshalData of A's objref (final RemRelease) during the call.
 * Prints HRESULTs; the server reports whether it survived. */
#define COBJMACROS
#include <windows.h>
#include <objbase.h>
#include <stdio.h>
#include <string.h>

struct shared { DWORD size; BYTE data[1024]; HRESULT wait_hr; LONG calls; LONG alive; };
static struct shared *sh;
static HANDLE ready, in_call, released, quit;
static char mode[32];
static IPersist obj;
static LONG refs = 1;

static HRESULT WINAPI qi(IPersist *iface, REFIID iid, void **out)
{
    if (IsEqualGUID(iid, &IID_IUnknown) || IsEqualGUID(iid, &IID_IPersist)) { *out = iface; IPersist_AddRef(iface); return S_OK; }
    *out = NULL; return E_NOINTERFACE;
}
static ULONG WINAPI addref(IPersist *iface) { return InterlockedIncrement(&refs); }
static ULONG WINAPI release(IPersist *iface)
{
    LONG r = InterlockedDecrement(&refs);
    if (r < 0) printf("server: object over-released (%ld)\n", r);
    return r;
}
static HRESULT WINAPI getclassid(IPersist *iface, CLSID *clsid)
{
    DWORD index;
    memset(clsid, 0, sizeof(*clsid));
    if (InterlockedIncrement(&sh->calls) != 1) return S_OK;
    CoDisconnectObject((IUnknown *)iface, 0);
    if (!strcmp(mode, "double")) CoDisconnectObject((IUnknown *)iface, 0);
    SetEvent(in_call);
    sh->wait_hr = CoWaitForMultipleHandles(0, 10000, 1, &released, &index);
    printf("server: refs on object at end of call %ld\n", refs);
    return S_OK;
}
static IPersistVtbl vtbl = { qi, addref, release, getclassid };

static void server(void)
{
    IStream *stream; HGLOBAL hglobal; MSG msg;
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    obj.lpVtbl = &vtbl;
    CreateStreamOnHGlobal(NULL, TRUE, &stream);
    CoMarshalInterface(stream, &IID_IPersist, (IUnknown *)&obj, MSHCTX_LOCAL, NULL, MSHLFLAGS_NORMAL);
    GetHGlobalFromStream(stream, &hglobal);
    sh->size = GlobalSize(hglobal);
    memcpy(sh->data, GlobalLock(hglobal), sh->size);
    GlobalUnlock(hglobal);
    IStream_Release(stream);
    SetEvent(ready);
    while (MsgWaitForMultipleObjects(1, &quit, FALSE, INFINITE, QS_ALLINPUT) == WAIT_OBJECT_0 + 1)
        while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) DispatchMessageA(&msg);
    printf("server: refs on object before CoUninitialize %ld\n", refs);
    CoUninitialize();
    sh->alive = 1;
    fflush(stdout);
}

static IStream *objref_stream(void)
{
    IStream *stream;
    CreateStreamOnHGlobal(NULL, TRUE, &stream);
    IStream_Write(stream, sh->data, sh->size, NULL);
    IStream_Seek(stream, (LARGE_INTEGER){{0}}, STREAM_SEEK_SET, NULL);
    return stream;
}

static HRESULT call_hr, call2_hr = 0xdead;
static DWORD WINAPI call_thread(void *p) { CLSID c; call_hr = IPersist_GetClassID((IPersist *)p, &c); return 0; }
static DWORD WINAPI call2_thread(void *p)
{
    CLSID c;
    CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (!strcmp(mode, "relmd"))
    {
        IStream *s = objref_stream();
        call2_hr = CoReleaseMarshalData(s);
        IStream_Release(s);
    }
    else if (!strcmp(mode, "rel"))
        call2_hr = IPersist_Release((IPersist *)p);
    else call2_hr = IPersist_GetClassID((IPersist *)p, &c);
    return 0;
}

int main(int argc, char **argv)
{
    HANDLE mapping, call, call2;
    PROCESS_INFORMATION pi;
    STARTUPINFOA si = {sizeof(si)};
    IPersist *a = NULL;
    IStream *s;
    char cmd[MAX_PATH + 64];
    CLSID c;
    DWORD code, t;
    HRESULT hr;

    mapping = CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, sizeof(*sh), "disc_probe");
    sh = MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(*sh));
    ready = CreateEventA(NULL, FALSE, FALSE, "disc_probe ready");
    in_call = CreateEventA(NULL, FALSE, FALSE, "disc_probe in_call");
    released = CreateEventA(NULL, FALSE, FALSE, "disc_probe released");
    quit = CreateEventA(NULL, FALSE, FALSE, "disc_probe quit");
    setvbuf(stdout, NULL, _IONBF, 0);

    if (argc > 2 && !strcmp(argv[1], "server")) { strcpy(mode, argv[2]); server(); return 0; }
    strcpy(mode, argc > 1 ? argv[1] : "newcall");
    sprintf(cmd, "\"%s\" server %s", argv[0], mode);
    CreateProcessA(argv[0], cmd, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi);
    if (WaitForSingleObject(ready, 10000)) { printf("server not ready\n"); return 2; }

    CoInitializeEx(NULL, COINIT_MULTITHREADED);
    s = objref_stream();
    hr = CoUnmarshalInterface(s, &IID_IPersist, (void **)&a);
    IStream_Release(s);
    if (hr) { printf("unmarshal %#lx\n", hr); return 2; }
    call = CreateThread(NULL, 0, call_thread, a, 0, NULL);
    if (WaitForSingleObject(in_call, 10000)) { printf("first call not received\n"); return 2; }
    t = GetTickCount();
    call2 = CreateThread(NULL, 0, call2_thread, a, 0, NULL);
    if (WaitForSingleObject(call2, 5000)) printf("%s: second op hangs\n", mode);
    else printf("%s: second op %#lx (%lu ms), server calls %ld\n", mode, call2_hr, GetTickCount() - t, sh->calls);
    SetEvent(released);
    if (WaitForSingleObject(call, 15000)) printf("first call hangs\n");
    else printf("first call %#lx, server wait %#lx\n", call_hr, sh->wait_hr);
    if (strcmp(mode, "rel"))
    {
        hr = IPersist_GetClassID(a, &c);
        printf("third call %#lx\n", hr);
        IPersist_Release(a);
    }
    SetEvent(quit);
    if (WaitForSingleObject(pi.hProcess, 10000)) printf("server hangs\n");
    GetExitCodeProcess(pi.hProcess, &code);
    printf("server exit %#lx, clean CoUninitialize %ld\n", code, sh->alive);
    return 0;
}
