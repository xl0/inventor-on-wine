/* stress.exe [seconds] [fakeifs]: server process with NSTA STA threads + MTA threads hosting objects that expose
 * many interfaces; slots of objrefs in shared memory. Server threads keep replacing objects (marshal all IIDs,
 * later CoDisconnectObject or just drop), some objects disconnect themselves inside calls. Client process threads
 * unmarshal random slots, call random interfaces, release. Reports call result histogram.
 * "bench N": server registers N dummy RPC interfaces, client times IPersist calls. */
#define COBJMACROS
#include <windows.h>
#include <objbase.h>
#include <oleidl.h>
#include <ocidl.h>
#include <rpc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const struct { const IID *iid; int method; int arg; } ifs[] = {
    {&IID_IPersist,3,0},{&IID_IPersistStream,3,0},{&IID_IPersistStreamInit,3,0},{&IID_IPersistStorage,3,0},
    {&IID_IPersistFile,3,0},{&IID_IPersistPropertyBag,3,0},{&IID_IOleWindow,3,0},{&IID_IOleInPlaceObject,3,0},
    {&IID_IOleInPlaceActiveObject,3,0},{&IID_IOleInPlaceUIWindow,3,0},{&IID_IOleInPlaceFrame,3,0},
    {&IID_IOleInPlaceSite,3,0},{&IID_IRunnableObject,3,0},{&IID_IClassFactory,4,1},{&IID_IStream,9,1},
    {&IID_ILockBytes,5,1},{&IID_IStorage,10,1},
};
#define NIF (sizeof(ifs)/sizeof(ifs[0]))
#define NSLOT 64
#define NSTA 4
#define NMTA 2
#define NCLIENT 8

struct slot { volatile LONG seq; DWORD size[NIF]; BYTE data[NIF][512]; };
struct shared { volatile LONG stop; LONG calls, objs, selfdisc; struct slot slots[NSLOT]; LONG lock[NSLOT]; };
static struct shared *sh;

struct obj { IUnknown iface; LONG refs; int selfdisc; };
static void *vt[16];
static LONG live_objs;
static int safe;
static HRESULT WINAPI o_qi(IUnknown *i, REFIID iid, void **out)
{
    unsigned k;
    if (IsEqualGUID(iid, &IID_IUnknown)) goto ok;
    for (k = 0; k < NIF; k++) if (IsEqualGUID(iid, ifs[k].iid)) goto ok;
    *out = NULL; return E_NOINTERFACE;
ok: *out = i; IUnknown_AddRef(i); return S_OK;
}
static ULONG WINAPI o_addref(IUnknown *i) { return InterlockedIncrement(&((struct obj *)i)->refs); }
static ULONG WINAPI o_release(IUnknown *i)
{
    struct obj *o = (struct obj *)i; LONG r = InterlockedDecrement(&o->refs);
    if (r < 0) { printf("server: over-release %p\n", o); fflush(stdout); }
    if (!r) { memset(o, 0xcc, sizeof(*o)); HeapFree(GetProcessHeap(), 0, o); InterlockedDecrement(&live_objs); }
    return r;
}
static HRESULT WINAPI o_method(IUnknown *i, void *a)
{
    struct obj *o = (struct obj *)i;
    InterlockedIncrement(&sh->calls);
    if (o->selfdisc && InterlockedExchange((LONG *)&o->selfdisc, 0))
    {
        InterlockedIncrement(&sh->selfdisc);
        CoDisconnectObject(i, 0);
        if (safe == 1) { DWORD idx; HANDLE h = GetCurrentProcess(); CoWaitForMultipleHandles(0, rand() % 5, 1, &h, &idx); }
        else Sleep(rand() % 3);
    }
    return S_OK;
}

static DWORD WINAPI server_thread(void *arg)
{
    BOOL sta = (INT_PTR)arg < NSTA;
    struct obj *mine[NSLOT] = {0};
    unsigned k, s;
    MSG msg;

    CoInitializeEx(NULL, sta ? COINIT_APARTMENTTHREADED : COINIT_MULTITHREADED);
    srand(GetCurrentThreadId());
    while (!sh->stop)
    {
        s = rand() % NSLOT;
        if (sta) while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) DispatchMessageA(&msg);
        if (InterlockedCompareExchange(&sh->lock[s], 1, 0)) { if (!sta) Sleep(0); continue; }
        if (mine[s] && safe != 3 && (sta || !safe) && rand() % 2) CoDisconnectObject(&mine[s]->iface, 0);
        if (mine[s]) { IUnknown_Release(&mine[s]->iface); mine[s] = NULL; }
        if (!sh->slots[s].seq || rand() % 4 == 0)
        {
            struct obj *o = HeapAlloc(GetProcessHeap(), 0, sizeof(*o));
            o->iface.lpVtbl = (void *)vt; o->refs = 1; o->selfdisc = safe != 3 && (sta || !safe) && rand() % 4 == 0;
            InterlockedIncrement(&live_objs);
            for (k = 0; k < NIF; k++)
            {
                IStream *st; HGLOBAL hg; HRESULT hr;
                CreateStreamOnHGlobal(NULL, TRUE, &st);
                hr = CoMarshalInterface(st, ifs[k].iid, &o->iface, MSHCTX_LOCAL, NULL, MSHLFLAGS_NORMAL);
                if (hr) { printf("marshal %u: %#lx\n", k, hr); sh->slots[s].size[k] = 0; IStream_Release(st); continue; }
                GetHGlobalFromStream(st, &hg);
                sh->slots[s].size[k] = GlobalSize(hg);
                memcpy(sh->slots[s].data[k], GlobalLock(hg), sh->slots[s].size[k]);
                GlobalUnlock(hg);
                IStream_Release(st);
            }
            mine[s] = o;
            InterlockedIncrement(&sh->slots[s].seq);
            InterlockedIncrement(&sh->objs);
        }
        InterlockedExchange(&sh->lock[s], 0);
        if (sta) MsgWaitForMultipleObjects(0, NULL, FALSE, rand() % 3, QS_ALLINPUT); else Sleep(rand() % 3);
    }
    for (s = 0; s < NSLOT; s++) if (mine[s]) { CoDisconnectObject(&mine[s]->iface, 0); IUnknown_Release(&mine[s]->iface); }
    CoUninitialize();
    return 0;
}

