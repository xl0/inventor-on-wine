/* Absolute wait deadlines and absolute waitable-timer due times (issue 093).
 * No args: per wait kind, deadline base (NtQuerySystemTime = GetSystemTimeAsFileTime, or
 * GetSystemTimePreciseAsFileTime) and offset (ms; <= 0 = past), with the default resolution and
 * after timeBeginPeriod(1): elapsed min/mean/max (QPC ms) and how many returned before the precise
 * system time reached the deadline ("early", with the worst margin in us) or before
 * NtQuerySystemTime() did ("sys early").
 * `rel`: relative 1 ms condition variable waits (futex path on Wine) under timeBeginPeriod(1).
 * `bench`: ns per GetSystemTime(Precise)AsFileTime call.
 * `clock`: steps the wall clock during absolute waits (needs SeSystemtimePrivilege; restores it).
 * x86_64-w64-mingw32-gcc -O2 -o abs_timeout.exe abs_timeout.c -lwinmm -lntdll -lsynchronization */
#define _WIN32_WINNT 0x0602
#include <windows.h>
#include <winternl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 2
#endif
NTSTATUS WINAPI NtDelayExecution(BOOLEAN, const LARGE_INTEGER *);
NTSTATUS WINAPI NtQuerySystemTime(LARGE_INTEGER *);
NTSTATUS WINAPI NtQueryTimerResolution(ULONG *, ULONG *, ULONG *);

static LARGE_INTEGER freq;
static HANDLE ev;
static double now_ms(void) { LARGE_INTEGER t; QueryPerformanceCounter(&t); return t.QuadPart * 1000.0 / freq.QuadPart; }
static LONGLONG precise(void) { LARGE_INTEGER t; GetSystemTimePreciseAsFileTime((FILETIME *)&t); return t.QuadPart; }

static const char *kinds[] = { "NtWaitForSingleObject", "NtDelayExecution", "WaitableTimer",
                               "WaitableTimer HIGH_RES", NULL };

/* wait until the absolute time due; returns the precise system time right after the wait,
 * *sys = NtQuerySystemTime() after it */
static LONGLONG wait_abs(int k, LARGE_INTEGER due, LONGLONG *sys)
{
    LARGE_INTEGER s;
    HANDLE t;

    switch (k)
    {
    case 0: NtWaitForSingleObject(ev, FALSE, &due); break;
    case 1: NtDelayExecution(FALSE, &due); break;
    case 2: case 3:
        t = CreateWaitableTimerExW(NULL, NULL, k == 3 ? CREATE_WAITABLE_TIMER_HIGH_RESOLUTION : 0, TIMER_ALL_ACCESS);
        SetWaitableTimer(t, &due, 0, NULL, NULL, FALSE);
        WaitForSingleObject(t, INFINITE);
        CloseHandle(t);
        break;
    }
    NtQuerySystemTime(&s);
    *sys = s.QuadPart;
    return precise();
}

static void run(const char *tag)
{
    static const int offs[] = { 1, 5, 20, 0, -1, -1000 };
    int k, b, o, i, n = 20;

    for (k = 0; kinds[k]; k++)
    for (b = 0; b < 2; b++)
    for (o = 0; o < ARRAYSIZE(offs); o++)
    {
        double mn = 1e9, mx = 0, sum = 0, worst = 0;
        int early = 0, early_sys = 0;

        for (i = 0; i < n; i++)
        {
            LARGE_INTEGER due;
            LONGLONG after, sys;
            double t0, e, spin = t0 = now_ms();

            while (now_ms() < spin + (rand() % 1000) / 250.0);  /* random phase, 0..4 ms */
            if (b) due.QuadPart = precise(); else NtQuerySystemTime(&due);
            due.QuadPart += offs[o] * 10000LL;
            t0 = now_ms();
            after = wait_abs(k, due, &sys);
            e = now_ms() - t0;
            if (e < mn) mn = e;
            if (e > mx) mx = e;
            sum += e;
            if (sys < due.QuadPart) early_sys++;
            if (after < due.QuadPart)
            {
                early++;
                if ((due.QuadPart - after) / 10.0 > worst) worst = (due.QuadPart - after) / 10.0;
            }
        }
        printf("%-8s %-23s %-7s %+5d ms: %6.2f %6.2f %6.2f  early %2d/%d (worst %5.0f us) sys early %2d\n", tag,
               kinds[k], b ? "precise" : "system", offs[o], mn, sum / n, mx, early, n, worst, early_sys);
    }
}

