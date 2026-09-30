/* Does a loader operation in thread B block while thread A is inside a DllMain
 * (PROCESS_ATTACH of dm_block.dll)? Issues 048, 092. DLLs: see dm.c.
 * Usage: loader_dllmain.exe MODE [IL_DLL_PATH]  (all|all_quick runs every mode in a child process)
 *   none (baseline), new (LoadLibrary of an unloaded DLL), loaded / loaded_path (LoadLibrary of a
 *   loaded one by name / full path), getmod / getmod_path, getmodex_pin / getmodex_ref / getmodex_addr
 *   (GetModuleHandleEx PIN / 0 / FROM_ADDRESS|UNCHANGED), getproc / getproc_ord, fwd_sys (kernel32
 *   HeapAlloc forwarder), fwd_loaded / fwd_new (dm_fwd.dll forwarders to a loaded / unloaded DLL),
 *   addref (LdrAddRefDll), freelib (FreeLibrary, count 2 -> 1), filename (GetModuleFileNameW),
 *   datafile / datafile_loaded (LoadLibraryEx AS_DATAFILE of an unloaded / loaded DLL),
 *   pcfile (RtlPcToFileHeader), thread (CreateThread, thread runs),
 *   il / ildata (LoadLibraryEx(IL_DLL_PATH) / AS_DATAFILE),
 *   *_running: the same on dm_block.dll, whose DllMain is running (getmod, getproc, pcfile, loaded),
 *   *_pending: the same on dm_top.dll, which imports dm_block.dll (mapped, DllMain not called yet).
 * Prints "MODE: returned in N ms -> result" or "MODE: blocked (>3000 ms), ...".
 * x86_64-w64-mingw32-gcc -O2 -o loader_dllmain.exe loader_dllmain.c */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *mode;
static const char *il_path;
static HANDLE ev_in, ev_rel, ev_go, ev_bup, ev_done;
static PVOID (WINAPI *pRtlPcToFileHeader)(PVOID, PVOID *);
static LONG (WINAPI *pLdrAddRefDll)(ULONG, HMODULE);
static void *result;
static DWORD result_err;
static BOOL pending;
static DWORD (WINAPI *pGetMappedFileNameA)(HANDLE, void *, char *, DWORD);

static DWORD WINAPI thread_a(void *arg)
{
    HMODULE h = LoadLibraryA(pending ? "dm_top.dll" : "dm_block.dll");
    if (!h) printf("A: load failed %lu\n", GetLastError());
    return 0;
}

static DWORD WINAPI trivial(void *arg) { return 7; }

static HMODULE env_base(const char *name)
{
    char buf[32];
    if (!GetEnvironmentVariableA(name, buf, sizeof(buf))) return NULL;
    return (HMODULE)(ULONG_PTR)_strtoui64(buf, NULL, 16);
}

static BOOL is(const char *m) { return !strcmp(mode, m); }

static DWORD WINAPI thread_b(void *arg)
{
    void *r = NULL;
    HMODULE h, fwd = GetModuleHandleA("dm_fwd.dll"), k32 = GetModuleHandleA("kernel32.dll");
    char path[MAX_PATH];
    WCHAR pathW[MAX_PATH];

    GetFullPathNameA("dm_plain.dll", MAX_PATH, path, NULL);
    h = GetModuleHandleA("dm_plain.dll");  /* loaded before A started, if the mode needs it */
    SetEvent(ev_bup);
    WaitForSingleObject(ev_go, INFINITE);
    SetLastError(0xdeadbeef);
    if (is("none")) r = (void *)1;
    else if (is("new")) r = LoadLibraryA("dm_plain2.dll");
    else if (is("loaded")) r = LoadLibraryA("dm_plain.dll");
    else if (is("loaded_path")) r = LoadLibraryA(path);
    else if (is("getmod")) r = GetModuleHandleA("dm_plain.dll");
    else if (is("getmod_path")) r = GetModuleHandleA(path);
    else if (is("getmodex_pin")) GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_PIN, "dm_plain.dll", (HMODULE *)&r);
    else if (is("getmodex_ref")) GetModuleHandleExA(0, "dm_plain.dll", (HMODULE *)&r);
    else if (is("getmodex_addr"))
        GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (const char *)h + 0x1000, (HMODULE *)&r);
    else if (is("getproc")) r = GetProcAddress(h, "dm_func");
    else if (is("getproc_ord")) r = GetProcAddress(h, (const char *)1);
    else if (is("fwd_sys")) r = GetProcAddress(k32, "HeapAlloc");
    else if (is("fwd_loaded")) r = GetProcAddress(fwd, "fwd_loaded");
    else if (is("fwd_new")) r = GetProcAddress(fwd, "fwd_new");
    else if (is("addref")) r = (void *)(ULONG_PTR)(pLdrAddRefDll(0, h) == 0);
    else if (is("freelib")) r = (void *)(ULONG_PTR)FreeLibrary(h);
    else if (is("filename")) r = (void *)(ULONG_PTR)GetModuleFileNameW(h, pathW, MAX_PATH);
    else if (is("datafile")) r = LoadLibraryExA("dm_plain2.dll", NULL, LOAD_LIBRARY_AS_DATAFILE);
    else if (is("datafile_loaded")) r = LoadLibraryExA("dm_plain.dll", NULL, LOAD_LIBRARY_AS_DATAFILE);
    else if (is("pcfile")) pRtlPcToFileHeader((void *)thread_b, &r);  /* as C++ throw does */
    else if (is("il")) r = LoadLibraryExA(il_path, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
    else if (is("ildata")) r = LoadLibraryExA(il_path, NULL, LOAD_LIBRARY_AS_DATAFILE);
    else if (is("thread"))
    {
        HANDLE t = CreateThread(NULL, 0, trivial, NULL, 0, NULL);
        WaitForSingleObject(t, INFINITE);
        r = t;
    }
    else if (is("getmod_running")) r = GetModuleHandleA("dm_block.dll");
    else if (is("getproc_running")) r = GetProcAddress(env_base("DM_BLOCK_BASE"), "dm_func");
    else if (is("pcfile_running")) pRtlPcToFileHeader((char *)env_base("DM_BLOCK_BASE") + 0x1000, &r);
    else if (is("loaded_running")) r = LoadLibraryA("dm_block.dll");
    else if (is("getmod_pending")) r = GetModuleHandleA("dm_top.dll");
    else if (is("getproc_pending")) r = GetProcAddress(env_base("DM_TOP_BASE"), "dm_top_func");
    else if (is("pcfile_pending")) pRtlPcToFileHeader((char *)env_base("DM_TOP_BASE") + 0x1000, &r);
    else if (is("loaded_pending")) r = LoadLibraryA("dm_top.dll");
    else printf("unknown mode %s\n", mode);
    result = r;
    result_err = GetLastError();
    SetEvent(ev_done);  /* thread exit itself waits for the loader lock (THREAD_DETACH) */
    return 0;
}