static LONG hist_hr[16], hist_n[16];
static void count(HRESULT hr)
{
    int i;
    for (i = 0; i < 16; i++)
    {
        if (hist_n[i] && hist_hr[i] == hr) { InterlockedIncrement(&hist_n[i]); return; }
        if (!hist_n[i] && !InterlockedCompareExchange(&hist_hr[i], hr, 0)) { hist_hr[i] = hr; InterlockedIncrement(&hist_n[i]); return; }
    }
}

static DWORD WINAPI client_thread(void *arg)
{
    CoInitializeEx(NULL, (INT_PTR)arg % 2 ? COINIT_APARTMENTTHREADED : COINIT_MULTITHREADED);
    srand(GetCurrentThreadId());
    while (!sh->stop)
    {
        unsigned s = rand() % NSLOT, k = rand() % NIF, n;
        IUnknown *p = NULL; IStream *st; HRESULT hr; BYTE buf[64];
        if (InterlockedCompareExchange(&sh->lock[s], 2, 0)) continue;
        if (!sh->slots[s].seq || !sh->slots[s].size[k]) { InterlockedExchange(&sh->lock[s], 0); continue; }
        CreateStreamOnHGlobal(NULL, TRUE, &st);
        IStream_Write(st, sh->slots[s].data[k], sh->slots[s].size[k], NULL);
        sh->slots[s].size[k] = 0; /* NORMAL objref: one unmarshal */
        InterlockedExchange(&sh->lock[s], 0);
        IStream_Seek(st, (LARGE_INTEGER){{0}}, STREAM_SEEK_SET, NULL);
        hr = CoUnmarshalInterface(st, ifs[k].iid, (void **)&p);
        IStream_Release(st);
        if (hr) { count(hr); continue; }
        for (n = rand() % 4 + 1; n; n--)
        {
            void **v = *(void ***)p;
            hr = ifs[k].arg ? ((HRESULT (WINAPI *)(void *, INT_PTR))v[ifs[k].method])(p, 1)
                            : ((HRESULT (WINAPI *)(void *, void *))v[ifs[k].method])(p, buf);
            count(hr);
        }
        IUnknown_Release(p);
    }
    CoUninitialize();
    return 0;
}

static LONG CALLBACK veh(EXCEPTION_POINTERS *ep)
{
    void *frames[40]; char line[2048]; int n, i, len;
    if (ep->ExceptionRecord->ExceptionCode != EXCEPTION_ACCESS_VIOLATION) return EXCEPTION_CONTINUE_SEARCH;
    n = RtlCaptureStackBackTrace(0, 40, frames, NULL);
    len = sprintf(line, "AVTRACE tid %04lx at %p addr %p:", GetCurrentThreadId(), ep->ExceptionRecord->ExceptionAddress,
                  (void *)ep->ExceptionRecord->ExceptionInformation[1]);
    for (i = 0; i < n && len < 1900; i++)
    {
        HMODULE mod; char name[MAX_PATH];
        if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, frames[i], &mod))
        {
            GetModuleFileNameA(mod, name, sizeof(name));
            len += sprintf(line + len, " %s+%#Ix", strrchr(name, '\\') ? strrchr(name, '\\') + 1 : name, (char *)frames[i] - (char *)mod);
        }
        else len += sprintf(line + len, " %p", frames[i]);
    }
    printf("%s\n", line);
    return EXCEPTION_CONTINUE_SEARCH;
}

static RPC_DISPATCH_FUNCTION dummy_table[1];
static RPC_DISPATCH_TABLE dummy_dispatch = { 1, dummy_table };

