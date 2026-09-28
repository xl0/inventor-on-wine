/* comctl32 SetWindowSubclass ground truth (046): props used by v5/v6, both versions
 * on one window, cross-thread / cross-process calls, removal during nested calls,
 * destroy during a callback. Prints observations only.
 * Build: x86_64-w64-mingw32-gcc -O2 -o subclass_probe.exe subclass_probe.c -lcomctl32 */
#include <windows.h>
#include <commctrl.h>
#include <stdio.h>

typedef BOOL (WINAPI *SETSUB)(HWND, SUBCLASSPROC, UINT_PTR, DWORD_PTR);
typedef BOOL (WINAPI *GETSUB)(HWND, SUBCLASSPROC, UINT_PTR, DWORD_PTR *);
typedef BOOL (WINAPI *REMSUB)(HWND, SUBCLASSPROC, UINT_PTR);
typedef LRESULT (WINAPI *DEFSUB)(HWND, UINT, WPARAM, LPARAM);

struct cc { const char *name; HMODULE mod; SETSUB set; GETSUB get; REMSUB rem; DEFSUB def; };
static struct cc v5, v6;
static char log_buf[4096];

static void logm(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vsprintf(log_buf + strlen(log_buf), fmt, args);
    va_end(args);
}

static void load(struct cc *c, const char *name)
{
    c->name = name;
    c->mod = LoadLibraryA("comctl32.dll");
    c->set = (SETSUB)GetProcAddress(c->mod, (LPCSTR)410);
    c->get = (GETSUB)GetProcAddress(c->mod, (LPCSTR)411);
    c->rem = (REMSUB)GetProcAddress(c->mod, (LPCSTR)412);
    c->def = (DEFSUB)GetProcAddress(c->mod, (LPCSTR)413);
}

static LRESULT WINAPI base_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_USER) logm("base ");
    if (msg == WM_NCDESTROY) logm("base:NCDESTROY ");
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static HWND mkwnd(void)
{
    return CreateWindowW(L"sbprobe", L"sbprobe", WS_OVERLAPPEDWINDOW, 0, 0, 100, 100, 0, 0, 0, 0);
}

static BOOL CALLBACK enum_prop(HWND hwnd, LPWSTR name, HANDLE data, ULONG_PTR param)
{
    if (IS_INTRESOURCE(name)) printf(" #%u", LOWORD(name));
    else printf(" %ls", name);
    return TRUE;
}

static void dump(HWND hwnd, const char *what)
{
    WNDPROC p = (WNDPROC)GetWindowLongPtrW(hwnd, GWLP_WNDPROC);
    printf("  %s: wndproc %s, props:", what, p == base_proc ? "base" : "other");
    EnumPropsExW(hwnd, enum_prop, 0);
    printf("\n");
}

/* generic logging subclass; ref selects behaviour */
#define R_NEST 1        /* send a nested WM_USER (lp=1) */
#define R_REMNEXT 2     /* in nested call (lp=1), remove subclass id+1 of same version */
#define R_DESTROY 4     /* DestroyWindow before DefSubclassProc */
#define R_REMSELF 8     /* remove self */
static struct cc *ver_of[16];

static LRESULT CALLBACK sub(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR id, DWORD_PTR ref)
{
    struct cc *c = ver_of[id];
    if (msg == WM_NCDESTROY)
    {
        DWORD_PTR r;
        LRESULT ret;
        logm("%s#%u:NCDESTROY ", c->name, (int)id);
        ret = c->def(hwnd, msg, wp, lp);
        logm("(after def get=%d) ", c->get(hwnd, sub, id, &r));
        return ret;
    }
    if (msg != WM_USER) return c->def(hwnd, msg, wp, lp);
    logm("%s#%u%s ", c->name, (int)id, lp ? "n" : "");
    if (!lp && (ref & R_NEST)) SendMessageW(hwnd, WM_USER, 0, 1);
    if (lp && (ref & R_REMNEXT)) logm("[rem%u=%d] ", (int)id + 1, c->rem(hwnd, sub, id + 1));
    if (!lp && (ref & R_REMSELF)) logm("[remself=%d] ", c->rem(hwnd, sub, id));
    if (!lp && (ref & R_DESTROY)) logm("[destroy=%d] ", DestroyWindow(hwnd));
    return c->def(hwnd, msg, wp, lp);
}

static void sendu(HWND hwnd, const char *what)
{
    log_buf[0] = 0;
    SendMessageW(hwnd, WM_USER, 0, 0);
    printf("  %s: %s\n", what, log_buf);
}

