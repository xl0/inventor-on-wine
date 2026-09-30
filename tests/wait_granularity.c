/* Timer resolution semantics of short timed waits (issues 088, 089).
 * No args: mean duration of 1 ms waits (Sleep, WaitForSingleObject, MsgWait, condition variable,
 * waitable timers) with the default resolution and after timeBeginPeriod(1).
 * Modes: phase (duration + wake phase vs the 15.625 ms grid per wait type, random start phase),
 * loops (back-to-back means for several timeouts), periods (timeBeginPeriod(N)), nt
 * (NtSetTimerResolution / timeBeginPeriod interplay), running (resolution change during a wait),
 * periodic (periodic waitable timers), pool (threadpool/timer queue/USER timers), mmtimer (timeSetEvent,
 * timeBeginPeriod/timeEndPeriod return values).
 * x86_64-w64-mingw32-gcc -O2 -o wait_granularity.exe wait_granularity.c -lwinmm -lntdll -lsynchronization */
#define _WIN32_WINNT 0x0602
#include <windows.h>
#include <winternl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 2
#endif
NTSTATUS WINAPI NtQueryTimerResolution(ULONG *, ULONG *, ULONG *);
NTSTATUS WINAPI NtSetTimerResolution(ULONG, BOOLEAN, ULONG *);
NTSTATUS WINAPI NtDelayExecution(BOOLEAN, const LARGE_INTEGER *);
NTSTATUS WINAPI NtQuerySystemTime(LARGE_INTEGER *);

#define TICK 15.625
static LARGE_INTEGER freq;
static double now_ms(void) { LARGE_INTEGER t; QueryPerformanceCounter(&t); return t.QuadPart * 1000.0 / freq.QuadPart; }
static void spin(double ms) { double e = now_ms() + ms; while (now_ms() < e); }

static void res(const char *when)
{
    ULONG mn, mx, cur;
    NtQueryTimerResolution(&mn, &mx, &cur);
    printf("%s: NtQueryTimerResolution min %lu max %lu cur %lu (100ns)\n", when, mn, mx, cur);
}

/* one timed wait of ms milliseconds of the given kind; the objects are never signaled */
static HANDLE ev, ev2, port;
static CONDITION_VARIABLE cv = CONDITION_VARIABLE_INIT;
static SRWLOCK lock = SRWLOCK_INIT;
static LONG addr;
static const char *kinds[] = { "Sleep", "SleepEx alertable", "WaitForSingleObject", "WaitForMultipleObjects",
    "MsgWaitForMultipleObjects", "SleepConditionVariableSRW", "WaitOnAddress", "NtWaitForSingleObject abs",
    "NtDelayExecution abs", "waitable timer", "waitable timer abs", "waitable timer HIGH_RES",
    "GetQueuedCompletionStatus", NULL };

static void wait_kind(int k, DWORD ms)
{
    HANDLE h[2] = { ev, ev2 }, t;
    LARGE_INTEGER due;
    LONG cmp = 0;
    OVERLAPPED *ov;
    ULONG_PTR key;
    DWORD size;

    switch (k)
    {
    case 0: Sleep(ms); break;
    case 1: SleepEx(ms, TRUE); break;
    case 2: WaitForSingleObject(ev, ms); break;
    case 3: WaitForMultipleObjects(2, h, FALSE, ms); break;
    case 4: MsgWaitForMultipleObjects(0, NULL, FALSE, ms, QS_ALLINPUT); break;
    case 5: AcquireSRWLockExclusive(&lock); SleepConditionVariableSRW(&cv, &lock, ms, 0); ReleaseSRWLockExclusive(&lock); break;
    case 6: WaitOnAddress(&addr, &cmp, sizeof(cmp), ms); break;
    case 7: NtQuerySystemTime(&due); due.QuadPart += ms * 10000; NtWaitForSingleObject(ev, FALSE, &due); break;
    case 8: NtQuerySystemTime(&due); due.QuadPart += ms * 10000; NtDelayExecution(FALSE, &due); break;
    case 9: case 10: case 11:
        t = CreateWaitableTimerExW(NULL, NULL, k == 11 ? CREATE_WAITABLE_TIMER_HIGH_RESOLUTION : 0, TIMER_ALL_ACCESS);
        if (k == 10) { GetSystemTimePreciseAsFileTime((FILETIME *)&due); due.QuadPart += ms * 10000; }
        else due.QuadPart = -(LONGLONG)ms * 10000;
        SetWaitableTimer(t, &due, 0, NULL, NULL, FALSE);
        WaitForSingleObject(t, INFINITE);
        CloseHandle(t);
        break;
    case 12: GetQueuedCompletionStatus(port, &size, &key, &ov, ms); break;
    }
}

