/* WaitForInputIdle on a GUI child that is busy for 1.5 s before pumping messages.
 * Windows: waits ~1500 ms. Result is written to C:\wl_idle.txt (GUI subsystem, no console).
 * "wl_idle.exe thread": the child additionally starts a helper thread that runs a message loop at once;
 * Windows 11 then returns at once (~10 ms): a message wait of any thread counts (issue 133, tests/r133/idle.c). */
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
static DWORD WINAPI helper(void *p)
{
    MSG msg;
    CreateWindowA("static", "helper", WS_POPUP, 0, 0, 1, 1, HWND_MESSAGE, NULL, NULL, NULL);
    while (GetMessageA(&msg, NULL, 0, 0)) DispatchMessageA(&msg);
    return 0;
}
int main(int argc, char **argv)
{
    if (argc > 1 && !strcmp(argv[1], "child"))
    {
        MSG msg; DWORD end;
        CreateWindowA("static", "idle child", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 0, 0, 200, 100, NULL, NULL, NULL, NULL);
        if (argc > 2) CloseHandle(CreateThread(NULL, 0, helper, NULL, 0, NULL));
        Sleep(1500); /* busy initialisation, no message pumping */
        /* GetMessage: Wine never treats a PeekMessage polling loop as idle (issue 143) */
        end = SetTimer(NULL, 0, 3000, NULL);
        while (GetMessageA(&msg, NULL, 0, 0) && !(msg.message == WM_TIMER && msg.wParam == end)) DispatchMessageA(&msg);
        return 0;
    }
    else
    {
        char cmd[MAX_PATH + 16]; STARTUPINFOA si = {sizeof(si)}; PROCESS_INFORMATION pi; DWORD t0, r;
        GetModuleFileNameA(NULL, cmd, MAX_PATH); strcat(cmd, argc > 1 ? " child thread" : " child");
        CreateProcessA(NULL, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi);
        t0 = GetTickCount();
        r = WaitForInputIdle(pi.hProcess, 10000);
        { char b[100]; sprintf(b, "WaitForInputIdle = %lu after %lu ms (expect %s)\n", r, GetTickCount() - t0, argc > 1 ? "< 200" : "~1500"); HANDLE f = CreateFileA("C:\\wl_idle.txt", GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL); DWORD n; WriteFile(f, b, strlen(b), &n, NULL); CloseHandle(f); }
        WaitForSingleObject(pi.hProcess, 6000);
    }
    return 0;
}
