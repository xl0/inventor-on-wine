/* CreateProcess(DEBUG_PROCESS) of an exe whose external manifest has a missing
 * dependency: print the result and the debug events that still arrive
 * (tells the exit code Windows uses and whether the child ran any code). */
#include <windows.h>
#include <stdio.h>
int main(int argc, char **argv)
{
    char self[MAX_PATH]; STARTUPINFOA si = {sizeof(si)}; PROCESS_INFORMATION pi; DEBUG_EVENT ev; FILE *f;
    if (argc > 1) return 42;
    GetModuleFileNameA(NULL, self, MAX_PATH);
    CreateDirectoryA("C:\\t\\004", NULL); CreateDirectoryA("C:\\t\\004\\dbg", NULL);
    CopyFileA(self, "C:\\t\\004\\dbg\\child.exe", FALSE);
    f = fopen("C:\\t\\004\\dbg\\child.exe.manifest", "wb");
    fputs("<?xml version=\"1.0\"?><assembly xmlns=\"urn:schemas-microsoft-com:asm.v1\" manifestVersion=\"1.0\">"
          "<dependency><dependentAssembly><assemblyIdentity type=\"win32\" name=\"nosuch\" version=\"1.0.0.0\" processorArchitecture=\"*\"/>"
          "</dependentAssembly></dependency></assembly>", f);
    fclose(f);
    if (CreateProcessA("C:\\t\\004\\dbg\\child.exe", (char *)"child.exe x", NULL, NULL, FALSE, DEBUG_PROCESS, NULL, NULL, &si, &pi))
        printf("CreateProcess ok\n");
    else printf("CreateProcess err=%lu\n", GetLastError());
    while (WaitForDebugEvent(&ev, 2000))
    {
        printf("event %lu pid %lu", ev.dwDebugEventCode, ev.dwProcessId);
        if (ev.dwDebugEventCode == EXIT_PROCESS_DEBUG_EVENT) printf(" exit=%#lx", ev.u.ExitProcess.dwExitCode);
        if (ev.dwDebugEventCode == EXCEPTION_DEBUG_EVENT) printf(" code=%#lx", ev.u.Exception.ExceptionRecord.ExceptionCode);
        printf("\n");
        ContinueDebugEvent(ev.dwProcessId, ev.dwThreadId, DBG_CONTINUE);
        if (ev.dwDebugEventCode == EXIT_PROCESS_DEBUG_EVENT) break;
    }
    printf("done (last wait err=%lu)\n", GetLastError());
    return 0;
}
