/* uninit_race.exe [N]: N rounds of: STA thread marshals an object, an MTA thread unmarshals and calls it
 * while the STA uninitializes at a random moment without pumping. Reports call results, object refs
 * after CoUninitialize and at the end of the round (expect 1), crashes. (112) */
#define COBJMACROS
#include <windows.h>
#include <objbase.h>
#include <stdio.h>
#include <stdlib.h>

static LONG refs;
static HRESULT WINAPI qi(IPersist *i, REFIID iid, void **o)
{ if (IsEqualGUID(iid, &IID_IUnknown) || IsEqualGUID(iid, &IID_IPersist)) { *o = i; InterlockedIncrement(&refs); return S_OK; } *o = NULL; return E_NOINTERFACE; }
static ULONG WINAPI addref(IPersist *i) { return InterlockedIncrement(&refs); }
static ULONG WINAPI release(IPersist *i) { return InterlockedDecrement(&refs); }
static HRESULT WINAPI gci(IPersist *i, CLSID *c) { memset(c, 0, sizeof(*c)); return S_OK; }
static IPersistVtbl vt = { qi, addref, release, gci };
static IPersist obj = { &vt };
static IStream *stream;
static HANDLE ready, go;
static LONG after_uninit;

static DWORD CALLBACK server(void *arg)
{
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    CreateStreamOnHGlobal(NULL, TRUE, &stream);
    CoMarshalInterface(stream, &IID_IPersist, (IUnknown *)&obj, MSHCTX_INPROC, NULL, MSHLFLAGS_NORMAL);
    IStream_Seek(stream, (LARGE_INTEGER){{0}}, STREAM_SEEK_SET, NULL);
    SetEvent(ready);
    WaitForSingleObject(go, INFINITE);
    if (rand() % 2) Sleep(rand() % 2);
    CoUninitialize();
    after_uninit = refs;
    return 0;
}

static DWORD CALLBACK client(void *arg)
{
    IPersist *p = NULL; CLSID c; HRESULT hr;
    CoInitializeEx(NULL, COINIT_MULTITHREADED);
    hr = CoUnmarshalInterface(stream, &IID_IPersist, (void **)&p);
    if (!hr) { hr = IPersist_GetClassID(p, &c); IPersist_Release(p); }
    CoUninitialize();
    return hr;
}

int main(int argc, char **argv)
{
    int i, k, n = argc > 1 ? atoi(argv[1]) : 1000, bad = 0;
    LONG hist[4] = {0}; HRESULT hrs[4] = {0};
    setvbuf(stdout, NULL, _IONBF, 0);
    ready = CreateEventA(NULL, FALSE, FALSE, NULL);
    go = CreateEventA(NULL, FALSE, FALSE, NULL);
    for (i = 0; i < n; i++)
    {
        HANDLE s, c; DWORD code;
        refs = 1;
        if (getenv("RACE_VERBOSE")) printf("round %d\n", i);
        s = CreateThread(NULL, 0, server, NULL, 0, NULL);
        WaitForSingleObject(ready, INFINITE);
        c = CreateThread(NULL, 0, getenv("RACE_NOCALL") ? (LPTHREAD_START_ROUTINE)Sleep : client, NULL, 0, NULL);
        SetEvent(go);
        if (WaitForSingleObject(c, 10000)) { printf("round %d: client hangs\n", i); return 1; }
        if (WaitForSingleObject(s, 10000)) { printf("round %d: server hangs\n", i); return 1; }
        GetExitCodeThread(c, &code);
        for (k = 0; k < 4; k++) if (!hist[k] || hrs[k] == code) { hrs[k] = code; hist[k]++; break; }
        if (code == 0xc0000005 || refs != 1) { bad++; printf("round %d: client %#lx, refs after uninit %ld, now %ld\n", i, code, after_uninit, refs); }
        IStream_Release(stream);
        CloseHandle(s); CloseHandle(c);
    }
    for (k = 0; k < 4 && hist[k]; k++) printf("client %#lx: %ld\n", hrs[k], hist[k]);
    printf("%d bad rounds of %d\n", bad, n);
    return 0;
}
