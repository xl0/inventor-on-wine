/* Does a loader operation in thread B block while thread A is inside a DllMain
 * (PROCESS_ATTACH of dm_block.dll)? Issue 048: CoreCLR thread LoadLibraryExW()s an
 * IL-only framework assembly while the main thread runs managed code from a DllMain.
 * Usage: loader_dllmain.exe MODE [IL_DLL_PATH]
 *   MODE: none (baseline), new (LoadLibrary of an unloaded native DLL), loaded (LoadLibrary of a loaded one),
 *   getmod, getproc, pcfile (RtlPcToFileHeader), il (LoadLibraryEx(IL_DLL_PATH, LOAD_WITH_ALTERED_SEARCH_PATH)),
 *   ildata (same with LOAD_LIBRARY_AS_DATAFILE), thread (CreateThread, thread runs)
 * Prints "MODE: returned in N ms" or "MODE: blocked (>3000 ms), returned N ms after release".
 * x86_64-w64-mingw32-gcc -O2 -o loader_dllmain.exe loader_dllmain.c */
#include <windows.h>
#include <stdio.h>
#include <string.h>

static const char *mode;
static const char *il_path;
static HANDLE ev_in, ev_rel, ev_go, ev_bup, ev_done;
static PVOID (WINAPI *pRtlPcToFileHeader)(PVOID, PVOID *);

static DWORD WINAPI thread_a(void *arg)
{
    HMODULE h = LoadLibraryA("dm_block.dll");
    if (!h) printf("dm_block.dll load failed %lu\n", GetLastError());
    return 0;
}

static DWORD WINAPI trivial(void *arg) { return 7; }

static DWORD WINAPI thread_b(void *arg)
{
    void *r = NULL;
    DWORD err;
    SetEvent(ev_bup);
    WaitForSingleObject(ev_go, INFINITE);
    if (!strcmp(mode, "new")) r = LoadLibraryA("dm_plain.dll");
    else if (!strcmp(mode, "loaded")) r = LoadLibraryA("dm_plain.dll");
    else if (!strcmp(mode, "getmod")) r = GetModuleHandleA("dm_plain.dll");
    else if (!strcmp(mode, "getproc")) r = GetProcAddress(GetModuleHandleA("dm_plain.dll"), "dm_func");
    else if (!strcmp(mode, "il")) r = LoadLibraryExA(il_path, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
    else if (!strcmp(mode, "ildata")) r = LoadLibraryExA(il_path, NULL, LOAD_LIBRARY_AS_DATAFILE);
    else if (!strcmp(mode, "none")) r = (void *)1;
    else if (!strcmp(mode, "pcfile")) pRtlPcToFileHeader((void *)thread_b, &r);  /* as C++ throw does */
    else if (!strcmp(mode, "thread"))
    {
        HANDLE t = CreateThread(NULL, 0, trivial, NULL, 0, NULL);
        WaitForSingleObject(t, INFINITE);
        r = t;
    }
    err = GetLastError();
    if (!r) printf("%s: failed, error %lu\n", mode, err);
    SetEvent(ev_done);  /* thread exit itself waits for the loader lock (THREAD_DETACH) */
    return 0;
}

int main(int argc, char **argv)
{
    HANDLE a, b;
    DWORD t0, t1;

    if (argc < 2) return 2;
    mode = argv[1];
    il_path = argc > 2 ? argv[2] : "";
    setvbuf(stdout, NULL, _IONBF, 0);
    pRtlPcToFileHeader = (void *)GetProcAddress(GetModuleHandleA("ntdll.dll"), "RtlPcToFileHeader");
    if (!strcmp(mode, "loaded") || !strcmp(mode, "getmod") || !strcmp(mode, "getproc"))
        LoadLibraryA("dm_plain.dll");
    ev_in = CreateEventA(NULL, TRUE, FALSE, "dm_in");
    ev_rel = CreateEventA(NULL, TRUE, FALSE, "dm_release");
    ev_go = CreateEventA(NULL, TRUE, FALSE, NULL);
    /* B exists before A enters DllMain: a new thread can't start while A holds the loader lock */
    ev_bup = CreateEventA(NULL, TRUE, FALSE, NULL);
    ev_done = CreateEventA(NULL, TRUE, FALSE, NULL);
    b = CreateThread(NULL, 0, thread_b, NULL, 0, NULL);
    if (WaitForSingleObject(ev_bup, 5000)) { printf("B not started\n"); return 1; }
    a = CreateThread(NULL, 0, thread_a, NULL, 0, NULL);
    if (WaitForSingleObject(ev_in, 5000)) { printf("DllMain not entered\n"); return 1; }
    t0 = GetTickCount();
    SetEvent(ev_go);
    if (!WaitForSingleObject(ev_done, 3000))
        printf("%s: returned in %lu ms\n", mode, GetTickCount() - t0);
    else
    {
        SetEvent(ev_rel);
        t1 = GetTickCount();
        WaitForSingleObject(ev_done, INFINITE);
        printf("%s: blocked (>3000 ms), returned %lu ms after release\n", mode, GetTickCount() - t1);
    }
    SetEvent(ev_rel);
    WaitForSingleObject(a, INFINITE);
    return 0;
}
