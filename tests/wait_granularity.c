/* Mean duration of short timed waits (Sleep, WaitForSingleObject, waitable timers, condition
 * variable, MsgWaitForMultipleObjects) with the default timer resolution and after
 * timeBeginPeriod(1); NtQueryTimerResolution before/after (issue 088).
 * x86_64-w64-mingw32-gcc -O2 -o wait_granularity.exe wait_granularity.c -lwinmm -lntdll */
#define _WIN32_WINNT 0x0602
#include <windows.h>
#include <stdio.h>

#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 2
#endif
NTSTATUS WINAPI NtQueryTimerResolution(ULONG *, ULONG *, ULONG *);

static LARGE_INTEGER freq;
static double now_ms(void) { LARGE_INTEGER t; QueryPerformanceCounter(&t); return t.QuadPart * 1000.0 / freq.QuadPart; }

static void res(const char *when)
{
    ULONG mn, mx, cur;
    NtQueryTimerResolution(&mn, &mx, &cur);
    printf("%s: NtQueryTimerResolution min %lu max %lu cur %lu (100ns)\n", when, mn, mx, cur);
}

static void run(const char *tag)
{
    HANDLE ev = CreateEventW(NULL, FALSE, FALSE, NULL), t;
    CONDITION_VARIABLE cv = CONDITION_VARIABLE_INIT;
    SRWLOCK lock = SRWLOCK_INIT;
    LARGE_INTEGER due;
    const int n = 40;
    double t0;
    int i, flags;

    t0 = now_ms(); for (i = 0; i < n; i++) Sleep(1);
    printf("%s Sleep(1)                    %6.2f ms\n", tag, (now_ms() - t0) / n);
    t0 = now_ms(); for (i = 0; i < n; i++) WaitForSingleObject(ev, 1);
    printf("%s WaitForSingleObject(ev, 1)  %6.2f ms\n", tag, (now_ms() - t0) / n);
    t0 = now_ms(); for (i = 0; i < n; i++) MsgWaitForMultipleObjects(0, NULL, FALSE, 1, QS_ALLINPUT);
    printf("%s MsgWaitForMultipleObjects 1 %6.2f ms\n", tag, (now_ms() - t0) / n);
    AcquireSRWLockExclusive(&lock);
    t0 = now_ms(); for (i = 0; i < n; i++) SleepConditionVariableSRW(&cv, &lock, 1, 0);
    printf("%s SleepConditionVariableSRW 1 %6.2f ms\n", tag, (now_ms() - t0) / n);
    ReleaseSRWLockExclusive(&lock);
    for (flags = 0; flags <= CREATE_WAITABLE_TIMER_HIGH_RESOLUTION; flags += CREATE_WAITABLE_TIMER_HIGH_RESOLUTION)
    {
        t0 = now_ms();
        for (i = 0; i < n; i++)
        {
            t = CreateWaitableTimerExW(NULL, NULL, flags, TIMER_ALL_ACCESS);
            due.QuadPart = -10000;
            SetWaitableTimer(t, &due, 0, NULL, NULL, FALSE);
            WaitForSingleObject(t, INFINITE);
            CloseHandle(t);
        }
        printf("%s waitable timer 1 ms%s  %6.2f ms\n", tag, flags ? " (HIGH_RES)" : "          ", (now_ms() - t0) / n);
    }
    CloseHandle(ev);
}

int main(void)
{
    QueryPerformanceFrequency(&freq);
    res("default");
    run("default");
    timeBeginPeriod(1);
    res("timeBeginPeriod(1)");
    run("period1");
    timeEndPeriod(1);
    return 0;
}
