/* com_disconnect_wait.exe: can the last release of an object's interface be handled by an STA
 * while that STA is inside a call on another object of the same interface (109)?
 * Server (child process, STA): objects A and B, both IPersist, marshaled NORMAL. A::GetClassID
 * optionally calls CoDisconnectObject(A) ("disconnect" arg), signals the client, then pumps in
 * CoWaitForMultipleHandles until the client has released B. The client (MTA) calls A, waits
 * for the signal, releases its only proxy of B (RemRelease, handled nested in A's call) and
 * reports how long that took and what A's call returned.
 * Wine: B's stub was the last registered IPersist stub (A's registration went with the disconnect),
 * so its deletion waited for A's call, i.e. for the STA itself: the release hangs.
 * Usage: com_disconnect_wait.exe [disconnect]   (child: com_disconnect_wait.exe server [disconnect])
 * Build: x86_64-w64-mingw32-gcc -O2 -o com_disconnect_wait.exe com_disconnect_wait.c -lole32 -luuid */
#define COBJMACROS
#include <windows.h>
#include <objbase.h>
#include <stdio.h>
#include <string.h>

struct shared
{
    DWORD size[2];
    BYTE data[2][1024];
    HRESULT wait_hr;
};

static struct shared *sh;
static HANDLE ready, in_call, released, quit;
static BOOL disconnect;
static IPersist objs[2];

static HRESULT WINAPI persist_QueryInterface(IPersist *iface, REFIID iid, void **obj)
{
    if (IsEqualGUID(iid, &IID_IUnknown) || IsEqualGUID(iid, &IID_IPersist))
    {
        *obj = iface;
        return S_OK;
    }
    *obj = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI persist_AddRef(IPersist *iface) { return 2; }
static ULONG WINAPI persist_Release(IPersist *iface) { return 1; }

static HRESULT WINAPI persist_GetClassID(IPersist *iface, CLSID *clsid)
{
    DWORD index;

    memset(clsid, 0, sizeof(*clsid));
    clsid->Data1 = iface == &objs[0] ? 0xa : 0xb;
    if (iface != &objs[0]) return S_OK;
    if (disconnect) CoDisconnectObject((IUnknown *)iface, 0);
    SetEvent(in_call);
    sh->wait_hr = CoWaitForMultipleHandles(0, 10000, 1, &released, &index);
    return S_OK;
}

static IPersistVtbl persist_vtbl = { persist_QueryInterface, persist_AddRef, persist_Release, persist_GetClassID };

static void server(void)
{
    MSG msg;
    int i;

    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    for (i = 0; i < 2; i++)
    {
        IStream *stream;
        HGLOBAL hglobal;

        objs[i].lpVtbl = &persist_vtbl;
        CreateStreamOnHGlobal(NULL, TRUE, &stream);
        CoMarshalInterface(stream, &IID_IPersist, (IUnknown *)&objs[i], MSHCTX_LOCAL, NULL, MSHLFLAGS_NORMAL);
        GetHGlobalFromStream(stream, &hglobal);
        sh->size[i] = GlobalSize(hglobal);
        memcpy(sh->data[i], GlobalLock(hglobal), sh->size[i]);
        GlobalUnlock(hglobal);
        IStream_Release(stream);
    }
    SetEvent(ready);
    while (MsgWaitForMultipleObjects(1, &quit, FALSE, INFINITE, QS_ALLINPUT) == WAIT_OBJECT_0 + 1)
        while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) DispatchMessageA(&msg);
    CoUninitialize();
}

static IPersist *unmarshal(int i)
{
    IStream *stream;
    IPersist *p = NULL;
    HRESULT hr;

    CreateStreamOnHGlobal(NULL, TRUE, &stream);
    IStream_Write(stream, sh->data[i], sh->size[i], NULL);
    IStream_Seek(stream, (LARGE_INTEGER){{0}}, STREAM_SEEK_SET, NULL);
    hr = CoUnmarshalInterface(stream, &IID_IPersist, (void **)&p);
    if (hr) printf("CoUnmarshalInterface %d: %#lx\n", i, hr);
    IStream_Release(stream);
    return p;
}

static HRESULT call_hr;
static DWORD WINAPI call_thread(void *p)
{
    CLSID clsid;
    call_hr = IPersist_GetClassID((IPersist *)p, &clsid);
    return 0;
}

static DWORD WINAPI release_thread(void *p)
{
    IPersist_Release((IPersist *)p);
    return 0;
}

int main(int argc, char **argv)
{
    HANDLE mapping, call, rel;
    PROCESS_INFORMATION pi;
    STARTUPINFOA si = {sizeof(si)};
    IPersist *a, *b;
    char cmd[MAX_PATH + 64];
    DWORD t;
    int ret = 0;

    mapping = CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, sizeof(*sh), "com_disconnect_wait");
    sh = MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(*sh));
    ready = CreateEventA(NULL, FALSE, FALSE, "com_disconnect_wait ready");
    in_call = CreateEventA(NULL, FALSE, FALSE, "com_disconnect_wait in_call");
    released = CreateEventA(NULL, FALSE, FALSE, "com_disconnect_wait released");
    quit = CreateEventA(NULL, FALSE, FALSE, "com_disconnect_wait quit");

    if (argc > 1 && !strcmp(argv[1], "server"))
    {
        disconnect = argc > 2 && !strcmp(argv[2], "disconnect");
        server();
        return 0;
    }
    disconnect = argc > 1 && !strcmp(argv[1], "disconnect");

    sprintf(cmd, "\"%s\" server%s", argv[0], disconnect ? " disconnect" : "");
    CreateProcessA(argv[0], cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi);
    if (WaitForSingleObject(ready, 10000)) { printf("server not ready\n"); return 2; }

    CoInitializeEx(NULL, COINIT_MULTITHREADED);
    a = unmarshal(0);
    b = unmarshal(1);
    call = CreateThread(NULL, 0, call_thread, a, 0, NULL);
    if (WaitForSingleObject(in_call, 10000)) { printf("A's call not received\n"); return 2; }

    t = GetTickCount();
    rel = CreateThread(NULL, 0, release_thread, b, 0, NULL);
    if (WaitForSingleObject(rel, 5000))
    {
        printf("%s: releasing B hangs while A's call runs on the server STA\n", disconnect ? "disconnect" : "plain");
        ret = 1;
    }
    else printf("%s: releasing B took %lu ms\n", disconnect ? "disconnect" : "plain", GetTickCount() - t);
    SetEvent(released);
    if (WaitForSingleObject(call, 15000)) { printf("A's call hangs\n"); ret = 1; }
    else printf("A's call returned %#lx, server wait %#lx\n", call_hr, sh->wait_hr);
    if (!ret) { IPersist_Release(a); SetEvent(quit); WaitForSingleObject(pi.hProcess, 5000); }
    else TerminateProcess(pi.hProcess, 1);
    return ret;
}
