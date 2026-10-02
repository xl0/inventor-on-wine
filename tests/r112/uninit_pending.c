/* uninit_pending.exe [N [release]]: STA server in a child process marshals an object, then stops pumping. The client
 * starts N calls on it from N threads (queued for the STA), then the server calls CoUninitialize.
 * Reports: did the queued calls run (inside CoUninitialize?), how long CoUninitialize took, the object's
 * refcount afterwards (stub released?), and the client calls' results/times. (112) */
#define COBJMACROS
#include <windows.h>
#include <objbase.h>
#include <stdio.h>
#include <string.h>

struct shared { DWORD size; BYTE data[1024]; LONG calls, calls_in_uninit, refs_after, uninit_ms, uninit_done; };
static struct shared *sh;
static HANDLE ready, go;
static LONG refs = 1, in_uninit;

static HRESULT WINAPI qi(IPersist *iface, REFIID iid, void **out)
{
    if (IsEqualGUID(iid, &IID_IUnknown) || IsEqualGUID(iid, &IID_IPersist)) { *out = iface; IPersist_AddRef(iface); return S_OK; }
    *out = NULL; return E_NOINTERFACE;
}
static ULONG WINAPI addref(IPersist *iface) { return InterlockedIncrement(&refs); }
static ULONG WINAPI release(IPersist *iface) { return InterlockedDecrement(&refs); }
static HRESULT WINAPI getclassid(IPersist *iface, CLSID *clsid)
{
    memset(clsid, 0, sizeof(*clsid));
    InterlockedIncrement(&sh->calls);
    if (in_uninit) InterlockedIncrement(&sh->calls_in_uninit);
    return S_OK;
}
static IPersistVtbl vtbl = { qi, addref, release, getclassid };
static IPersist obj = { &vtbl };

static void server(void)
{
    IStream *stream; HGLOBAL hglobal; DWORD t;
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    CreateStreamOnHGlobal(NULL, TRUE, &stream);
    CoMarshalInterface(stream, &IID_IPersist, (IUnknown *)&obj, MSHCTX_LOCAL, NULL, MSHLFLAGS_NORMAL);
    GetHGlobalFromStream(stream, &hglobal);
    sh->size = GlobalSize(hglobal);
    memcpy(sh->data, GlobalLock(hglobal), sh->size);
    GlobalUnlock(hglobal);
    IStream_Release(stream);
    SetEvent(ready);
    WaitForSingleObject(go, INFINITE); /* no pumping: incoming calls stay queued */
    printf("server: refs before CoUninitialize %ld, calls %ld\n", refs, sh->calls);
    t = GetTickCount();
    in_uninit = 1;
    CoUninitialize();
    in_uninit = 0;
    sh->uninit_ms = GetTickCount() - t;
    sh->refs_after = refs;
    sh->uninit_done = 1;
    Sleep(3000);
}

static IPersist *proxy;
static int release_proxy;
static HRESULT hrs[64]; static DWORD done_ms[64], start;
static DWORD WINAPI call_thread(void *arg)
{
    CLSID c; int i = (INT_PTR)arg;
    CoInitializeEx(NULL, COINIT_MULTITHREADED);
    hrs[i] = IPersist_GetClassID(proxy, &c);
    done_ms[i] = GetTickCount() - start;
    if (release_proxy) IPersist_Release(proxy);
    return 0;
}

int main(int argc, char **argv)
{
    HANDLE mapping, th[64];
    PROCESS_INFORMATION pi; STARTUPINFOA si = {sizeof(si)};
    char cmd[MAX_PATH + 64];
    IStream *s; HRESULT hr; int i, n = argc > 1 ? atoi(argv[1]) : 2;
    release_proxy = argc > 2; /* "N release": each caller releases the proxy after its call (N <= 1) */
    CLSID c;

    mapping = CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, sizeof(*sh), "uninit_pending");
    sh = MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(*sh));
    ready = CreateEventA(NULL, FALSE, FALSE, "uninit_pending ready");
    go = CreateEventA(NULL, FALSE, FALSE, "uninit_pending go");
    setvbuf(stdout, NULL, _IONBF, 0);
    if (argc > 1 && !strcmp(argv[1], "server")) { server(); return 0; }

    sprintf(cmd, "\"%s\" server", argv[0]);
    CreateProcessA(argv[0], cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi);
    if (WaitForSingleObject(ready, 10000)) { printf("server not ready\n"); return 2; }
    CoInitializeEx(NULL, COINIT_MULTITHREADED);
    CreateStreamOnHGlobal(NULL, TRUE, &s);
    IStream_Write(s, sh->data, sh->size, NULL);
    IStream_Seek(s, (LARGE_INTEGER){{0}}, STREAM_SEEK_SET, NULL);
    hr = CoUnmarshalInterface(s, &IID_IPersist, (void **)&proxy);
    IStream_Release(s);
    if (hr) { printf("unmarshal %#lx\n", hr); return 2; }
    start = GetTickCount();
    for (i = 0; i < n; i++) th[i] = CreateThread(NULL, 0, call_thread, (void *)(INT_PTR)i, 0, NULL);
    Sleep(500);
    printf("calls run before CoUninitialize: %ld\n", sh->calls);
    SetEvent(go);
    for (i = 0; i < n; i++)
    {
        if (WaitForSingleObject(th[i], 2500)) printf("call %d: still pending (server uninit done %ld)\n", i, sh->uninit_done);
        else printf("call %d: %#lx after %lu ms (server uninit done %ld)\n", i, hrs[i], done_ms[i], sh->uninit_done);
    }
    for (i = 0; i < 50 && !sh->uninit_done; i++) Sleep(100);
    printf("server: calls %ld, in CoUninitialize %ld, CoUninitialize %ld ms, object refs after %ld\n",
           sh->calls, sh->calls_in_uninit, sh->uninit_ms, sh->refs_after);
    if (!release_proxy)
    {
        hr = IPersist_GetClassID(proxy, &c);
        printf("call after uninit %#lx\n", hr);
    }
    WaitForSingleObject(pi.hProcess, 10000);
    for (i = 0; i < n; i++) if (!WaitForSingleObject(th[i], 0)) continue; else printf("call %d still pending after server exit\n", i);
    return 0;
}
