/* What happens to a COM call cancelled by the message filter (099)?
 * The client (STA, message filter) calls IPersist::GetClassID on a proxy. The first call is slow
 * (~600 ms on the server), and the server posts a message to the client thread when it starts,
 * so the client's MessagePending runs and returns PENDINGMSG_CANCELCALL. Then the client makes
 * a second call right away (filter now waits), waits for the server to finish both, and makes
 * a third. GetClassID returns CLSID {n} for the n-th call.
 * Servers: in-process STA thread, in-process MTA, other process STA, other process MTA.
 * Usage: com_cancel.exe [sta|mta|psta|pmta]...  (child: com_cancel.exe server sta|mta)
 * Build: x86_64-w64-mingw32-gcc -O2 -o com_cancel.exe com_cancel.c -lole32 -luuid */
#define COBJMACROS
#include <windows.h>
#include <objbase.h>
#include <stdio.h>

struct shared
{
    LONG calls, finished;
    DWORD client_tid;
    HRESULT testcancel_first, testcancel_last;  /* CoTestCancel in call 1: at its start, at its end */
    DWORD cancel_seen_ms;                        /* when call 1 saw CoTestCancel change */
    DWORD size;
    BYTE data[4096];
};

static struct shared *sh;
static HANDLE ready, quit;
static DWORD t0;

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
    LONG n = InterlockedIncrement(&sh->calls);
    DWORD start = GetTickCount();
    HRESULT hr;

    if (n == 1)
    {
        /* CoTestCancel is an unimplemented stub on Wine */
        BOOL wine = !!GetProcAddress(GetModuleHandleA("ntdll"), "wine_get_version");
        sh->testcancel_first = sh->testcancel_last = wine ? E_NOTIMPL : CoTestCancel();
        PostThreadMessageW(sh->client_tid, WM_APP, 0, 0);
        while (GetTickCount() - start < 600)
        {
            hr = wine ? E_NOTIMPL : CoTestCancel();
            if (hr != sh->testcancel_last && !sh->cancel_seen_ms) sh->cancel_seen_ms = GetTickCount() - start;
            sh->testcancel_last = hr;
            Sleep(5);
        }
    }
    memset(clsid, 0, sizeof(*clsid));
    clsid->Data1 = n;
    InterlockedIncrement(&sh->finished);
    return S_OK;
}

static const IPersistVtbl persist_vtbl = { persist_QueryInterface, persist_AddRef, persist_Release, persist_GetClassID };
static IPersist persist = { (IPersistVtbl *)&persist_vtbl };

static void serve(BOOL sta, MSHCTX ctx)
{
    IStream *stream;
    HGLOBAL hglobal;
    HRESULT hr;

    CoInitializeEx(NULL, sta ? COINIT_APARTMENTTHREADED : COINIT_MULTITHREADED);
    CreateStreamOnHGlobal(NULL, TRUE, &stream);
    hr = CoMarshalInterface(stream, &IID_IPersist, (IUnknown *)&persist, ctx, NULL, MSHLFLAGS_NORMAL);
    if (hr) printf("CoMarshalInterface %#lx\n", hr);
    GetHGlobalFromStream(stream, &hglobal);
    sh->size = GlobalSize(hglobal);
    memcpy(sh->data, GlobalLock(hglobal), sh->size);
    GlobalUnlock(hglobal);
    SetEvent(ready);
    if (sta)
    {
        MSG msg;
        while (MsgWaitForMultipleObjects(1, &quit, FALSE, INFINITE, QS_ALLINPUT) == WAIT_OBJECT_0 + 1)
            while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) DispatchMessageW(&msg);
    }
    else WaitForSingleObject(quit, INFINITE);
    CoReleaseMarshalData(stream);
    IStream_Release(stream);
    CoUninitialize();
}

static DWORD CALLBACK server_thread(void *arg)
{
    serve(!!arg, MSHCTX_INPROC);
    return 0;
}

static LONG pending_count, cancel;

static HRESULT WINAPI filter_QueryInterface(IMessageFilter *iface, REFIID iid, void **obj)
{
    *obj = iface;
    return S_OK;
}
static ULONG WINAPI filter_AddRef(IMessageFilter *iface) { return 2; }
static ULONG WINAPI filter_Release(IMessageFilter *iface) { return 1; }
static DWORD WINAPI filter_HandleInComingCall(IMessageFilter *iface, DWORD type, HTASK task, DWORD tick, INTERFACEINFO *info)
{
    return SERVERCALL_ISHANDLED;
}
static DWORD WINAPI filter_RetryRejectedCall(IMessageFilter *iface, HTASK task, DWORD tick, DWORD type)
{
    return -1;
}
static DWORD WINAPI filter_MessagePending(IMessageFilter *iface, HTASK task, DWORD tick, DWORD type)
{
    pending_count++;
    printf("  [%4lu ms] MessagePending task %p tick %lu type %lu -> %s\n", GetTickCount() - t0, task, tick, type,
           cancel ? "CANCELCALL" : "WAITDEFPROCESS");
    return cancel ? PENDINGMSG_CANCELCALL : PENDINGMSG_WAITDEFPROCESS;
}
static const IMessageFilterVtbl filter_vtbl = { filter_QueryInterface, filter_AddRef, filter_Release,
        filter_HandleInComingCall, filter_RetryRejectedCall, filter_MessagePending };