static double loop_mean(int k, DWORD ms, int n)
{
    double t0;
    int i;
    wait_kind(k, ms);
    t0 = now_ms();
    for (i = 0; i < n; i++) wait_kind(k, ms);
    return (now_ms() - t0) / n;
}

/* single waits from a random phase: duration and the phase of the wake-up on the 15.625 ms grid */
static void phase(DWORD ms)
{
    int k, i, n = 40, bins[16];
    double t0, t1, d, mn, mx, sum;
    for (k = 0; kinds[k]; k++)
    {
        memset(bins, 0, sizeof(bins));
        mn = 1e9; mx = sum = 0;
        for (i = 0; i < n; i++)
        {
            spin((rand() % 17000) / 1000.0);
            t0 = now_ms(); wait_kind(k, ms); t1 = now_ms();
            d = t1 - t0; sum += d; if (d < mn) mn = d; if (d > mx) mx = d;
            bins[(int)(fmod(t1, TICK) * 16 / TICK)]++;
        }
        printf("%-27s %3lu ms: min %6.2f mean %6.2f max %6.2f  wake phase /16:", kinds[k], ms, mn, sum / n, mx);
        for (i = 0; i < 16; i++) printf(" %d", bins[i]);
        printf("\n");
    }
}

static void loops(void)
{
    static const DWORD t[] = { 0, 1, 2, 5, 10, 15, 16, 20, 31, 32, 50 };
    int i;
    for (i = 0; i < ARRAYSIZE(t); i++)
        printf("loop %2lu ms: Sleep %6.2f  WaitForSingleObject %6.2f  timer %6.2f  timer abs %6.2f\n", t[i],
               loop_mean(0, t[i], 16), loop_mean(2, t[i], 16), loop_mean(9, t[i], 16), loop_mean(10, t[i], 16));
}

static void periods(void)
{
    static const UINT p[] = { 1, 2, 3, 5, 8, 10, 15, 16, 20 };
    ULONG mn, mx, cur;
    int i;
    for (i = 0; i < ARRAYSIZE(p); i++)
    {
        timeBeginPeriod(p[i]);
        NtQueryTimerResolution(&mn, &mx, &cur);
        printf("timeBeginPeriod(%2u) cur %6lu: Sleep(1) %6.2f  Sleep(3) %6.2f  Sleep(7) %6.2f  timer 1 %6.2f\n", p[i], cur,
               loop_mean(0, 1, 20), loop_mean(0, 3, 20), loop_mean(0, 7, 20), loop_mean(9, 1, 20));
        timeEndPeriod(p[i]);
    }
}

static void ntset(const char *what, ULONG r, BOOLEAN set)
{
    ULONG cur = 0xdead;
    NTSTATUS s = NtSetTimerResolution(r, set, &cur);
    printf("%-40s NtSetTimerResolution(%lu, %u) = %#lx cur %lu -> Sleep(1) %6.2f\n", what, r, set, s, cur, loop_mean(0, 1, 20));
}

static void nt(void)
{
    printf("start: Sleep(1) %6.2f\n", loop_mean(0, 1, 20));
    ntset("clear without set", 10000, FALSE);
    ntset("set 1 ms", 10000, TRUE);
    ntset("set 1 ms again", 10000, TRUE);
    res("after set");
    ntset("clear", 10000, FALSE);
    ntset("clear again", 10000, FALSE);
    ntset("set 5 ms", 50000, TRUE);
    ntset("set 0.5 ms", 5000, TRUE);
    ntset("clear (other value)", 12345, FALSE);
    ntset("set 1 (100 ns)", 1, TRUE);
    ntset("clear", 1, FALSE);
    ntset("set 20 ms", 200000, TRUE);
    ntset("clear", 200000, FALSE);
    timeBeginPeriod(1);
    printf("timeBeginPeriod(1): Sleep(1) %6.2f\n", loop_mean(0, 1, 20));
    ntset("clear after timeBeginPeriod", 10000, FALSE);
    timeEndPeriod(1);
    printf("timeEndPeriod(1): Sleep(1) %6.2f\n", loop_mean(0, 1, 20));
    ntset("set 1 ms", 10000, TRUE);
    timeBeginPeriod(1); timeEndPeriod(1);
    printf("timeBegin+EndPeriod(1) after Nt set: Sleep(1) %6.2f\n", loop_mean(0, 1, 20));
    ntset("clear", 10000, FALSE);
    timeBeginPeriod(1); timeBeginPeriod(1); timeEndPeriod(1);
    printf("timeBeginPeriod(1) x2 + End x1: Sleep(1) %6.2f\n", loop_mean(0, 1, 20));
    timeEndPeriod(1);
    printf("End x2: Sleep(1) %6.2f\n", loop_mean(0, 1, 20));
    printf("timeEndPeriod(1) unbalanced = %u\n", timeEndPeriod(1));
    timeBeginPeriod(5); timeBeginPeriod(2);
    printf("timeBeginPeriod(5)+(2): Sleep(1) %6.2f\n", loop_mean(0, 1, 20));
    timeEndPeriod(2);
    printf("timeEndPeriod(2): Sleep(1) %6.2f\n", loop_mean(0, 1, 20));
    timeEndPeriod(5);
    printf("timeEndPeriod(5): Sleep(1) %6.2f\n", loop_mean(0, 1, 20));
}

