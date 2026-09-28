/* RegisterDragDrop / RevokeDragDrop edge cases (034): return codes, AddRef/Release
 * counts of the app's IDropTarget, window props, cross-thread / cross-apartment /
 * cross-process revoke, revoke after OleUninitialize, window destruction.
 * argv[1] = "child" is the cross-process helper.
 * Build: x86_64-w64-mingw32-gcc -O2 -o dragdrop_revoke.exe dragdrop_revoke.c -lole32 -luuid -luser32 */
#define COBJMACROS
#include <windows.h>
#include <ole2.h>
#include <stdio.h>

typedef struct { IDropTarget iface; LONG addref, release, qi; DWORD last_tid; } target;

static target *impl(IDropTarget *i) { return (target *)i; }
static HRESULT WINAPI qi(IDropTarget *i, REFIID riid, void **out)
{
    impl(i)->qi++;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IDropTarget))
    { *out = i; IDropTarget_AddRef(i); return S_OK; }
    *out = NULL; return E_NOINTERFACE;
}
static ULONG WINAPI addref(IDropTarget *i) { impl(i)->last_tid = GetCurrentThreadId(); return 1 + InterlockedIncrement(&impl(i)->addref); }
static ULONG WINAPI release(IDropTarget *i) { impl(i)->last_tid = GetCurrentThreadId(); return 1 + InterlockedIncrement(&impl(i)->release); }
static HRESULT WINAPI enter(IDropTarget *i, IDataObject *d, DWORD k, POINTL p, DWORD *e) { return S_OK; }
static HRESULT WINAPI over(IDropTarget *i, DWORD k, POINTL p, DWORD *e) { return S_OK; }
static HRESULT WINAPI leave(IDropTarget *i) { return S_OK; }
static HRESULT WINAPI drop(IDropTarget *i, IDataObject *d, DWORD k, POINTL p, DWORD *e) { return S_OK; }
static IDropTargetVtbl vtbl = { qi, addref, release, enter, over, leave, drop };

static void init(target *t) { memset(t, 0, sizeof(*t)); t->iface.lpVtbl = &vtbl; }
static void show(const char *what, HRESULT hr, target *t)
{
    printf("%-44s hr %08lx  addref %ld release %ld qi %ld%s\n", what, hr, t->addref, t->release, t->qi,
           t->last_tid && t->last_tid != GetCurrentThreadId() ? "  (last ref op on other thread)" : "");
}

static BOOL CALLBACK prop_cb(HWND hwnd, LPWSTR name, HANDLE data, ULONG_PTR param)
{
    if (IS_INTRESOURCE(name)) printf("    prop #%u = %p\n", LOWORD(name), data);
    else printf("    prop %ls = %p%s\n", name, data, data == (HANDLE)param ? " (target)" : "");
    return TRUE;
}

static HWND mkwin(void) { return CreateWindowA("static", "dd", WS_OVERLAPPEDWINDOW, 0, 0, 100, 100, 0, 0, 0, 0); }

/* thread helpers */
struct job { int op; HWND hwnd; target *t; HRESULT hr; HANDLE go, done; };
enum { OP_REGISTER, OP_REVOKE, OP_REVOKE_NOCOM, OP_REVOKE_MTA, OP_OWN_REGISTER_UNINIT, OP_OWN_REGISTER_EXIT };

static DWORD WINAPI thread(void *arg)
{
    struct job *j = arg;
    MSG msg;
    if (j->op == OP_REVOKE_MTA) CoInitializeEx(NULL, COINIT_MULTITHREADED);
    else if (j->op != OP_REVOKE_NOCOM) OleInitialize(NULL);
    switch (j->op)
    {
    case OP_REGISTER: j->hr = RegisterDragDrop(j->hwnd, &j->t->iface); break;
    case OP_REVOKE: case OP_REVOKE_NOCOM: case OP_REVOKE_MTA: j->hr = RevokeDragDrop(j->hwnd); break;
    case OP_OWN_REGISTER_UNINIT: case OP_OWN_REGISTER_EXIT:
        j->hwnd = mkwin();
        j->hr = RegisterDragDrop(j->hwnd, &j->t->iface);
        SetEvent(j->done);
        WaitForSingleObject(j->go, INFINITE);
        while (PeekMessageA(&msg, 0, 0, 0, PM_REMOVE)) DispatchMessageA(&msg);
        if (j->op == OP_OWN_REGISTER_UNINIT) { OleUninitialize(); SetEvent(j->done); WaitForSingleObject(j->go, INFINITE); }
        return 0; /* window stays (owner thread exit destroys it) */
    }
    if (j->op == OP_REVOKE_MTA || j->op == OP_REVOKE_NOCOM) { if (j->op == OP_REVOKE_MTA) CoUninitialize(); }
    else OleUninitialize();
    return 0;
}

