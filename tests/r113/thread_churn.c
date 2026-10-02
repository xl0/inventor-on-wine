/* thread_churn.exe [CREATORS] [PER_CREATOR] [io]: CREATORS threads each create PER_CREATOR threads
 * without waiting for them (handle closed at once); the threads exit at once (io: after one
 * overlapped ReadFile on an IOCP-bound pipe that completes immediately). Reports the peak number of
 * number of threads running the thread function, the peak thread count of the process (sampled) and
 * the first CreateThread failure (113).
 * Build: i686-w64-mingw32-gcc -O2 -o thread_churn32.exe thread_churn.c */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <tlhelp32.h>

static LONG peak_threads, done;

/* samples the process' thread count (toolhelp) every 10 ms */
static DWORD WINAPI sampler(void *arg)
{
    while (!done)
    {
        THREADENTRY32 te = { sizeof(te) };
        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
        LONG n = 0;
        if (Thread32First(snap, &te)) do if (te.th32OwnerProcessID == GetCurrentProcessId()) n++; while (Thread32Next(snap, &te));
        CloseHandle(snap);
        if (n > peak_threads) peak_threads = n;
        Sleep(10);
    }
    return 0;
}

static LONG live, peak, created, failed;
static int per, io;
static HANDLE srv, cli, port;

static DWORD WINAPI worker(void *arg)
{
    LONG n = InterlockedIncrement(&live), p;
    while ((p = peak) < n && InterlockedCompareExchange(&peak, n, p) != p);
    if (io)
    {
        OVERLAPPED *ov = calloc(1, sizeof(*ov));
        char buf[1];
        DWORD w;
        WriteFile(cli, "x", 1, &w, NULL);
        if (!ReadFile(srv, buf, 1, NULL, ov) && GetLastError() != ERROR_IO_PENDING) printf("read %lu\n", GetLastError());
    }
    InterlockedDecrement(&live);
    return 0;
}

static DWORD WINAPI creator(void *arg)
{
    int i;
    for (i = 0; i < per; i++)
    {
        HANDLE th = CreateThread(NULL, 0, worker, NULL, 0, NULL);
        if (!th)
        {
            if (InterlockedIncrement(&failed) == 1)
            {
                printf("CreateThread failed %lu after %ld threads, %ld live\n", GetLastError(), created, live);
                fflush(stdout);
                if (getenv("PAUSE")) Sleep(atoi(getenv("PAUSE")));
            }
            Sleep(1);
            continue;
        }
        InterlockedIncrement(&created);
        CloseHandle(th);
    }
    return 0;
}

int main(int argc, char **argv)
{
    int i, n = argc > 1 ? atoi(argv[1]) : 4;
    HANDLE th[64];
    DWORD t = GetTickCount(), bytes;
    ULONG_PTR key;
    OVERLAPPED *o;
    per = argc > 2 ? atoi(argv[2]) : 5000;
    io = argc > 3 && !strcmp(argv[3], "io");
    if (io)
    {
        srv = CreateNamedPipeA("\\\\.\\pipe\\r113c", PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED, PIPE_TYPE_BYTE,
                               1, 1 << 20, 1 << 20, 0, NULL);
        cli = CreateFileA("\\\\.\\pipe\\r113c", GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
        port = CreateIoCompletionPort(srv, NULL, 1, 0);
    }
    CloseHandle(CreateThread(NULL, 0, sampler, NULL, 0, NULL));
    for (i = 0; i < n; i++) th[i] = CreateThread(NULL, 0, creator, NULL, 0, NULL);
    WaitForMultipleObjects(n, th, TRUE, INFINITE);
    while (live) Sleep(10);
    if (io) while (GetQueuedCompletionStatus(port, &bytes, &key, &o, 0)) free(o);
    done = 1;
    printf("%d x %d threads%s: created %ld, failures %ld, peak running %ld, peak threads %ld, %lu ms\n", n, per,
           io ? " io" : "", created, failed, peak, peak_threads, GetTickCount() - t);
    return 0;
}