static double wstart, wend;
static int wkind; static DWORD wms;
static DWORD CALLBACK waiter(void *arg)
{
    Sleep(1); /* start just after a tick */
    wstart = now_ms(); wait_kind(wkind, wms); wend = now_ms();
    return 0;
}

static void running_one(int k, DWORD ms, BOOL raise, double after)
{
    HANDLE th;
    wkind = k; wms = ms;
    if (!raise) timeBeginPeriod(1);
    th = CreateThread(NULL, 0, waiter, NULL, 0, NULL);
    Sleep(after);
    if (raise) timeBeginPeriod(1); else timeEndPeriod(1);
    WaitForSingleObject(th, INFINITE); CloseHandle(th);
    if (raise) timeEndPeriod(1);
    printf("%-27s %lu ms, %s after ~%.0f ms: %6.2f ms\n", kinds[k], ms, raise ? "timeBeginPeriod(1)" : "timeEndPeriod(1)",
           after, wend - wstart);
}

static void running(void)
{
    static const int k[] = { 0, 2, 4, 9 };
    int i, j;
    LARGE_INTEGER due;
    HANDLE t;
    double t0;

    for (i = 0; i < ARRAYSIZE(k); i++)
        for (j = 0; j < 3; j++) running_one(k[i], 50, TRUE, 20);
    for (i = 0; i < ARRAYSIZE(k); i++)
        for (j = 0; j < 3; j++) running_one(k[i], 45, FALSE, 20);
    /* timer set before the change, waited after it */
    for (j = 0; j < 3; j++)
    {
        Sleep(1);
        t = CreateWaitableTimerW(NULL, FALSE, NULL);
        due.QuadPart = -10000;
        t0 = now_ms();
        SetWaitableTimer(t, &due, 0, NULL, NULL, FALSE);
        timeBeginPeriod(1);
        WaitForSingleObject(t, INFINITE);
        printf("timer 1 ms set, then timeBeginPeriod(1), then waited: %6.2f\n", now_ms() - t0);
        timeEndPeriod(1);
        CloseHandle(t);
    }
    for (j = 0; j < 3; j++)
    {
        timeBeginPeriod(1);
        Sleep(1);
        t = CreateWaitableTimerW(NULL, FALSE, NULL);
        due.QuadPart = -30000;
        t0 = now_ms();
        SetWaitableTimer(t, &due, 0, NULL, NULL, FALSE);
        timeEndPeriod(1);
        WaitForSingleObject(t, INFINITE);
        printf("timer 3 ms set under period 1, then timeEndPeriod(1), then waited: %6.2f\n", now_ms() - t0);
        CloseHandle(t);
    }
}

static void periodic_one(DWORD flags, LONG period, const char *tag)
{
    HANDLE t = CreateWaitableTimerExW(NULL, NULL, flags, TIMER_ALL_ACCESS);
    LARGE_INTEGER due;
    double t0, prev, iv[8];
    int n = 0;
    due.QuadPart = -(LONGLONG)period * 10000;
    t0 = prev = now_ms();
    SetWaitableTimer(t, &due, period, NULL, NULL, FALSE);
    while (now_ms() - t0 < 500)
    {
        WaitForSingleObject(t, INFINITE);
        if (n < 8) iv[n] = now_ms() - prev;
        prev = now_ms(); n++;
    }
    CancelWaitableTimer(t); CloseHandle(t);
    printf("%s periodic %s %2ld ms: %4d signals in 500 ms, first intervals %.1f %.1f %.1f %.1f %.1f %.1f %.1f %.1f\n", tag,
           flags ? "HIGH_RES" : "normal  ", period, n, iv[0], iv[1], iv[2], iv[3], iv[4], iv[5], iv[6], iv[7]);
}

