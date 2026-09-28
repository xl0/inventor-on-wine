/* Session ids as seen by a service vs an interactive process (issue 014).
 * svc_session.exe          install as own-process service, start it, print its
 *                          report, then report for this (user) process
 * svc_session.exe service  service entry (writes svc_session.log next to exe)
 * svc_session.exe report   print the report for this process
 * Exit code of the driver: 0 if the service ran and its IsWindowsService check
 * (Go x/sys/windows/svc algorithm) was true.
 * Build: x86_64-w64-mingw32-gcc -O2 -o svc_session.exe svc_session.c -lntdll -lwtsapi32
 */
#include <windows.h>
#include <winternl.h>
#include <wtsapi32.h>
#include <stdio.h>

#define NAME L"t014svc"
static SERVICE_STATUS_HANDLE status_handle;
static SERVICE_STATUS status;
static FILE *out;

/* golang.org/x/sys/windows/svc.IsWindowsService: parent (by
 * InheritedFromUniqueProcessId) must be services.exe with SessionId 0 */
static void parent_info(void)
{
    PROCESS_BASIC_INFORMATION pbi;
    SYSTEM_PROCESS_INFORMATION *spi;
    ULONG size = 1 << 20;
    char *buf = malloc(size);
    NTSTATUS st;

    NtQueryInformationProcess(GetCurrentProcess(), ProcessBasicInformation, &pbi, sizeof(pbi), NULL);
    st = NtQuerySystemInformation(SystemProcessInformation, buf, size, &size);
    if (st) { fprintf(out, "NtQuerySystemInformation %#lx\n", st); return; }
    for (spi = (void *)buf;; spi = (void *)((char *)spi + spi->NextEntryOffset))
    {
        if ((ULONG_PTR)spi->UniqueProcessId == (ULONG_PTR)pbi.InheritedFromUniqueProcessId)
        {
            BOOL is_svc = spi->SessionId == 0 && !_wcsnicmp(spi->ImageName.Buffer, L"services.exe",
                                                            spi->ImageName.Length / 2);
            fprintf(out, "parent %lu %.*ls session %lu IsWindowsService %d\n", (ULONG)(ULONG_PTR)spi->UniqueProcessId,
                    spi->ImageName.Length / 2, spi->ImageName.Buffer, spi->SessionId, is_svc);
            return;
        }
        if (!spi->NextEntryOffset) break;
    }
    fprintf(out, "parent %lu not found\n", (ULONG)(ULONG_PTR)pbi.InheritedFromUniqueProcessId);
}

static void report(void)
{
    DWORD id = ~0u, len, count = 0, i;
    ULONG nt = ~0u;
    NTSTATUS st;
    HANDLE token, h;
    WCHAR name[256];
    char info[1024];
    OBJECT_NAME_INFORMATION *oni = (void *)info;
    WTS_SESSION_INFOW *sessions;

    fprintf(out, "PEB SessionId %lu\n", NtCurrentTeb()->ProcessEnvironmentBlock->SessionId);
    if (!ProcessIdToSessionId(GetCurrentProcessId(), &id)) id = 1000000 + GetLastError();
    fprintf(out, "ProcessIdToSessionId %lu\n", id);
    st = NtQueryInformationProcess(GetCurrentProcess(), ProcessSessionInformation, &nt, sizeof(nt), NULL);
    fprintf(out, "ProcessSessionInformation %#lx %lu\n", st, nt);
    OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token);
    id = ~0u;
    GetTokenInformation(token, TokenSessionId, &id, sizeof(id), &len);
    fprintf(out, "TokenSessionId %lu\n", id);
    CloseHandle(token);
    fprintf(out, "WTSGetActiveConsoleSessionId %lu\n", WTSGetActiveConsoleSessionId());
    fprintf(out, "KUSER_SHARED_DATA ActiveConsoleId %lu\n", *(ULONG *)(0x7ffe0000 + 0x2d8));
    if (WTSQueryUserToken(WTSGetActiveConsoleSessionId(), &token))
    {
        id = ~0u;
        GetTokenInformation(token, TokenSessionId, &id, sizeof(id), &len);
        fprintf(out, "WTSQueryUserToken ok, session %lu\n", id);
        CloseHandle(token);
    }
    else fprintf(out, "WTSQueryUserToken failed %lu\n", GetLastError());
    if (WTSEnumerateSessionsW(WTS_CURRENT_SERVER_HANDLE, 0, 1, &sessions, &count))
    {
        for (i = 0; i < count; i++)
            fprintf(out, "WTS session %lu %ls state %u\n", sessions[i].SessionId, sessions[i].pWinStationName,
                    sessions[i].State);
        WTSFreeMemory(sessions);
    }
    else fprintf(out, "WTSEnumerateSessions failed %lu\n", GetLastError());
    name[0] = 0;
    GetUserObjectInformationW(GetProcessWindowStation(), UOI_NAME, name, sizeof(name), &len);
    fprintf(out, "winstation %ls\n", name);
    name[0] = 0;
    GetUserObjectInformationW(GetThreadDesktop(GetCurrentThreadId()), UOI_NAME, name, sizeof(name), &len);
    fprintf(out, "desktop %ls\n", name);
    h = CreateEventW(NULL, TRUE, FALSE, L"t014_local_event");
    if (!NtQueryObject(h, ObjectNameInformation, oni, sizeof(info), NULL))
        fprintf(out, "unqualified event -> %.*ls\n", oni->Name.Length / 2, oni->Name.Buffer);
    CloseHandle(h);
    parent_info();
    fflush(out);
}