static IMessageFilter filter = { (IMessageFilterVtbl *)&filter_vtbl };

static void call(IPersist *proxy, const char *what)
{
    CLSID clsid;
    DWORD start = GetTickCount();
    HRESULT hr;
    MSG msg;
    int msgs = 0;

    memset(&clsid, 0xcc, sizeof(clsid));
    pending_count = 0;
    hr = IPersist_GetClassID(proxy, &clsid);
    while (PeekMessageW(&msg, NULL, WM_APP, WM_APP, PM_REMOVE)) msgs++;
    printf("  [%4lu ms] %s: hr %#lx after %lu ms, clsid.Data1 %#lx, MessagePending %ld, WM_APP left in queue %d, "
           "server calls %ld finished %ld\n", GetTickCount() - t0, what, hr, GetTickCount() - start, clsid.Data1,
           pending_count, msgs, sh->calls, sh->finished);
}

static void run(const char *mode)
{
    PROCESS_INFORMATION pi = {0};
    HANDLE thread = NULL;
    IPersist *proxy;
    IStream *stream;
    HRESULT hr;
    DWORD start;

    printf("%s:\n", mode);
    memset(sh, 0, sizeof(*sh));
    sh->client_tid = GetCurrentThreadId();
    ResetEvent(quit);
    if (mode[0] == 'p')
    {
        STARTUPINFOA si = {sizeof(si)};
        char cmd[MAX_PATH + 32], exe[MAX_PATH];
        GetModuleFileNameA(NULL, exe, sizeof(exe));
        sprintf(cmd, "\"%s\" server %s", exe, mode + 1);
        CreateProcessA(exe, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi);
    }
    else thread = CreateThread(NULL, 0, server_thread, (void *)(INT_PTR)(mode[0] == 's'), 0, NULL);
    WaitForSingleObject(ready, INFINITE);

    CreateStreamOnHGlobal(NULL, TRUE, &stream);
    IStream_Write(stream, sh->data, sh->size, NULL);
    IStream_Seek(stream, (LARGE_INTEGER){{0}}, STREAM_SEEK_SET, NULL);
    hr = CoUnmarshalInterface(stream, &IID_IPersist, (void **)&proxy);
    IStream_Release(stream);
    if (hr)
    {
        printf("  CoUnmarshalInterface %#lx\n", hr);
        return;
    }

    t0 = GetTickCount();
    cancel = 1;
    call(proxy, "call 1 (cancel)");
    cancel = 0;
    call(proxy, "call 2");
    start = GetTickCount();
    while (sh->finished < 2 && GetTickCount() - start < 5000)
    {
        MSG msg;
        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) DispatchMessageW(&msg);
        Sleep(10);
    }
    call(proxy, "call 3");
    printf("  server: CoTestCancel in call 1: at start %#lx, changed after %lu ms, at end %#lx\n",
           sh->testcancel_first, sh->cancel_seen_ms, sh->testcancel_last);

    IPersist_Release(proxy);
    SetEvent(quit);
    if (thread) WaitForSingleObject(thread, INFINITE);
    else WaitForSingleObject(pi.hProcess, 10000);
    if (thread) CloseHandle(thread);
    else
    {
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }
}

int main(int argc, char **argv)
{
    static const char *all[] = { "sta", "mta", "psta", "pmta" };
    IMessageFilter *prev;
    HANDLE map;
    int i;

    setvbuf(stdout, NULL, _IONBF, 0);
    map = CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, sizeof(*sh), "com_cancel_shm");
    sh = MapViewOfFile(map, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(*sh));
    ready = CreateEventA(NULL, FALSE, FALSE, "com_cancel_ready");
    quit = CreateEventA(NULL, TRUE, FALSE, "com_cancel_quit");

    if (argc == 3 && !strcmp(argv[1], "server"))
    {
        serve(!strcmp(argv[2], "sta"), MSHCTX_LOCAL);
        return 0;
    }

    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    CoRegisterMessageFilter(&filter, &prev);
    if (argc > 1) for (i = 1; i < argc; i++) run(argv[i]);
    else for (i = 0; i < 4; i++) run(all[i]);
    CoRegisterMessageFilter(prev, NULL);
    CoUninitialize();
    return 0;
}