static HRESULT run_on_thread(int op, HWND hwnd, target *t)
{
    struct job j = { op, hwnd, t };
    HANDLE h = CreateThread(NULL, 0, thread, &j, 0, NULL);
    /* pump while waiting: cross-thread Release may need our apartment */
    while (MsgWaitForMultipleObjects(1, &h, FALSE, 5000, QS_ALLINPUT) == WAIT_OBJECT_0 + 1)
    { MSG msg; while (PeekMessageA(&msg, 0, 0, 0, PM_REMOVE)) DispatchMessageA(&msg); }
    CloseHandle(h);
    return j.hr;
}

static int child(void)
{
    target t; HWND hwnd; HRESULT hr;
    HANDLE ready = OpenEventA(EVENT_ALL_ACCESS, FALSE, "dd034_ready"), go = OpenEventA(EVENT_ALL_ACCESS, FALSE, "dd034_go");
    OleInitialize(NULL);
    init(&t);
    hwnd = mkwin();
    SetWindowTextA(hwnd, "dd034_child");
    hr = RegisterDragDrop(hwnd, &t.iface);
    printf("child: register hr %08lx target %p\n", hr, &t);
    fflush(stdout);
    SetEvent(ready);
    while (MsgWaitForMultipleObjects(1, &go, FALSE, 10000, QS_ALLINPUT) == WAIT_OBJECT_0 + 1)
    { MSG msg; while (PeekMessageA(&msg, 0, 0, 0, PM_REMOVE)) DispatchMessageA(&msg); }
    show("child: after parent's revoke", 0, &t);
    printf("child: own revoke hr %08lx\n", RevokeDragDrop(hwnd));
    show("child: after own revoke", 0, &t);
    fflush(stdout);
    DestroyWindow(hwnd);
    OleUninitialize();
    return 0;
}

