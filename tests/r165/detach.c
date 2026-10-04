/* detach.dll for deadwin.exe: releases a COM object at process detach, on whichever thread exits
 * the process (as an application DLL's static destructors do).
 * Build: x86_64-w64-mingw32-gcc -O2 -shared -o detach.dll detach.c */
#define COBJMACROS
#include <windows.h>
#include <unknwn.h>
#include <stdio.h>

static IUnknown *object;
static DWORD main_tid;

__declspec(dllexport) void release_at_detach(IUnknown *unk)
{
    object = unk;
    main_tid = GetCurrentThreadId();
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, void *reserved)
{
    if (reason == DLL_PROCESS_DETACH && reserved && object)
    {
        if (GetCurrentThreadId() != main_tid)
        {
            fprintf(stderr, "process detach on thread %04lx, not the main thread %04lx\n", GetCurrentThreadId(), main_tid);
            fflush(stderr);
        }
        IUnknown_Release(object);
    }
    return TRUE;
}