/* step the wall clock by `step` s 200 ms into an absolute wait `ahead` s long, restore it afterwards */
static HANDLE go;
static LONG step_s;
static DWORD WINAPI stepper(void *arg)
{
    LARGE_INTEGER t;
    SYSTEMTIME st;

    WaitForSingleObject(go, INFINITE);
    Sleep(200);
    GetSystemTimePreciseAsFileTime((FILETIME *)&t);
    t.QuadPart += step_s * 10000000LL;
    FileTimeToSystemTime((FILETIME *)&t, &st);
    if (!SetSystemTime(&st)) printf("SetSystemTime failed %lu\n", GetLastError());
    return 0;
}

static void clock_step(int k, LONG step, int ahead)
{
    LARGE_INTEGER due, t;
    LONGLONG sys;
    SYSTEMTIME st;
    HANDLE th;
    double t0, e;

    step_s = step;
    go = CreateEventW(NULL, TRUE, FALSE, NULL);
    th = CreateThread(NULL, 0, stepper, NULL, 0, NULL);
    due.QuadPart = precise() + ahead * 10000000LL;
    t0 = now_ms();
    SetEvent(go);
    wait_abs(k, due, &sys);
    e = now_ms() - t0;
    WaitForSingleObject(th, INFINITE);
    /* undo the step */
    GetSystemTimePreciseAsFileTime((FILETIME *)&t);
    t.QuadPart -= step * 10000000LL;
    FileTimeToSystemTime((FILETIME *)&t, &st);
    SetSystemTime(&st);
    printf("clock %-23s deadline +%d s, step %+ld s after 200 ms: waited %7.1f ms\n", kinds[k], ahead, step, e);
    CloseHandle(th);
    CloseHandle(go);
}

int main(int argc, char **argv)
{
    ULONG mn, mx, cur;
    int k;

    QueryPerformanceFrequency(&freq);
    ev = CreateEventW(NULL, FALSE, FALSE, NULL);
    srand(GetTickCount());
    setvbuf(stdout, NULL, _IONBF, 0);
    if (argc > 1 && !strcmp(argv[1], "rel"))
    {
        CONDITION_VARIABLE cv = CONDITION_VARIABLE_INIT;
        SRWLOCK lock = SRWLOCK_INIT;
        CRITICAL_SECTION cs;
        int i, j, n = 200;

        InitializeCriticalSection(&cs);
        timeBeginPeriod(1);
        for (j = 0; j < 2; j++)
        {
            double mn = 1e9, e, t0;
            int early = 0;
            for (i = 0; i < n; i++)
            {
                t0 = now_ms();
                if (j) SleepConditionVariableCS(&cv, &cs, 1);
                else { AcquireSRWLockExclusive(&lock); SleepConditionVariableSRW(&cv, &lock, 1, 0); ReleaseSRWLockExclusive(&lock); }
                e = now_ms() - t0;
                if (e < mn) mn = e;
                if (e < 1.0) early++;
            }
            printf("period1 SleepConditionVariable%s 1 ms: min %.3f ms, %d/%d under 1 ms\n", j ? "CS" : "SRW", mn, early, n);
        }
        return 0;
    }
    if (argc > 1 && !strcmp(argv[1], "bench"))
    {
        FILETIME ft;
        double t0 = now_ms();
        for (k = 0; k < 10000000; k++) GetSystemTimeAsFileTime(&ft);
        printf("GetSystemTimeAsFileTime %.1f ns/call\n", (now_ms() - t0) * 1e6 / 1e7);
        t0 = now_ms();
        for (k = 0; k < 10000000; k++) GetSystemTimePreciseAsFileTime(&ft);
        printf("GetSystemTimePreciseAsFileTime %.1f ns/call\n", (now_ms() - t0) * 1e6 / 1e7);
        return 0;
    }
    if (argc > 1 && !strcmp(argv[1], "clock"))
    {
        HANDLE tok;
        TOKEN_PRIVILEGES tp = { 1 };
        OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES, &tok);
        LookupPrivilegeValueA(NULL, "SeSystemtimePrivilege", &tp.Privileges[0].Luid);
        tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
        AdjustTokenPrivileges(tok, FALSE, &tp, 0, NULL, NULL);
        printf("privilege: %lu\n", GetLastError());
        for (k = 0; kinds[k]; k++)
        {
            clock_step(k, 2, 1);
            clock_step(k, -2, 1);
        }
        return 0;
    }
    NtQueryTimerResolution(&mn, &mx, &cur);
    printf("resolution cur %lu; columns: min mean max elapsed (ms)\n", cur);
    run("default");
    timeBeginPeriod(1);
    run("period1");
    timeEndPeriod(1);
    return 0;
}
