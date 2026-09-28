/* Launch <dir>\child.exe for each dir argument, print whether process creation
 * succeeded (child returns 42) or the error code. */
#include <windows.h>
#include <stdio.h>
int main(int argc, char **argv)
{
    for (int i = 1; i < argc; i++)
    {
        char path[MAX_PATH]; STARTUPINFOA si = {sizeof(si)}; PROCESS_INFORMATION pi; DWORD code;
        snprintf(path, sizeof(path), "%s\\child.exe", argv[i]);
        if (!CreateProcessA(path, NULL, NULL, NULL, FALSE, 0, NULL, argv[i], &si, &pi))
        { printf("-\n  ^ %-14s CreateProcess err=%lu\n", argv[i], GetLastError()); fflush(stdout); continue; }
        WaitForSingleObject(pi.hProcess, 5000); GetExitCodeProcess(pi.hProcess, &code);
        printf("  ^ %-14s exit=%lu\n", argv[i], code); fflush(stdout);
        CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
    }
    return 0;
}
