/* DLLs for loader_dllmain.c. -DBLOCK: DllMain(PROCESS_ATTACH) signals "dm_in" and
 * waits for "dm_release" (15 s max). Otherwise a plain DLL.
 * x86_64-w64-mingw32-gcc -O2 -shared -DBLOCK -o dm_block.dll dm.c
 * x86_64-w64-mingw32-gcc -O2 -shared -o dm_plain.dll dm.c */
#include <windows.h>

__declspec(dllexport) int dm_func(void) { return 42; }

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, void *reserved)
{
#ifdef BLOCK
    if (reason == DLL_PROCESS_ATTACH)
    {
        HANDLE in = OpenEventA(EVENT_ALL_ACCESS, FALSE, "dm_in");
        HANDLE rel = OpenEventA(EVENT_ALL_ACCESS, FALSE, "dm_release");
        SetEvent(in);
        WaitForSingleObject(rel, 15000);
    }
#endif
    return TRUE;
}