int main(int argc, char **argv)
{
    HANDLE mapping, th[16], ev;
    PROCESS_INFORMATION pi; STARTUPINFOA si = {sizeof(si)};
    char cmd[MAX_PATH + 64];
    int i, secs = argc > 2 ? atoi(argv[2]) : 20;
    DWORD code;

    char name[64];
    if (argc > 3) sprintf(name, "r109stress %s", argv[3]); else sprintf(name, "r109stress %lu", GetCurrentProcessId());
    mapping = CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, sizeof(*sh), name);
    sh = MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(*sh));
    strcat(name, " ready");
    ev = CreateEventA(NULL, TRUE, FALSE, name);
    setvbuf(stdout, NULL, _IONBF, 0);
    vt[0] = o_qi; vt[1] = o_addref; vt[2] = o_release;
    for (i = 3; i < 16; i++) vt[i] = o_method;

    if (argc > 1 && !strcmp(argv[1], "server"))
    {
        AddVectoredExceptionHandler(1, veh);
        safe = getenv("R109_SAFE") ? atoi(getenv("R109_SAFE")) : 0;
        for (i = 0; i < NSTA + NMTA; i++) th[i] = CreateThread(NULL, 0, server_thread, (void *)(INT_PTR)i, 0, NULL);
        WaitForMultipleObjects(NSTA + NMTA, th, TRUE, INFINITE);
        printf("server: objs %ld, live objects at exit %ld\n", sh->objs, live_objs);
        return 0;
    }
    if (argc > 1 && !strcmp(argv[1], "bserver"))
    {
        int n = atoi(argv[2]); IStream *st; HGLOBAL hg; struct obj o = {{(void *)vt}, 1, 0}; MSG msg;
        CoInitializeEx(NULL, COINIT_MULTITHREADED);
        CreateStreamOnHGlobal(NULL, TRUE, &st);
        CoMarshalInterface(st, &IID_IPersist, &o.iface, MSHCTX_LOCAL, NULL, MSHLFLAGS_TABLESTRONG);
        for (i = 0; i < n; i++)
        {
            RPC_SERVER_INTERFACE *If = calloc(1, sizeof(*If));
            If->Length = sizeof(*If);
            UuidCreate(&If->InterfaceId.SyntaxGUID);
            If->DispatchTable = &dummy_dispatch;
            RpcServerRegisterIfEx((RPC_IF_HANDLE)If, NULL, NULL, RPC_IF_OLE | RPC_IF_AUTOLISTEN, RPC_C_LISTEN_MAX_CALLS_DEFAULT, NULL);
        }
        GetHGlobalFromStream(st, &hg);
        sh->slots[0].size[0] = GlobalSize(hg);
        memcpy(sh->slots[0].data[0], GlobalLock(hg), sh->slots[0].size[0]);
        SetEvent(ev);
        while (!sh->stop) Sleep(50);
        return 0;
    }
    if (argc > 1 && !strcmp(argv[1], "bench"))
    {
        IStream *st; IPersist *p; CLSID c; LARGE_INTEGER f, t0, t1; int n = 3000;
        sprintf(cmd, "\"%s\" bserver %s %lu", argv[0], argv[2], GetCurrentProcessId());
        CreateProcessA(argv[0], cmd, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi);
        WaitForSingleObject(ev, 30000);
        CoInitializeEx(NULL, COINIT_MULTITHREADED);
        CreateStreamOnHGlobal(NULL, TRUE, &st);
        IStream_Write(st, sh->slots[0].data[0], sh->slots[0].size[0], NULL);
        IStream_Seek(st, (LARGE_INTEGER){{0}}, STREAM_SEEK_SET, NULL);
        if (CoUnmarshalInterface(st, &IID_IPersist, (void **)&p)) { printf("unmarshal failed\n"); return 1; }
        for (i = 0; i < 300; i++) IPersist_GetClassID(p, &c);
        QueryPerformanceFrequency(&f); QueryPerformanceCounter(&t0);
        for (i = 0; i < n; i++) IPersist_GetClassID(p, &c);
        QueryPerformanceCounter(&t1);
        printf("bench %s extra ifs: %.1f us/call\n", argv[2], (t1.QuadPart - t0.QuadPart) * 1e6 / f.QuadPart / n);
        sh->stop = 1;
        WaitForSingleObject(pi.hProcess, 10000);
        return 0;
    }
    sprintf(cmd, "\"%s\" server 0 %lu", argv[0], GetCurrentProcessId());
    CreateProcessA(argv[0], cmd, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi);
    for (i = 0; i < NCLIENT; i++) th[i] = CreateThread(NULL, 0, client_thread, (void *)(INT_PTR)i, 0, NULL);
    Sleep(secs * 1000);
    sh->stop = 1;
    if (WaitForMultipleObjects(NCLIENT, th, TRUE, 30000)) printf("client threads hang\n");
    if (WaitForSingleObject(pi.hProcess, 30000)) printf("server hangs\n");
    GetExitCodeProcess(pi.hProcess, &code);
    printf("server exit %#lx, server calls %ld, objs %ld, selfdisc %ld\n", code, sh->calls, sh->objs, sh->selfdisc);
    for (i = 0; i < 16 && hist_n[i]; i++) printf("  hr %#lx: %ld\n", hist_hr[i], hist_n[i]);
    return 0;
}
