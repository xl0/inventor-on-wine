/* hol.exe [NSTA] [BLOCKMS] [SECS] [NEST]: server child process hosts NSTA STAs (one object each); the client
 * process (MTA, 2 threads per STA) calls all of them for SECS. STA 0's method sleeps BLOCKMS. Reports per-STA
 * call count and max latency: calls to STAs 1.. must not wait behind STA 0 (one endpoint, shared connections).
 * NEST=1: each server method on STA k>0 first calls STA (k%(NSTA-1))+1's object from inside the call
 * (cross-apartment within the server process, through the same endpoint). */
#define COBJMACROS
#include <windows.h>
#include <objbase.h>
#include <stdio.h>
#include <stdlib.h>

#define MAXSTA 128
struct shared { LONG ready; DWORD size[MAXSTA]; BYTE data[MAXSTA][512]; };
static struct shared *sh;
static int nsta, blockms, nest;
static HANDLE stop_ev;
static IPersist *srv_proxies[MAXSTA]; /* server-side proxies used by nested calls, per STA thread */

struct obj { IPersist iface; int idx; IPersist *peer; };
static HRESULT WINAPI qi(IPersist *i, REFIID iid, void **o)
{ if (IsEqualGUID(iid, &IID_IUnknown) || IsEqualGUID(iid, &IID_IPersist)) { *o = i; return S_OK; } *o = NULL; return E_NOINTERFACE; }
static ULONG WINAPI addref(IPersist *i) { return 2; }
static ULONG WINAPI release(IPersist *i) { return 1; }
static HRESULT WINAPI gci(IPersist *i, CLSID *c)
{
    struct obj *o = (struct obj *)i;
    memset(c, 0, sizeof(*c)); c->Data1 = o->idx;
    if (!o->idx && blockms) Sleep(blockms);
    if (nest && o->peer) { CLSID c2; HRESULT hr = IPersist_GetClassID(o->peer, &c2); if (hr) return hr; }
    return S_OK;
}
static IPersistVtbl vt = { qi, addref, release, gci };

static DWORD CALLBACK sta(void *arg)
{
    int idx = (int)(ULONG_PTR)arg; struct obj o = { {&vt}, idx, NULL }; IStream *st; HGLOBAL hg; MSG msg;
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    CreateStreamOnHGlobal(NULL, TRUE, &st);
    CoMarshalInterface(st, &IID_IPersist, (IUnknown *)&o.iface, MSHCTX_LOCAL, NULL, MSHLFLAGS_TABLESTRONG);
    GetHGlobalFromStream(st, &hg);
    sh->size[idx] = GlobalSize(hg); memcpy(sh->data[idx], GlobalLock(hg), sh->size[idx]); GlobalUnlock(hg);
    InterlockedIncrement(&sh->ready);
    while (sh->ready < nsta) Sleep(1);
    if (nest && idx && idx < nsta - 1)
    {
        /* proxy to a peer STA of this same process */
        int p = nsta - 1; IStream *ps; HGLOBAL h2 = GlobalAlloc(GMEM_MOVEABLE, sh->size[p]);
        memcpy(GlobalLock(h2), sh->data[p], sh->size[p]); GlobalUnlock(h2);
        CreateStreamOnHGlobal(h2, TRUE, &ps);
        if (CoUnmarshalInterface(ps, &IID_IPersist, (void **)&o.peer)) printf("server: nested unmarshal failed\n");
        IStream_Release(ps);
    }
    InterlockedIncrement(&sh->ready);
    while (MsgWaitForMultipleObjects(1, &stop_ev, FALSE, INFINITE, QS_ALLINPUT) == WAIT_OBJECT_0 + 1)
        while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) DispatchMessageA(&msg);
    if (o.peer) IPersist_Release(o.peer);
    if (getenv("VERB")) printf("server: sta %d released\n", idx);
    CoUninitialize();
    if (getenv("VERB")) printf("server: sta %d uninit\n", idx);
    return 0;
}

static IPersist *proxies[MAXSTA];
static LONG ncalls[MAXSTA], maxlat[MAXSTA], fails[MAXSTA];
static DWORD end_time;
static DWORD CALLBACK cl(void *arg)
{
    int idx = (int)(ULONG_PTR)arg % nsta;
    CoInitializeEx(NULL, COINIT_MULTITHREADED);
    while (GetTickCount() < end_time)
    {
        CLSID c; DWORD t = GetTickCount(); HRESULT hr = IPersist_GetClassID(proxies[idx], &c); LONG d = GetTickCount() - t, m;
        if (hr || c.Data1 != idx) { if (InterlockedIncrement(&fails[idx]) < 3) printf("sta %d: hr %#lx id %lu\n", idx, hr, c.Data1); Sleep(10); }
        InterlockedIncrement(&ncalls[idx]);
        while (d > (m = maxlat[idx]) && InterlockedCompareExchange(&maxlat[idx], d, m) != m);
    }
    CoUninitialize();
    return 0;
}

