/* 070: which activation context is active, and does a manifest-only CLSID resolve, when code of a
 * DLL whose embedded manifest (resource 2) declares the class runs
 *  - directly on the main thread after LoadLibrary,
 *  - under a context created from the DLL's resource (HMODULE + resource 2, and path + resource 2),
 *  - inside incoming COM calls (in-process cross-apartment and cross-process) into a server
 *    object of that DLL, created/marshaled with or without the DLL's context active.
 * Build (x86_64-w64-mingw32):
 *   windres probe.rc -o probe_res.o
 *   gcc -O2 -shared -o probe.dll probe.c probe_res.o -Wl,--export-all-symbols -lole32 -loleaut32 -luuid
 *   gcc -O2 -o actctx_comcall.exe actctx_comcall.c -lole32 -loleaut32 -luuid
 * Run with probe.dll next to the exe. "child FILE" = internal (cross-process caller). */
#define COBJMACROS
#include <windows.h>
#include <ole2.h>
#include <stdio.h>

static const GUID CLSID_Server = {0xa9485c80,0,0x4000,{0x80,0,0,0,0,0,0,2}};
static void (*probe_test)(WCHAR *);
static IDispatch *(*probe_create)(void);
static HMODULE dll;
static WCHAR dllpath[MAX_PATH];

static void call(IDispatch *d, const char *what)
{
    DISPPARAMS dp = {0};
    VARIANT v;
    HRESULT hr;
    VariantInit(&v);
    hr = IDispatch_Invoke(d, 1, &IID_NULL, 0, DISPATCH_METHOD, &dp, &v, NULL, NULL);
    printf("  %-28s caller tid %lu: invoke %08lx -> %ls\n", what, GetCurrentThreadId(), hr,
           V_VT(&v) == VT_BSTR ? V_BSTR(&v) : L"?");
    VariantClear(&v);
}

static HANDLE dll_ctx(BOOL hmodule, int res)
{
    ACTCTXW ctx = { sizeof(ctx) };
    HANDLE h;
    ctx.dwFlags = ACTCTX_FLAG_RESOURCE_NAME_VALID | (hmodule ? ACTCTX_FLAG_HMODULE_VALID : 0);
    ctx.lpSource = dllpath;
    ctx.hModule = dll;
    ctx.lpResourceName = MAKEINTRESOURCEW(res);
    h = CreateActCtxW(&ctx);
    if (h == INVALID_HANDLE_VALUE) printf("  CreateActCtx(%s res %d) failed %lu\n", hmodule ? "hmodule" : "path", res, GetLastError());
    return h;
}

/* server thread: MTA or STA; creates the object per mode, marshals it for in-proc + local use */
struct server { int mode; BOOL sta; IStream *inproc; HGLOBAL local; HANDLE ready, done; };
enum { CREATE_CTX, MARSHAL_CTX, NO_CTX, CTX_KEPT };
static const char *modes[] = { "created under ctx", "marshaled under ctx", "no ctx", "ctx kept active" };

static DWORD WINAPI server_thread(void *arg)
{
    struct server *s = arg;
    HANDLE ctx = dll_ctx(TRUE, 2);
    ULONG_PTR cookie = 0;
    IDispatch *d = NULL;
    IStream *stm;
    HRESULT hr;

    CoInitializeEx(NULL, s->sta ? COINIT_APARTMENTTHREADED : COINIT_MULTITHREADED);
    if (s->mode != NO_CTX) ActivateActCtx(ctx, &cookie);
    if (s->mode == CREATE_CTX || s->mode == CTX_KEPT)
    {
        hr = CoCreateInstance(&CLSID_Server, NULL, CLSCTX_INPROC_SERVER, &IID_IDispatch, (void **)&d);
        if (FAILED(hr)) printf("  server CoCreateInstance %08lx\n", hr);
    }
    else
    {
        if (cookie) { DeactivateActCtx(0, cookie); cookie = 0; }
        d = probe_create();
        if (s->mode == MARSHAL_CTX) ActivateActCtx(ctx, &cookie);
    }
    if (s->mode == CREATE_CTX && cookie) { DeactivateActCtx(0, cookie); cookie = 0; }
    CoMarshalInterThreadInterfaceInStream(&IID_IDispatch, (IUnknown *)d, &s->inproc);
    CreateStreamOnHGlobal(NULL, FALSE, &stm);
    hr = CoMarshalInterface(stm, &IID_IDispatch, (IUnknown *)d, MSHCTX_LOCAL, NULL, MSHLFLAGS_TABLESTRONG);
    if (FAILED(hr)) printf("  CoMarshalInterface %08lx\n", hr);
    GetHGlobalFromStream(stm, &s->local);
    IStream_Release(stm);
    if (s->mode == MARSHAL_CTX && cookie) { DeactivateActCtx(0, cookie); cookie = 0; }
    SetEvent(s->ready);
    if (s->sta)
    {
        DWORD idx;
        CoWaitForMultipleHandles(0, INFINITE, 1, &s->done, &idx);
    }
    else WaitForSingleObject(s->done, INFINITE);
    if (cookie) DeactivateActCtx(0, cookie);
    IDispatch_Release(d);
    CoUninitialize();
    return 0;
}