int main(int argc, char **argv)
{
    target t;
    HWND hwnd;
    HRESULT hr;

    setvbuf(stdout, NULL, _IONBF, 0);
    if (argc > 1 && !strcmp(argv[1], "child")) return child();

    /* no OleInitialize */
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    init(&t); hwnd = mkwin();
    show("register, CoInitialize(STA) only", RegisterDragDrop(hwnd, &t.iface), &t);
    show("revoke, CoInitialize(STA) only", RevokeDragDrop(hwnd), &t);
    DestroyWindow(hwnd);
    CoUninitialize();

    OleInitialize(NULL);

    init(&t); hwnd = mkwin();
    show("register", RegisterDragDrop(hwnd, &t.iface), &t);
    printf("    tid %04lx pid %04lx\n", GetCurrentThreadId(), GetCurrentProcessId());
    EnumPropsExW(hwnd, prop_cb, (LPARAM)&t);
    show("register again", RegisterDragDrop(hwnd, &t.iface), &t);
    show("revoke", RevokeDragDrop(hwnd), &t);
    EnumPropsExW(hwnd, prop_cb, (LPARAM)&t);
    show("revoke again", RevokeDragDrop(hwnd), &t);
    DestroyWindow(hwnd);
    show("revoke destroyed hwnd", RevokeDragDrop(hwnd), &t);
    show("revoke NULL", RevokeDragDrop(NULL), &t);

    init(&t); hwnd = mkwin();
    RegisterDragDrop(hwnd, &t.iface);
    DestroyWindow(hwnd);
    show("register, DestroyWindow (no revoke)", 0, &t);
    show("  then revoke", RevokeDragDrop(hwnd), &t);


    init(&t); hwnd = mkwin();
    RegisterDragDrop(hwnd, &t.iface);
    RemovePropW(hwnd, L"OleDropTargetInterface");
    show("register, remove OleDropTargetInterface prop", 0, &t);
    show("  then revoke", RevokeDragDrop(hwnd), &t);
    DestroyWindow(hwnd);

    /* cross-thread */
    init(&t); hwnd = mkwin();
    show("other STA registers our window", run_on_thread(OP_REGISTER, hwnd, &t), &t);
    show("  we revoke", RevokeDragDrop(hwnd), &t);
    DestroyWindow(hwnd);

    init(&t); hwnd = mkwin();
    show("register", RegisterDragDrop(hwnd, &t.iface), &t);
    show("  other STA revokes", run_on_thread(OP_REVOKE, hwnd, &t), &t);
    EnumPropsExW(hwnd, prop_cb, (LPARAM)&t);
    show("  we revoke", RevokeDragDrop(hwnd), &t);
    DestroyWindow(hwnd);
    OleUninitialize();
    show("  we OleUninitialize", 0, &t);
    OleInitialize(NULL);

    init(&t); hwnd = mkwin();
    show("register", RegisterDragDrop(hwnd, &t.iface), &t);
    show("  MTA thread revokes", run_on_thread(OP_REVOKE_MTA, hwnd, &t), &t);
    show("  we revoke", RevokeDragDrop(hwnd), &t);
    DestroyWindow(hwnd);

    init(&t); hwnd = mkwin();
    show("register", RegisterDragDrop(hwnd, &t.iface), &t);
    show("  no-COM thread revokes", run_on_thread(OP_REVOKE_NOCOM, hwnd, &t), &t);
    show("  we revoke", RevokeDragDrop(hwnd), &t);
    DestroyWindow(hwnd);

    /* registering thread uninitializes / exits before revoke */
    {
        struct job j = { OP_OWN_REGISTER_UNINIT };
        HANDLE h;
        init(&t); j.t = &t;
        j.go = CreateEventA(NULL, FALSE, FALSE, NULL); j.done = CreateEventA(NULL, FALSE, FALSE, NULL);
        h = CreateThread(NULL, 0, thread, &j, 0, NULL);
        WaitForSingleObject(j.done, INFINITE);
        show("thread registers own window", j.hr, &t);
        SetEvent(j.go); WaitForSingleObject(j.done, INFINITE);
        show("  thread OleUninitialize (window alive)", 0, &t);
        show("  we revoke", RevokeDragDrop(j.hwnd), &t);
        show("  revoke again", RevokeDragDrop(j.hwnd), &t);
        SetEvent(j.go); WaitForSingleObject(h, INFINITE); CloseHandle(h);

        init(&t); j.op = OP_OWN_REGISTER_EXIT;
        h = CreateThread(NULL, 0, thread, &j, 0, NULL);
        WaitForSingleObject(j.done, INFINITE);
        SetEvent(j.go); WaitForSingleObject(h, INFINITE); CloseHandle(h);
        show("thread registers own window, exits", j.hr, &t);
        show("  we revoke (window gone)", RevokeDragDrop(j.hwnd), &t);
    }

    /* same thread: OleUninitialize before revoke */
    init(&t); hwnd = mkwin();
    RegisterDragDrop(hwnd, &t.iface);
    OleUninitialize();
    show("register, OleUninitialize", 0, &t);
    show("  revoke without COM", RevokeDragDrop(hwnd), &t);
    OleInitialize(NULL);
    show("  revoke after OleInitialize", RevokeDragDrop(hwnd), &t);
    DestroyWindow(hwnd);

    init(&t); hwnd = mkwin();
    RegisterDragDrop(hwnd, &t.iface);
    OleUninitialize();
    OleInitialize(NULL);
    show("register, OleUninitialize, OleInitialize", 0, &t);
    EnumPropsExW(hwnd, prop_cb, (LPARAM)&t);
    show("  revoke", RevokeDragDrop(hwnd), &t);
    DestroyWindow(hwnd);

    init(&t); hwnd = mkwin();
    RegisterDragDrop(hwnd, &t.iface);
    DestroyWindow(hwnd);
    OleUninitialize();
    OleInitialize(NULL);
    show("register, DestroyWindow, OleUninitialize", 0, &t);

    /* cross-process */
    {
        STARTUPINFOA si = { sizeof(si) };
        PROCESS_INFORMATION pi;
        char cmd[MAX_PATH + 16];
        HANDLE ready = CreateEventA(NULL, FALSE, FALSE, "dd034_ready"), go = CreateEventA(NULL, FALSE, FALSE, "dd034_go");
        GetModuleFileNameA(NULL, cmd, MAX_PATH);
        strcat(cmd, " child");
        CreateProcessA(NULL, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi);
        WaitForSingleObject(ready, 10000);
        hwnd = FindWindowA("static", "dd034_child");
        printf("child window %p, its OleDropTargetInterface prop %p\n", hwnd, GetPropW(hwnd, L"OleDropTargetInterface"));
        hr = RevokeDragDrop(hwnd);
        printf("revoke other process's window: hr %08lx\n", hr);
        EnumPropsExW(hwnd, prop_cb, 0);
        init(&t);
        show("register on other process's window", RegisterDragDrop(hwnd, &t.iface), &t);
        EnumPropsExW(hwnd, prop_cb, (LPARAM)&t);
        show("  revoke it", RevokeDragDrop(hwnd), &t);
        SetEvent(go);
        WaitForSingleObject(pi.hProcess, 10000);
    }
    /* last: Wine releases the prop value (crashes here), Windows its own pointer */
    init(&t); hwnd = mkwin();
    RegisterDragDrop(hwnd, &t.iface);
    SetPropW(hwnd, L"OleDropTargetInterface", (HANDLE)0x1234);
    show("register, overwrite OleDropTargetInterface prop", 0, &t);
    show("  then revoke", RevokeDragDrop(hwnd), &t);
    EnumPropsExW(hwnd, prop_cb, (LPARAM)&t);
    DestroyWindow(hwnd);
    OleUninitialize();
    printf("done\n");
    return 0;
}
