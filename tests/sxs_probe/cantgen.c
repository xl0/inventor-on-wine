/* Where does CreateProcess fail for an exe whose manifest can't generate an
 * activation context? Copies itself to <case>\child.exe with an external
 * child.exe.manifest, runs it inside a job with a completion port and prints
 * the CreateProcess result plus the job messages (process creation/exit). */
#include <windows.h>
#include <stdio.h>

static const char dep_fmt[] =
    "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
    "<assembly xmlns=\"urn:schemas-microsoft-com:asm.v1\" manifestVersion=\"1.0\">\n"
    "<dependency><dependentAssembly><assemblyIdentity type=\"win32\" %s/></dependentAssembly></dependency>\n"
    "</assembly>\n";

static void run(HANDLE port, const char *name, const char *manifest, DWORD flags)
{
    char dir[MAX_PATH], exe[MAX_PATH], path[MAX_PATH], self[MAX_PATH];
    STARTUPINFOA si = {sizeof(si)}; PROCESS_INFORMATION pi; DWORD code, n, err = 0; ULONG_PTR key; OVERLAPPED *ov;
    FILE *f;

    GetModuleFileNameA(NULL, self, MAX_PATH);
    snprintf(dir, sizeof(dir), "C:\\t\\004\\%s", name);
    CreateDirectoryA(dir, NULL);
    snprintf(exe, sizeof(exe), "%s\\child.exe", dir);
    CopyFileA(self, exe, FALSE);
    snprintf(path, sizeof(path), "%s.manifest", exe);
    f = fopen(path, "wb"); fputs(manifest, f); fclose(f);

    if (!CreateProcessA(exe, (char *)"child.exe child", NULL, NULL, FALSE, flags, NULL, dir, &si, &pi))
        err = GetLastError();
    printf("%-10s CreateProcess %s", name, err ? "failed" : "ok");
    if (err) printf(" err=%lu", err);
    else
    {
        if (flags & CREATE_SUSPENDED) ResumeThread(pi.hThread);
        WaitForSingleObject(pi.hProcess, 5000); GetExitCodeProcess(pi.hProcess, &code);
        printf(" pid=%lu exit=%lu", pi.dwProcessId, code);
        CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
    }
    printf("\n  job msgs:");
    while (GetQueuedCompletionStatus(port, &n, &key, &ov, 500))
        printf(" %lu(pid %lu)", n, (DWORD)(ULONG_PTR)ov);
    printf("\n");
    fflush(stdout);
}

int main(int argc, char **argv)
{
    char m[1024];
    HANDLE job, port;
    JOBOBJECT_ASSOCIATE_COMPLETION_PORT jacp;

    if (argc > 1 && !strcmp(argv[1], "child")) return 42;

    CreateDirectoryA("C:\\t\\004", NULL);
    job = CreateJobObjectA(NULL, NULL);
    port = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 1);
    jacp.CompletionKey = job; jacp.CompletionPort = port;
    SetInformationJobObject(job, JobObjectAssociateCompletionPortInformation, &jacp, sizeof(jacp));
    if (!AssignProcessToJobObject(job, GetCurrentProcess())) printf("assign err=%lu\n", GetLastError());
    printf("self pid=%lu (msg 6=NEW_PROCESS 7=EXIT_PROCESS 8=ABNORMAL_EXIT)\n", GetCurrentProcessId());

    snprintf(m, sizeof(m), dep_fmt, "name=\"nosuch\" version=\"1.0.0.0\" processorArchitecture=\"*\"");
    run(port, "missing", m, 0);
    run(port, "missing_s", m, CREATE_SUSPENDED);
    run(port, "malformed", "<?xml version=\"1.0\"?><assembly xmlns=\"urn:schemas-microsoft-com:asm.v1\" manifestVersion=\"1.0\"><dependency>", 0);
    snprintf(m, sizeof(m), dep_fmt, "name=\"Microsoft.Windows.Common-Controls\" version=\"6.0.0.0\" "
             "processorArchitecture=\"*\" publicKeyToken=\"6595b64144ccf1df\" language=\"*\"");
    run(port, "comctl6", m, 0);
    snprintf(m, sizeof(m), dep_fmt, "name=\"Microsoft.Windows.Common-Controls\" version=\"6.0.0.0\" "
             "processorArchitecture=\"x86\" publicKeyToken=\"6595b64144ccf1df\" language=\"*\"");
    run(port, "comctl6x86", m, 0);
    snprintf(m, sizeof(m), dep_fmt, "name=\"Microsoft.Windows.Common-Controls\" version=\"5.82.0.0\" "
             "processorArchitecture=\"*\" publicKeyToken=\"6595b64144ccf1df\" language=\"*\"");
    run(port, "comctl5", m, 0);
    snprintf(m, sizeof(m), dep_fmt, "name=\"Microsoft.VC90.OpenMP\" version=\"9.0.21022.8\" "
             "processorArchitecture=\"*\" publicKeyToken=\"1fc8b3b9a1e18e3b\"");
    run(port, "vc90omp", m, 0);
    return 0;
}
