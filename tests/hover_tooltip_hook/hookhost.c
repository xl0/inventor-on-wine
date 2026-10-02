/* hookhost.exe SECS: installs hook.dll's global WH_GETMESSAGE hook for SECS seconds (120).
   x86_64-w64-mingw32-gcc -O2 -o hookhost.exe hookhost.c */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
int main(int argc, char **argv)
{
    HMODULE dll = LoadLibraryA("hook.dll");
    HOOKPROC (*get_hook)(void) = dll ? (void *)GetProcAddress(dll, "get_hook") : NULL;
    DWORD end = GetTickCount() + (argc > 1 ? atoi(argv[1]) : 60) * 1000;
    MSG msg;
    if (!get_hook || !SetWindowsHookExA(WH_GETMESSAGE, get_hook(), dll, 0)) return 1;
    printf("hook installed at tick %lu\n", GetTickCount());
    while (GetTickCount() < end)
    {
        while (PeekMessageA(&msg, 0, 0, 0, PM_REMOVE)) DispatchMessageA(&msg);
        Sleep(50);
    }
    return 0;
}