static DWORD WINAPI handler(DWORD ctrl, DWORD type, void *data, void *ctx)
{
    return NO_ERROR;
}

static void WINAPI service_main(DWORD argc, WCHAR **argv)
{
    WCHAR path[MAX_PATH];

    status_handle = RegisterServiceCtrlHandlerExW(NAME, handler, NULL);
    status.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    status.dwCurrentState = SERVICE_RUNNING;
    SetServiceStatus(status_handle, &status);
    GetModuleFileNameW(NULL, path, MAX_PATH);
    wcscpy(wcsrchr(path, '\\') + 1, L"svc_session.log");
    out = _wfopen(path, L"w");
    report();
    fclose(out);
    status.dwCurrentState = SERVICE_STOPPED;
    SetServiceStatus(status_handle, &status);
}

int wmain(int argc, WCHAR **argv)
{
    SERVICE_TABLE_ENTRYW table[] = {{(WCHAR *)NAME, service_main}, {0}};
    WCHAR path[MAX_PATH + 16], log[MAX_PATH];
    SC_HANDLE scm, svc;
    SERVICE_STATUS st;
    char line[512];
    FILE *f;
    int i, ret = 1;

    out = stdout;
    if (argc > 1 && !wcscmp(argv[1], L"service")) return !StartServiceCtrlDispatcherW(table);
    if (argc > 1 && !wcscmp(argv[1], L"report")) { report(); return 0; }

    GetModuleFileNameW(NULL, log, MAX_PATH);
    swprintf(path, ARRAYSIZE(path), L"\"%ls\" service", log);
    wcscpy(wcsrchr(log, '\\') + 1, L"svc_session.log");
    DeleteFileW(log);
    if (!(scm = OpenSCManagerW(NULL, NULL, SC_MANAGER_ALL_ACCESS))) { printf("OpenSCManager %lu\n", GetLastError()); return 1; }
    if ((svc = OpenServiceW(scm, NAME, SERVICE_ALL_ACCESS))) { DeleteService(svc); CloseServiceHandle(svc); }
    svc = CreateServiceW(scm, NAME, NAME, SERVICE_ALL_ACCESS, SERVICE_WIN32_OWN_PROCESS, SERVICE_DEMAND_START,
                         SERVICE_ERROR_IGNORE, path, NULL, NULL, NULL, NULL, NULL);
    if (!svc) { printf("CreateService %lu\n", GetLastError()); return 1; }
    if (!StartServiceW(svc, 0, NULL)) printf("StartService %lu\n", GetLastError());
    for (i = 0; i < 100 && QueryServiceStatus(svc, &st) && st.dwCurrentState != SERVICE_STOPPED; i++) Sleep(100);
    DeleteService(svc);
    CloseServiceHandle(svc);
    CloseServiceHandle(scm);
    printf("--- service\n");
    if ((f = _wfopen(log, L"r")))
    {
        while (fgets(line, sizeof(line), f))
        {
            fputs(line, stdout);
            if (strstr(line, "IsWindowsService 1")) ret = 0;
        }
        fclose(f);
    }
    else printf("no service log\n");
    printf("--- user process\n");
    report();
    return ret;
}
