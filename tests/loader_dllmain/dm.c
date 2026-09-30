/* DLLs for loader_dllmain.c.
 * -DBLOCK: DllMain(PROCESS_ATTACH) stores its base in env DM_BLOCK_BASE, signals "dm_in" and waits
 *   for "dm_release" (15 s max). -DTOP: imports dm_block.dll (its DllMain runs after dm_block's).
 *   Otherwise a plain DLL. dm_fwd.dll forwards fwd_loaded -> dm_plain.dm_func, fwd_new -> dm_new.dm_func.
 * B=x86_64-w64-mingw32-gcc (or "i686-w64-mingw32-gcc -static-libgcc")
 * $B -O2 -shared -DBLOCK -o dm_block.dll dm.c && $B -O2 -shared -DTOP -o dm_top.dll dm.c dm_block.dll
 * $B -O2 -shared -o dm_plain.dll dm.c && cp dm_plain.dll dm_new.dll && cp dm_plain.dll dm_plain2.dll
 * $B -O2 -shared -o dm_fwd.dll dm.c dm_fwd.def */
#include <windows.h>
#include <stdio.h>

#ifdef TOP
__declspec(dllimport) int dm_func(void);
__declspec(dllexport) int dm_top_func(void) { return dm_func() + 1; }
#else
__declspec(dllexport) int dm_func(void) { return 42; }
#endif

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, void *reserved)
{
#ifdef NESTED  /* stress.c: loader calls from DllMain (the loader lock owner) */
    static HMODULE dep;
    if (reason == DLL_PROCESS_ATTACH)
    {
        HMODULE fwd = LoadLibraryA("sfwd.dll");
        if (!(dep = LoadLibraryA("s2.dll")) || !GetProcAddress(dep, "dm_func") || !GetModuleHandleA("s2.dll")
            || !fwd || !GetProcAddress(fwd, "fwd")) return FALSE;
        FreeLibrary(fwd);
    }
    if (reason == DLL_PROCESS_DETACH && !reserved) FreeLibrary(dep);
#endif
#ifdef BLOCK
    if (reason == DLL_PROCESS_ATTACH)
    {
        HANDLE in = OpenEventA(EVENT_ALL_ACCESS, FALSE, "dm_in");
        HANDLE rel = OpenEventA(EVENT_ALL_ACCESS, FALSE, "dm_release");
        char buf[32];
        sprintf(buf, "%p", inst);
        SetEnvironmentVariableA("DM_BLOCK_BASE", buf);
        SetEvent(in);
        WaitForSingleObject(rel, 15000);
    }
#endif
    return TRUE;
}
