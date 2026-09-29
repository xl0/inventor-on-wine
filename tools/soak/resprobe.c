/* resprobe.exe PID|EXENAME [interval_s]: every interval print a line of resource counts
 * of process PID (unix time first): kernel handles (total + top types), GDI objects (from the
 * process' GdiSharedHandleTable), windows, commit/working set.
 * resprobe.exe PID|EXENAME dump: one line per kernel handle: value, type index, type name, object name.
 * resprobe.exe PID|EXENAME threads: per module, the number of threads whose start address is in it.
 * resprobe.exe PID|EXENAME mods: base, size, name of each module. */
#include <windows.h>
#include <winternl.h>
#include <psapi.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>

typedef struct { PVOID Object; ULONG_PTR UniqueProcessId; ULONG_PTR HandleValue; ULONG GrantedAccess;
    USHORT CreatorBackTraceIndex; USHORT ObjectTypeIndex; ULONG HandleAttributes; ULONG Reserved; } HEX;
typedef struct { ULONG_PTR NumberOfHandles; ULONG_PTR Reserved; HEX Handles[1]; } HIEX;
typedef struct { UINT64 Object; ULONG Owner; USHORT Unique; UCHAR Type; UCHAR Flags; UINT64 UserPointer; } GENT;

static DWORD target; static int nwin;
static BOOL CALLBACK cnt(HWND h, LPARAM l)
{
    DWORD pid; GetWindowThreadProcessId(h, &pid);
    if (pid == target) nwin++;
    if (l) EnumChildWindows(h, cnt, 0);
    return TRUE;
}