static const char *modes[] =
{
    "none", "new", "loaded", "loaded_path", "getmod", "getmod_path", "getmodex_pin", "getmodex_ref",
    "getmodex_addr", "getproc", "getproc_ord", "fwd_sys", "fwd_loaded", "fwd_new", "addref", "freelib",
    "filename", "datafile", "datafile_loaded", "pcfile", "thread", "getmod_running", "getproc_running",
    "pcfile_running", "loaded_running", "getmod_pending", "getproc_pending", "pcfile_pending", "loaded_pending",
};

int main(int argc, char **argv)
{
    HANDLE a, b;
    DWORD t0, t1;
    unsigned int i;

    if (argc < 2) return 2;
    mode = argv[1];
    il_path = argc > 2 ? argv[2] : "";
    setvbuf(stdout, NULL, _IONBF, 0);
    if (is("all"))
    {
        for (i = 0; i < ARRAYSIZE(modes); i++)
        {
            char cmd[MAX_PATH + 64];
            STARTUPINFOA si = { sizeof(si) };
            PROCESS_INFORMATION pi;
            GetModuleFileNameA(NULL, cmd, MAX_PATH);
            strcat(cmd, " ");
            strcat(cmd, modes[i]);
            if (!CreateProcessA(NULL, cmd, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi)) return 1;
            WaitForSingleObject(pi.hProcess, INFINITE);
        }
        return 0;
    }
    pending = strstr(mode, "_pending") != NULL;
    pRtlPcToFileHeader = (void *)GetProcAddress(GetModuleHandleA("ntdll.dll"), "RtlPcToFileHeader");
    pLdrAddRefDll = (void *)GetProcAddress(GetModuleHandleA("ntdll.dll"), "LdrAddRefDll");
    pGetMappedFileNameA = (void *)GetProcAddress(GetModuleHandleA("kernel32.dll"), "K32GetMappedFileNameA");
    if (!strstr(mode, "new") && strcmp(mode, "datafile")) LoadLibraryA("dm_plain.dll");
    if (is("freelib")) LoadLibraryA("dm_plain.dll");
    if (!strncmp(mode, "fwd_", 4)) LoadLibraryA("dm_fwd.dll");
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
    if (pending)
    {
        /* dm_top.dll is mapped but not initialized; find its base without the loader */
        MEMORY_BASIC_INFORMATION mbi;
        char *p = NULL, buf[32], name[MAX_PATH];
        while (VirtualQuery(p, &mbi, sizeof(mbi)))
        {
            if (mbi.Type == MEM_IMAGE && mbi.AllocationBase == mbi.BaseAddress &&
                pGetMappedFileNameA(GetCurrentProcess(), p, name, MAX_PATH) && strstr(name, "dm_top.dll")) break;
            p = (char *)mbi.BaseAddress + mbi.RegionSize;
        }
        sprintf(buf, "%p", p);
        SetEnvironmentVariableA("DM_TOP_BASE", buf);
    }
    t0 = GetTickCount();
    SetEvent(ev_go);
    if (!WaitForSingleObject(ev_done, 3000))
        printf("%s: returned in %lu ms -> %p (error %lu)\n", mode, GetTickCount() - t0, result, result_err);
    else
    {
        SetEvent(ev_rel);
        t1 = GetTickCount();
        WaitForSingleObject(ev_done, INFINITE);
        printf("%s: blocked (>3000 ms), returned %lu ms after release -> %p (error %lu)\n",
               mode, GetTickCount() - t1, result, result_err);
    }
    SetEvent(ev_rel);
    WaitForSingleObject(a, INFINITE);
    return 0;
}