static void periodic(void)
{
    static const LONG p[] = { 1, 5, 20 };
    int i;
    for (i = 0; i < ARRAYSIZE(p); i++) periodic_one(0, p[i], "default");
    for (i = 0; i < ARRAYSIZE(p); i++) periodic_one(CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, p[i], "default");
    timeBeginPeriod(1);
    for (i = 0; i < ARRAYSIZE(p); i++) periodic_one(0, p[i], "period1");
    timeEndPeriod(1);
}

static double cb_time;
static HANDLE cb_ev;
static void CALLBACK pool_cb(PTP_CALLBACK_INSTANCE inst, void *ctx, PTP_TIMER timer) { cb_time = now_ms(); SetEvent(cb_ev); }
static void CALLBACK queue_cb(void *ctx, BOOLEAN fired) { cb_time = now_ms(); SetEvent(cb_ev); }
static LRESULT CALLBACK wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) { return DefWindowProcW(hwnd, msg, wp, lp); }

static void pool_run(const char *tag)
{
    PTP_TIMER pt = CreateThreadpoolTimer(pool_cb, NULL, NULL);
    HANDLE qt, wh;
    FILETIME ft;
    MSG msg;
    double t0, sum;
    int i, n = 20;

    for (sum = 0, i = 0; i < n; i++)
    {
        { ULARGE_INTEGER u; u.QuadPart = (ULONGLONG)-10000; ft.dwLowDateTime = u.LowPart; ft.dwHighDateTime = u.HighPart; }
        t0 = now_ms(); SetThreadpoolTimer(pt, &ft, 0, 0);
        WaitForSingleObject(cb_ev, INFINITE); sum += cb_time - t0;
    }
    printf("%s threadpool timer 1 ms           %6.2f\n", tag, sum / n);
    WaitForThreadpoolTimerCallbacks(pt, TRUE); CloseThreadpoolTimer(pt);
    for (sum = 0, i = 0; i < n; i++)
    {
        t0 = now_ms(); CreateTimerQueueTimer(&qt, NULL, queue_cb, NULL, 1, 0, 0);
        WaitForSingleObject(cb_ev, INFINITE); sum += cb_time - t0;
        DeleteTimerQueueTimer(NULL, qt, INVALID_HANDLE_VALUE);
    }
    printf("%s timer queue timer 1 ms          %6.2f\n", tag, sum / n);
    for (sum = 0, i = 0; i < n; i++)
    {
        t0 = now_ms(); RegisterWaitForSingleObject(&wh, ev, queue_cb, NULL, 1, WT_EXECUTEONLYONCE);
        WaitForSingleObject(cb_ev, INFINITE); sum += cb_time - t0;
        UnregisterWaitEx(wh, INVALID_HANDLE_VALUE);
    }
    printf("%s RegisterWaitForSingleObject 1 ms %6.2f\n", tag, sum / n);
    {
        HWND hwnd = CreateWindowW(L"wg_class", L"", 0, 0, 0, 10, 10, NULL, NULL, NULL, NULL);
        UINT ms[] = { 1, 10, 20 };
        int j;
        for (j = 0; j < 3; j++)
        {
            SetTimer(hwnd, 1, ms[j], NULL);
            GetMessageW(&msg, hwnd, WM_TIMER, WM_TIMER);
            t0 = now_ms();
            for (i = 0; i < n; i++) GetMessageW(&msg, hwnd, WM_TIMER, WM_TIMER);
            printf("%s WM_TIMER %2u ms interval          %6.2f\n", tag, ms[j], (now_ms() - t0) / n);
            KillTimer(hwnd, 1);
        }
        DestroyWindow(hwnd);
    }
}

static void pool(void)
{
    WNDCLASSW wc = { 0 };
    wc.lpfnWndProc = wndproc; wc.lpszClassName = L"wg_class";
    RegisterClassW(&wc);
    cb_ev = CreateEventW(NULL, FALSE, FALSE, NULL);
    pool_run("default");
    timeBeginPeriod(1);
    pool_run("period1");
    timeEndPeriod(1);
}

static LONG mm_count;
static void CALLBACK mm_cb(UINT id, UINT msg, DWORD_PTR user, DWORD_PTR dw1, DWORD_PTR dw2) { InterlockedIncrement(&mm_count); }