int main(int argc, char **argv)
{
    int i;
    DWORD pid = atoi(argv[1]);
    if (!pid)
    {
        PROCESSENTRY32 pe = {sizeof(pe)}; HANDLE sn = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        for (BOOL ok = Process32First(sn, &pe); ok; ok = Process32Next(sn, &pe))
            if (!_stricmp(pe.szExeFile, argv[1])) pid = pe.th32ProcessID;
        printf("pid %lu\n", pid);
    } int iv = argc > 2 ? atoi(argv[2]) : 5;
    HANDLE p = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    PROCESS_BASIC_INFORMATION pbi; ULONG64 tbl = 0; SIZE_T rd;
    ULONG sz = 1 << 24; HIEX *hi = malloc(sz); GENT *g = malloc(0x10000 * sizeof(GENT));
    target = pid;
    if (!p) { printf("OpenProcess %lu\n", GetLastError()); return 1; }
    if (argc > 2 && !strcmp(argv[2], "dump"))
    {
        HANDLE self = GetCurrentProcess(), src = OpenProcess(PROCESS_DUP_HANDLE, FALSE, pid), dup;
        static char nb[4096]; UNICODE_STRING *us = (UNICODE_STRING *)nb;
        if (NtQuerySystemInformation(64, hi, sz, NULL)) return 1;
        for (i = 0; i < hi->NumberOfHandles; i++)
        {
            if (hi->Handles[i].UniqueProcessId != pid) continue;
            printf("%lx %u ", (unsigned long)hi->Handles[i].HandleValue, hi->Handles[i].ObjectTypeIndex);
            if (DuplicateHandle(src, (HANDLE)hi->Handles[i].HandleValue, self, &dup, 0, FALSE, DUPLICATE_SAME_ACCESS))
            {
                /* type name, then object name (not for files: can block on pipes on Windows) */
                if (!NtQueryObject(dup, ObjectTypeInformation, nb, sizeof(nb) - 2, NULL))
                    printf("%.*ls ", us->Length / 2, us->Buffer);
                if ((us->Length != 8 || wcsncmp(us->Buffer, L"File", 4)) &&
                    !NtQueryObject(dup, ObjectNameInformation, nb, sizeof(nb) - 2, NULL) && us->Length)
                    printf("%.*ls", us->Length / 2, us->Buffer);
                CloseHandle(dup);
            }
            printf("\n");
        }
        return 0;
    }
    if (argc > 2 && (!strcmp(argv[2], "threads") || !strcmp(argv[2], "mods")))
    {
        static HMODULE mods[4096]; static int cnt[4096]; static MODULEINFO mi[4096]; DWORD need; int nm, other = 0;
        THREADENTRY32 te = {sizeof(te)}; HANDLE sn = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
        char name[MAX_PATH];
        EnumProcessModulesEx(p, mods, sizeof(mods), &need, LIST_MODULES_ALL);
        nm = need / sizeof(HMODULE);
        for (i = 0; i < nm; i++) GetModuleInformation(p, mods[i], &mi[i], sizeof(mi[i]));
        for (BOOL ok = argv[2][0] == 't' && Thread32First(sn, &te); ok; ok = Thread32Next(sn, &te))
        {
            HANDLE t; ULONG_PTR start = 0;
            if (te.th32OwnerProcessID != pid || !(t = OpenThread(THREAD_QUERY_INFORMATION, FALSE, te.th32ThreadID))) continue;
            NtQueryInformationThread(t, 9 /* ThreadQuerySetWin32StartAddress */, &start, sizeof(start), NULL);
            CloseHandle(t);
            for (i = 0; i < nm; i++)
                if (start >= (ULONG_PTR)mi[i].lpBaseOfDll && start < (ULONG_PTR)mi[i].lpBaseOfDll + mi[i].SizeOfImage) break;
            if (i < nm) cnt[i]++; else other++;
        }
        for (i = 0; i < nm; i++)
            if (!strcmp(argv[2], "mods") && GetModuleBaseNameA(p, mods[i], name, sizeof(name)))
                printf("%p %lx %s\n", mi[i].lpBaseOfDll, mi[i].SizeOfImage, name);
            else if (cnt[i] && GetModuleBaseNameA(p, mods[i], name, sizeof(name))) printf("%5d %s\n", cnt[i], name);
        if (argv[2][0] == 't') printf("%5d (outside modules)\n", other);
        return 0;
    }
    NtQueryInformationProcess(p, ProcessBasicInformation, &pbi, sizeof(pbi), NULL);
    ReadProcessMemory(p, (char *)pbi.PebBaseAddress + 0xf8, &tbl, 8, &rd);
    setvbuf(stdout, NULL, _IONBF, 0);
    for (;;)
    {
        int types[256] = {0}, gt[32] = {0}, n = 0, ng = 0, j, best;
        PROCESS_MEMORY_COUNTERS_EX pm = {sizeof(pm)};
        char buf[512], *s = buf;
        DWORD code;
        if (GetExitCodeProcess(p, &code) && code != STILL_ACTIVE) { printf("exited %lx\n", code); return 0; }
        if (!NtQuerySystemInformation(64, hi, sz, NULL))
            for (i = 0; i < hi->NumberOfHandles; i++)
                if (hi->Handles[i].UniqueProcessId == pid) { n++; types[hi->Handles[i].ObjectTypeIndex & 255]++; }
        if (tbl && ReadProcessMemory(p, (void *)(ULONG_PTR)tbl, g, 0x10000 * sizeof(GENT), &rd))
            for (i = 0; i < 0x10000; i++) if (g[i].Type) { ng++; gt[g[i].Type & 31]++; }
        nwin = 0; EnumWindows(cnt, 1);
        GetProcessMemoryInfo(p, (PROCESS_MEMORY_COUNTERS *)&pm, sizeof(pm));
        s += sprintf(s, "%lld h=%d gdi=%d win=%d priv=%lluM ws=%lluM |", (long long)time(NULL),
                     n, ng, nwin, (unsigned long long)pm.PrivateUsage >> 20, (unsigned long long)pm.WorkingSetSize >> 20);
        for (j = 0; j < 5; j++)
        {
            for (best = 0, i = 1; i < 256; i++) if (types[i] > types[best]) best = i;
            if (!types[best]) break;
            s += sprintf(s, " t%d:%d", best, types[best]); types[best] = 0;
        }
        s += sprintf(s, " | gdi");
        for (i = 0; i < 32; i++) if (gt[i]) s += sprintf(s, " %x:%d", i, gt[i]);
        puts(buf);
        Sleep(iv * 1000);
    }
}
