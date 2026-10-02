/* sta_endpoints.exe [N]: N rounds of: STA thread marshals an object, the main MTA thread unmarshals,
 * calls and releases it, the STA uninitializes and exits. Prints process handles by type at some
 * rounds (do dead apartments leave endpoints behind?) and ms per round (114).
 * x86_64-w64-mingw32-gcc -O2 -o sta_endpoints.exe sta_endpoints.c -lole32 -luuid -lntdll */
#define COBJMACROS
#include <windows.h>
#include <winternl.h>
#include <objbase.h>
#include <stdio.h>
#include <stdlib.h>

static HRESULT WINAPI qi(IPersist *i, REFIID iid, void **o)
{ if (IsEqualGUID(iid, &IID_IUnknown) || IsEqualGUID(iid, &IID_IPersist)) { *o = i; return S_OK; } *o = NULL; return E_NOINTERFACE; }
static ULONG WINAPI addref(IPersist *i) { return 2; }
static ULONG WINAPI release(IPersist *i) { return 1; }
static HRESULT WINAPI gci(IPersist *i, CLSID *c) { memset(c, 0, sizeof(*c)); return S_OK; }
static IPersistVtbl vt = { qi, addref, release, gci };
static IPersist obj = { &vt };
static IStream *stream;
static HANDLE ready, done;

static DWORD CALLBACK server(void *arg)
{
    MSG msg;
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    CoMarshalInterface(stream, &IID_IPersist, (IUnknown *)&obj, MSHCTX_INPROC, NULL, MSHLFLAGS_NORMAL);
    IStream_Seek(stream, (LARGE_INTEGER){{0}}, STREAM_SEEK_SET, NULL);
    PeekMessageA(&msg, NULL, 0, 0, PM_NOREMOVE);
    SetEvent(ready);
    while (MsgWaitForMultipleObjects(1, &done, FALSE, INFINITE, QS_ALLINPUT) == WAIT_OBJECT_0 + 1)
        while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) DispatchMessageA(&msg);
    CoUninitialize();
    return 0;
}

typedef struct { PVOID obj; ULONG_PTR pid, handle; ULONG access; USHORT bt, ti; ULONG attr, res; } HENTRY;
typedef struct { ULONG_PTR count, res; HENTRY h[1]; } HINFO;

static void dump_handles(int round)
{
    static char types[64][64]; int counts[64] = {0}, ntypes = 0, i, k, total = 0;
    ULONG size = 1 << 22; HINFO *info = malloc(size); char buf[1024];
    if (NtQuerySystemInformation(64 /* SystemExtendedHandleInformation */, info, size, &size)) { printf("query failed\n"); return; }
    for (i = 0; i < info->count; i++)
    {
        OBJECT_TYPE_INFORMATION *t = (void *)buf; char name[64];
        if (info->h[i].pid != GetCurrentProcessId()) continue;
        total++;
        if (NtQueryObject((HANDLE)info->h[i].handle, ObjectTypeInformation, buf, sizeof(buf), NULL)) continue;
        snprintf(name, sizeof(name), "%.*ls", (int)t->TypeName.Length / 2, t->TypeName.Buffer);
        for (k = 0; k < ntypes; k++) if (!strcmp(types[k], name)) break;
        if (k == ntypes && ntypes < 64) strcpy(types[ntypes++], name);
        if (k < 64) counts[k]++;
    }
    printf("round %d: %d handles:", round, total);
    for (k = 0; k < ntypes; k++) printf(" %s=%d", types[k], counts[k]);
    printf("\n");
    free(info);
}

int main(int argc, char **argv)
{
    int i, n = argc > 1 ? atoi(argv[1]) : 100;
    DWORD start = GetTickCount();
    setvbuf(stdout, NULL, _IONBF, 0);
    CoInitializeEx(NULL, COINIT_MULTITHREADED);
    ready = CreateEventA(NULL, FALSE, FALSE, NULL);
    done = CreateEventA(NULL, FALSE, FALSE, NULL);
    dump_handles(-1);
    for (i = 0; i < n; i++)
    {
        IPersist *p; CLSID c; HRESULT hr; HANDLE s;
        CreateStreamOnHGlobal(NULL, TRUE, &stream);
        s = CreateThread(NULL, 0, server, NULL, 0, NULL);
        if (WaitForSingleObject(ready, 10000)) { printf("round %d: marshal hangs\n", i); return 1; }
        hr = CoUnmarshalInterface(stream, &IID_IPersist, (void **)&p);
        if (!hr) { hr = IPersist_GetClassID(p, &c); IPersist_Release(p); }
        if (hr) printf("round %d: call %#lx\n", i, hr);
        SetEvent(done);
        if (WaitForSingleObject(s, 10000)) { printf("round %d: server hangs\n", i); return 1; }
        CloseHandle(s);
        IStream_Release(stream);
        if (i < 3 || i == 63 || i == 64 || i == n - 1) dump_handles(i);
    }
    printf("%d rounds, %lu ms\n", n, GetTickCount() - start);
    CoUninitialize();
    return 0;
}