int main(int argc, char **argv)
{
    HANDLE map; int i, secs; char cmd[512];
    setvbuf(stdout, NULL, _IONBF, 0);
    if (argc > 1 && !strcmp(argv[1], "server"))
    {
        HANDLE th[MAXSTA];
        nsta = atoi(argv[2]); blockms = atoi(argv[3]); nest = atoi(argv[4]);
        map = OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, argv[5]);
        sh = MapViewOfFile(map, FILE_MAP_ALL_ACCESS, 0, 0, 0);
        stop_ev = OpenEventA(SYNCHRONIZE, FALSE, argv[6]);
        for (i = 0; i < nsta; i++) th[i] = CreateThread(NULL, 0, sta, (void *)(ULONG_PTR)i, 0, NULL);
        for (i = 0; i < nsta; i++) { if (WaitForSingleObject(th[i], 15000)) printf("server: sta %d hangs\n", i); }
        printf("server: exiting\n");
        return 0;
    }
    nsta = argc > 1 ? atoi(argv[1]) : 8; blockms = argc > 2 ? atoi(argv[2]) : 2000;
    secs = argc > 3 ? atoi(argv[3]) : 5; nest = argc > 4 ? atoi(argv[4]) : 0;
    {
        char mname[64], ename[64]; STARTUPINFOA si = { sizeof(si) }; PROCESS_INFORMATION pi; HANDLE th[2 * MAXSTA];
        sprintf(mname, "r114hol_m_%lu", GetCurrentProcessId()); sprintf(ename, "r114hol_e_%lu", GetCurrentProcessId());
        map = CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, sizeof(*sh), mname);
        sh = MapViewOfFile(map, FILE_MAP_ALL_ACCESS, 0, 0, 0);
        stop_ev = CreateEventA(NULL, TRUE, FALSE, ename);
        sprintf(cmd, "\"%s\" server %d %d %d %s %s", argv[0], nsta, blockms, nest, mname, ename);
        CreateProcessA(argv[0], cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi);
        while (sh->ready < 2 * nsta) { if (!WaitForSingleObject(pi.hProcess, 1)) { printf("server died\n"); return 1; } }
        CoInitializeEx(NULL, COINIT_MULTITHREADED);
        for (i = 0; i < nsta; i++)
        {
            IStream *st; HGLOBAL hg = GlobalAlloc(GMEM_MOVEABLE, sh->size[i]); HRESULT hr;
            memcpy(GlobalLock(hg), sh->data[i], sh->size[i]); GlobalUnlock(hg);
            CreateStreamOnHGlobal(hg, TRUE, &st);
            if ((hr = CoUnmarshalInterface(st, &IID_IPersist, (void **)&proxies[i]))) { printf("unmarshal %d: %#lx\n", i, hr); return 1; }
            IStream_Release(st);
        }
        end_time = GetTickCount() + secs * 1000;
        for (i = 0; i < 2 * nsta; i++) th[i] = CreateThread(NULL, 0, cl, (void *)(ULONG_PTR)i, 0, NULL);
        for (i = 0; i < 2 * nsta; i += 64)
            if (WaitForMultipleObjects(min(64, 2 * nsta - i), th + i, TRUE, secs * 1000 + 30000)) { printf("client threads hang\n"); return 1; }
        {
            LONG worst = 0, total = 0;
            for (i = 0; i < nsta; i++) { total += ncalls[i]; if (i && maxlat[i] > worst) worst = maxlat[i]; }
            printf("sta 0: %ld calls max %ld ms fails %ld; sta 1..%d: %ld calls, worst max latency %ld ms\n",
                   ncalls[0], maxlat[0], fails[0], nsta - 1, total - ncalls[0], worst);
            for (i = 1; i < nsta; i++) if (fails[i] || maxlat[i] > blockms / 2) printf("  sta %d: %ld calls max %ld fails %ld\n", i, ncalls[i], maxlat[i], fails[i]);
        }
        for (i = 0; i < nsta; i++) IPersist_Release(proxies[i]);
        SetEvent(stop_ev);
        if (WaitForSingleObject(pi.hProcess, 20000)) { printf("server exit hangs pid %lx\n", pi.dwProcessId); if (!getenv("KEEP")) TerminateProcess(pi.hProcess, 1); }
        CoUninitialize();
    }
    return 0;
}