static void run_child(struct server *s)
{
    WCHAR exe[MAX_PATH], cmd[MAX_PATH * 2], file[MAX_PATH];
    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi;
    DWORD written;
    HANDLE f;

    GetTempPathW(MAX_PATH, file);
    wcscat(file, L"actctx_comcall.bin");
    f = CreateFileW(file, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    WriteFile(f, GlobalLock(s->local), GlobalSize(s->local), &written, NULL);
    GlobalUnlock(s->local);
    CloseHandle(f);
    GetModuleFileNameW(NULL, exe, MAX_PATH);
    swprintf(cmd, ARRAYSIZE(cmd), L"\"%ls\" child \"%ls\"", exe, file);
    fflush(stdout);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    if (!CreateProcessW(NULL, cmd, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi))
    { printf("  CreateProcess failed %lu\n", GetLastError()); return; }
    /* main thread is an STA: pump while the child calls */
    {
        DWORD idx;
        CoWaitForMultipleHandles(0, 30000, 1, &pi.hProcess, &idx);
    }
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
}

static int child(const WCHAR *file)
{
    HANDLE f = CreateFileW(file, GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL);
    DWORD size = GetFileSize(f, NULL), rd;
    HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE, size);
    IDispatch *d;
    IStream *stm;
    HRESULT hr;

    ReadFile(f, GlobalLock(g), size, &rd, NULL);
    GlobalUnlock(g);
    CloseHandle(f);
    CoInitializeEx(NULL, COINIT_MULTITHREADED);
    CreateStreamOnHGlobal(g, TRUE, &stm);
    hr = CoUnmarshalInterface(stm, &IID_IDispatch, (void **)&d);
    if (FAILED(hr)) { printf("  child CoUnmarshalInterface %08lx\n", hr); return 1; }
    call(d, "cross-process (child MTA)");
    IDispatch_Release(d);
    IStream_Release(stm);
    CoUninitialize();
    fflush(stdout);
    return 0;
}

int wmain(int argc, WCHAR **argv)
{
    WCHAR buf[512];
    ULONG_PTR cookie;
    HANDLE ctx;
    int res, mode, sta;

    if (argc == 3 && !wcscmp(argv[1], L"child")) return child(argv[2]);

    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    GetModuleFileNameW(NULL, dllpath, MAX_PATH);
    wcscpy(wcsrchr(dllpath, '\\') + 1, L"probe.dll");
    dll = LoadLibraryW(dllpath);
    if (!dll) { printf("LoadLibrary failed %lu\n", GetLastError()); return 1; }
    probe_test = (void *)GetProcAddress(dll, "probe_test");
    probe_create = (void *)GetProcAddress(dll, "probe_create");

    probe_test(buf);
    printf("direct, main STA, nothing activated: %ls\n", buf);

    for (res = 1; res <= 3; res++)
    {
        int hm;
        for (hm = 1; hm >= 0; hm--)
        {
            if ((ctx = dll_ctx(hm, res)) == INVALID_HANDLE_VALUE) continue;
            ActivateActCtx(ctx, &cookie);
            probe_test(buf);
            printf("direct, activated %s res %d: %ls\n", hm ? "hmodule" : "path", res, buf);
            DeactivateActCtx(0, cookie);
            ReleaseActCtx(ctx);
        }
    }

    for (sta = 0; sta <= 1; sta++)
    for (mode = 0; mode < ARRAYSIZE(modes); mode++)
    {
        struct server s = { mode, sta };
        IDispatch *d;
        HANDLE th;

        printf("server object in %s, %s:\n", sta ? "STA" : "MTA", modes[mode]);
        s.ready = CreateEventW(NULL, TRUE, FALSE, NULL);
        s.done = CreateEventW(NULL, TRUE, FALSE, NULL);
        th = CreateThread(NULL, 0, server_thread, &s, 0, NULL);
        WaitForSingleObject(s.ready, INFINITE);
        if (SUCCEEDED(CoGetInterfaceAndReleaseStream(s.inproc, &IID_IDispatch, (void **)&d)))
        {
            HANDLE ctx = dll_ctx(TRUE, 2);
            ULONG_PTR cookie;
            call(d, "in-process (main STA)");
            ActivateActCtx(ctx, &cookie);
            call(d, "in-process, caller ctx active");
            DeactivateActCtx(0, cookie);
            ReleaseActCtx(ctx);
            IDispatch_Release(d);
        }
        run_child(&s);
        SetEvent(s.done);
        WaitForSingleObject(th, INFINITE);
        CloseHandle(th);
    }
    CoUninitialize();
    return 0;
}
