/* 070: black-box check of what mfc140u's AFX_MAINTAIN_STATE2 (AFX_MANAGE_STATE) activates for
 * Inventor's FEA_Application_Common.dll, whose manifest (RT_MANIFEST 2) declares
 * {A9485C80-1DCC-4F77-9F26-E2569F4C03F1}. Loads the DLL from Inventor's Bin dir, then on several
 * threads: GetCurrentActCtx + CoCreateInstance(A9485C80) without / inside AFX_MAINTAIN_STATE2 of
 * the DLL's static module state (RVA 0x96740, what its FUN_18004031c returns; same binary sha256
 * 79f01966... on the VM and inv3). mfc140u ordinals from the DLL's import table:
 * 0x143 AFX_MAINTAIN_STATE2 ctor, 0x40f dtor.
 * Build: x86_64-w64-mingw32-gcc -O2 -municode -o mfc_state.exe mfc_state.c -lole32 */
#include <windows.h>
#include <stdio.h>

static const GUID clsid = {0xa9485c80,0x1dcc,0x4f77,{0x9f,0x26,0xe2,0x56,0x9f,0x4c,0x03,0xf1}};
static void (*ctor)(void *, void *), (*dtor)(void *);
static char *state;
static HANDLE modctx;

static void test(const char *what)
{
    HANDLE ctx = NULL;
    IUnknown *unk = NULL;
    HRESULT hr;
    GetCurrentActCtx(&ctx);
    hr = CoCreateInstance(&clsid, NULL, 0x17, &IID_IUnknown, (void **)&unk);
    printf("  %-34s ctx %p%s hr %08lx\n", what, ctx, ctx && ctx == modctx ? " (=module ctx)" : "", hr);
    if (unk) unk->lpVtbl->Release(unk);
    if (ctx) ReleaseActCtx(ctx);
}

static void run(const char *name)
{
    char buf[256] = {0};
    printf("%s (tid %lu):\n", name, GetCurrentThreadId());
    test("plain");
    ctor(buf, state);
    test("inside AFX_MAINTAIN_STATE2");
    dtor(buf);
    test("after");
}

static DWORD WINAPI thread(void *arg)
{
    int mode = (int)(INT_PTR)arg;
    if (mode) CoInitializeEx(NULL, mode == 1 ? COINIT_MULTITHREADED : COINIT_APARTMENTTHREADED);
    run(mode == 0 ? "new thread, no CoInitialize" : mode == 1 ? "new thread, MTA" : "new thread, STA");
    if (mode) CoUninitialize();
    return 0;
}

int wmain(int argc, WCHAR **argv)
{
    ACTIVATION_CONTEXT_BASIC_INFORMATION basic = {0};
    HMODULE fea, mfc;
    int i;

    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    {
        static WCHAR path[32768] = L"C:\\Program Files\\Autodesk\\Inventor 2027\\Bin;C:\\Program Files\\Common Files\\Autodesk Shared\\Components\\2027\\2.2.0;";
        GetEnvironmentVariableW(L"PATH", path + wcslen(path), 30000);
        SetEnvironmentVariableW(L"PATH", path);
        SetCurrentDirectoryW(L"C:\\Program Files\\Autodesk\\Inventor 2027\\Bin");
    }
    fea = LoadLibraryExW(L"C:\\Program Files\\Autodesk\\Inventor 2027\\Bin\\FEA_Application_Common.dll",
                         NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!fea) { printf("LoadLibrary failed %lu\n", GetLastError()); return 1; }
    mfc = GetModuleHandleW(L"mfc140u.dll");
    ctor = (void *)GetProcAddress(mfc, (char *)0x143);
    dtor = (void *)GetProcAddress(mfc, (char *)0x40f);
    state = (char *)fea + 0x96740;
    if (QueryActCtxW(QUERY_ACTCTX_FLAG_ACTCTX_IS_HMODULE | QUERY_ACTCTX_FLAG_NO_ADDREF, fea, NULL,
                     ActivationContextBasicInformation, &basic, sizeof(basic), NULL))
        modctx = basic.hActCtx;
    printf("module ctx (QueryActCtx IS_HMODULE, basic) %p\n", modctx);
    /* dump the pointer-sized fields of the module state that look like handles */
    for (i = 0; i < 0x300; i += 8)
    {
        void *v = *(void **)(state + i);
        if (v == INVALID_HANDLE_VALUE || (v && v == modctx)) printf("  state+%#x = %p\n", i, v);
    }
    run("main thread, STA");
    for (i = 0; i < 3; i++)
    {
        HANDLE t = CreateThread(NULL, 0, thread, (void *)(INT_PTR)i, 0, NULL);
        WaitForSingleObject(t, INFINITE);
        CloseHandle(t);
    }
    fflush(stdout);
    return 0;
}