/* ---- cross thread ---- */
static HWND thread_hwnd;
static HANDLE ready, done;
static DWORD WINAPI thread_proc(void *arg)
{
    struct cc *c = arg;
    MSG m;
    thread_hwnd = mkwnd();
    if (c) c->set(thread_hwnd, sub, c == &v5 ? 1 : 3, 0);
    SetEvent(ready);
    while (WaitForSingleObject(done, 0) && GetMessageW(&m, 0, 0, 0)) DispatchMessageW(&m);
    return 0;
}

static void cross_thread(struct cc *owner, struct cc *c)
{
    HANDLE th;
    DWORD_PTR r = 0xdead;
    ready = CreateEventW(0, 0, 0, 0);
    done = CreateEventW(0, 1, 0, 0);
    th = CreateThread(0, 0, thread_proc, owner, 0, 0);
    WaitForSingleObject(ready, INFINITE);
    printf("cross-thread: owner subclass %s, caller %s\n", owner ? owner->name : "none", c->name);
    dump(thread_hwnd, "before");
    int oid = owner == &v5 ? 1 : 3, cid = c == &v5 ? 2 : 4;
    SetLastError(0xdead);
    printf("  set(id %d)=%d err=%lu\n", cid, c->set(thread_hwnd, sub, cid, 0), GetLastError());
    SetLastError(0xdead);
    printf("  get(id %d)=%d ref=%#llx err=%lu\n", oid, c->get(thread_hwnd, sub, oid, &r), (unsigned long long)r, GetLastError());
    SetLastError(0xdead);
    printf("  rem(id %d)=%d err=%lu\n", oid, c->rem(thread_hwnd, sub, oid), GetLastError());
    SetLastError(0xdead);
    printf("  def(WM_USER)=%lld err=%lu\n", (long long)c->def(thread_hwnd, WM_USER + 1, 0, 0), GetLastError());
    dump(thread_hwnd, "after");
    sendu(thread_hwnd, "send");
    dump(thread_hwnd, "after send");
    SetEvent(done);
    PostMessageW(thread_hwnd, WM_NULL, 0, 0);
    WaitForSingleObject(th, INFINITE);
}

static void child(void)
{
    HWND hwnd = CreateWindowW(L"sbprobe", L"sbprobe-child", WS_OVERLAPPEDWINDOW, 0, 0, 100, 100, 0, 0, 0, 0);
    MSG m;
    DWORD end = GetTickCount() + 5000;
    v5.set(hwnd, sub, 1, 0);
    v6.set(hwnd, sub, 3, 0);
    while (GetTickCount() < end)
    {
        while (PeekMessageW(&m, 0, 0, 0, PM_REMOVE)) DispatchMessageW(&m);
        Sleep(10);
    }
}

static void cross_process(const char *exe)
{
    STARTUPINFOA si = {sizeof(si)};
    PROCESS_INFORMATION pi;
    char cmd[MAX_PATH + 16];
    HWND hwnd = 0;
    DWORD_PTR r = 0xdead;
    int i;
    sprintf(cmd, "\"%s\" child", exe);
    CreateProcessA(0, cmd, 0, 0, 0, 0, 0, 0, &si, &pi);
    for (i = 0; i < 200 && !hwnd; i++) { Sleep(10); hwnd = FindWindowW(L"sbprobe", L"sbprobe-child"); }
    Sleep(200);
    printf("cross-process (child subclassed with v5#1, v6#3): hwnd %p\n", hwnd);
    dump(hwnd, "props");
    printf("  v5 get(1)=%d ", v5.get(hwnd, sub, 1, &r));
    printf("v6 get(3)=%d\n", v6.get(hwnd, sub, 3, &r));
    SetLastError(0xdead);
    printf("  v5 set(2)=%d err=%lu ", v5.set(hwnd, sub, 2, 0), GetLastError());
    SetLastError(0xdead);
    printf("v6 set(4)=%d err=%lu\n", v6.set(hwnd, sub, 4, 0), GetLastError());
    printf("  v5 rem(1)=%d v6 rem(3)=%d\n", v5.rem(hwnd, sub, 1), v6.rem(hwnd, sub, 3));
    WaitForSingleObject(pi.hProcess, INFINITE);
}