static void mmtimer(void)
{
    static const UINT d[][2] = { { 1, 1 }, { 1, 10 }, { 5, 0 }, { 10, 10 } };
    TIMECAPS caps;
    MMRESULT r[4];
    ULONG cur;
    UINT id;
    int i;

    timeGetDevCaps(&caps, sizeof(caps));
    printf("timeGetDevCaps min %u max %u\n", caps.wPeriodMin, caps.wPeriodMax);
    for (i = 0; i < ARRAYSIZE(d); i++)
    {
        mm_count = 0;
        id = timeSetEvent(d[i][0], d[i][1], mm_cb, 0, TIME_PERIODIC);
        printf("timeSetEvent(%u, res %u) periodic: Sleep(1) meanwhile %6.2f", d[i][0], d[i][1], loop_mean(0, 1, 20));
        Sleep(500);
        printf(", %ld callbacks in ~800 ms\n", mm_count);
        timeKillEvent(id);
    }
    printf("after timeKillEvent: Sleep(1) %6.2f\n", loop_mean(0, 1, 20));
    /* one call per statement: argument evaluation order is unspecified */
    r[0] = timeBeginPeriod(0); r[1] = timeBeginPeriod(16); r[2] = timeBeginPeriod(20); r[3] = timeBeginPeriod(65536);
    printf("timeBeginPeriod(0) = %u, (16) = %u, (20) = %u, (65536) = %u\n", r[0], r[1], r[2], r[3]);
    r[0] = timeEndPeriod(16); r[1] = timeEndPeriod(20); r[2] = timeEndPeriod(20);
    printf("timeEndPeriod(16) = %u, (20) = %u, again (20) = %u\n", r[0], r[1], r[2]);
    r[0] = timeEndPeriod(1);
    printf("unbalanced timeEndPeriod(1) = %u\n", r[0]);
    r[0] = timeBeginPeriod(5); r[1] = timeEndPeriod(2);
    printf("timeBeginPeriod(5) = %u, timeEndPeriod(2) = %u -> Sleep(1) %6.2f\n", r[0], r[1], loop_mean(0, 1, 20));
    r[0] = timeEndPeriod(5);
    printf("timeEndPeriod(5) = %u -> Sleep(1) %6.2f\n", r[0], loop_mean(0, 1, 20));
    r[0] = timeBeginPeriod(1); NtSetTimerResolution(10000, FALSE, &cur); r[1] = timeEndPeriod(1); r[2] = timeEndPeriod(1);
    printf("timeBeginPeriod(1) = %u, NtSetTimerResolution clear, timeEndPeriod(1) = %u, again = %u\n", r[0], r[1], r[2]);
    r[0] = timeBeginPeriod(1); NtSetTimerResolution(10000, FALSE, &cur); r[1] = timeBeginPeriod(1);
    printf("timeBeginPeriod(1) = %u, clear, timeBeginPeriod(1) = %u -> Sleep(1) %6.2f\n", r[0], r[1], loop_mean(0, 1, 20));
    timeEndPeriod(1); timeEndPeriod(1);
}

int main(int argc, char **argv)
{
    const char *m = argc > 1 ? argv[1] : "";
    int k;

    QueryPerformanceFrequency(&freq);
    ev = CreateEventW(NULL, FALSE, FALSE, NULL);
    ev2 = CreateEventW(NULL, FALSE, FALSE, NULL);
    port = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 1);
    srand(GetTickCount());
    setvbuf(stdout, NULL, _IONBF, 0);
    if (!strcmp(m, "phase")) { phase(argc > 2 ? atoi(argv[2]) : 1); return 0; }
    if (!strcmp(m, "loops")) { loops(); return 0; }
    if (!strcmp(m, "periods")) { periods(); return 0; }
    if (!strcmp(m, "nt")) { nt(); return 0; }
    if (!strcmp(m, "running")) { running(); return 0; }
    if (!strcmp(m, "periodic")) { periodic(); return 0; }
    if (!strcmp(m, "pool")) { pool(); return 0; }
    if (!strcmp(m, "mmtimer")) { mmtimer(); return 0; }

    res("default");
    for (k = 0; kinds[k]; k++) printf("default %-27s 1 ms %6.2f ms\n", kinds[k], loop_mean(k, 1, 40));
    timeBeginPeriod(1);
    res("timeBeginPeriod(1)");
    for (k = 0; kinds[k]; k++) printf("period1 %-27s 1 ms %6.2f ms\n", kinds[k], loop_mean(k, 1, 40));
    timeEndPeriod(1);
    return 0;
}