int main(int argc, char **argv)
{
    WNDCLASSW cls = {0};
    ACTCTXA ctx = {sizeof(ctx)};
    char manifest[MAX_PATH];
    HANDLE hctx;
    ULONG_PTR cookie;
    FILE *f;
    HWND hwnd;
    int i;

    cls.lpfnWndProc = base_proc;
    cls.lpszClassName = L"sbprobe";
    RegisterClassW(&cls);

    load(&v5, "v5");
    GetTempPathA(MAX_PATH, manifest);
    strcat(manifest, "sbprobe.manifest");
    f = fopen(manifest, "w");
    fputs("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
          "<assembly xmlns=\"urn:schemas-microsoft-com:asm.v1\" manifestVersion=\"1.0\">"
          "<dependency><dependentAssembly><assemblyIdentity type=\"win32\" "
          "name=\"Microsoft.Windows.Common-Controls\" version=\"6.0.0.0\" processorArchitecture=\"*\" "
          "publicKeyToken=\"6595b64144ccf1df\" language=\"*\"/></dependentAssembly></dependency></assembly>", f);
    fclose(f);
    ctx.lpSource = manifest;
    hctx = CreateActCtxA(&ctx);
    ActivateActCtx(hctx, &cookie);
    load(&v6, "v6");
    DeactivateActCtx(0, cookie);
    for (i = 0; i < 16; i++) ver_of[i] = (i >= 3 && i < 5) || i >= 8 ? &v6 : &v5;

    if (argc > 1 && !strcmp(argv[1], "child")) { child(); return 0; }
    printf("v5 %p v6 %p (%s)\n", v5.mod, v6.mod, v5.mod == v6.mod ? "same" : "distinct");

    /* ids 1,2 (and 5..7) -> v5; 3,4 and 8+ -> v6 */
    printf("props:\n");
    hwnd = mkwnd(); v5.set(hwnd, sub, 1, 0); dump(hwnd, "v5 only");
    v6.set(hwnd, sub, 3, 0); dump(hwnd, "v5 then v6");
    sendu(hwnd, "call order");
    v5.rem(hwnd, sub, 1); dump(hwnd, "v5 removed");
    sendu(hwnd, "call order");
    v6.rem(hwnd, sub, 3); dump(hwnd, "v6 removed");
    DestroyWindow(hwnd);

    hwnd = mkwnd(); v6.set(hwnd, sub, 3, 0); v5.set(hwnd, sub, 1, 0); dump(hwnd, "v6 then v5");
    sendu(hwnd, "call order");
    v6.rem(hwnd, sub, 3); dump(hwnd, "v6 removed");
    v5.rem(hwnd, sub, 1); dump(hwnd, "v5 removed");
    DestroyWindow(hwnd);

    printf("nested remove of the next subclass:\n");
    hwnd = mkwnd(); v5.set(hwnd, sub, 6, 0); v5.set(hwnd, sub, 5, R_NEST | R_REMNEXT);
    sendu(hwnd, "v5 #5 nest, nested removes #6");
    sendu(hwnd, "again");
    DestroyWindow(hwnd);
    hwnd = mkwnd(); v6.set(hwnd, sub, 9, 0); v6.set(hwnd, sub, 8, R_NEST | R_REMNEXT);
    sendu(hwnd, "v6 #8 nest, nested removes #9");
    sendu(hwnd, "again");
    DestroyWindow(hwnd);

    printf("remove self then last:\n");
    hwnd = mkwnd(); v6.set(hwnd, sub, 8, R_REMSELF);
    sendu(hwnd, "v6 remself"); dump(hwnd, "after");
    DestroyWindow(hwnd);

    printf("destroy during callback / NCDESTROY:\n");
    hwnd = mkwnd(); v5.set(hwnd, sub, 1, 0); v5.set(hwnd, sub, 2, R_DESTROY);
    sendu(hwnd, "v5 destroy");
    hwnd = mkwnd(); v6.set(hwnd, sub, 3, 0); v6.set(hwnd, sub, 4, R_DESTROY);
    sendu(hwnd, "v6 destroy");
    hwnd = mkwnd(); v6.set(hwnd, sub, 3, 0); v5.set(hwnd, sub, 1, 0);
    log_buf[0] = 0; DestroyWindow(hwnd); printf("  v6+v5 DestroyWindow: %s\n", log_buf);

    cross_thread(NULL, &v5);
    cross_thread(NULL, &v6);
    cross_thread(&v5, &v5);
    cross_thread(&v6, &v6);
    cross_thread(&v5, &v6);
    cross_thread(&v6, &v5);
    cross_process(argv[0]);
    return 0;
}
