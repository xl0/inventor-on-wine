/* io_threads.exe [N] [MODE]: N short-lived threads; each issues one overlapped ReadFile on a pipe and
 * exits while it is pending; main completes it (WriteFile) and reaps the completion (113).
 * MODE: iocp (handle bound to a port, default), event (no port), none (threads just exit),
 *       done (read completes before the thread exits), imm (data already there: the read
 *       completes at once, the packet goes to the port), immwait (imm, main waits for the thread).
 * Prints reserved/committed address space every N/10 threads, stops at the first CreateThread failure.
 * Build: i686-w64-mingw32-gcc -O2 -o io_threads32.exe io_threads.c */
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static HANDLE srv, cli, port, ev;
static OVERLAPPED ov;
static char buf[16];
static const char *mode = "iocp";
static LONG immediate, aborted;

static DWORD WINAPI issuer(void *arg)
{
    if (!strcmp(mode, "none")) return 0;
    memset(&ov, 0, sizeof(ov));
    ov.hEvent = ev;
    if (ReadFile(srv, buf, 1, NULL, &ov)) immediate++;
    else if (GetLastError() != ERROR_IO_PENDING) printf("ReadFile %lu\n", GetLastError());
    if (!strcmp(mode, "done"))
    {
        DWORD n;
        WriteFile(cli, "x", 1, &n, NULL);
        WaitForSingleObject(ev, INFINITE);
    }
    return 0;
}

static void vm_usage(SIZE_T *reserved, SIZE_T *committed)
{
    MEMORY_BASIC_INFORMATION mbi;
    char *p = NULL;
    *reserved = *committed = 0;
    while (VirtualQuery(p, &mbi, sizeof(mbi)))
    {
        if (mbi.State == MEM_COMMIT) *committed += mbi.RegionSize;
        if (mbi.State != MEM_FREE) *reserved += mbi.RegionSize;
        p = (char *)mbi.BaseAddress + mbi.RegionSize;
        if (!p) break;
    }
}

int main(int argc, char **argv)
{
    int i, n = argc > 1 ? atoi(argv[1]) : 10000;
    SIZE_T res, com;
    DWORD bytes, w;
    ULONG_PTR key;
    OVERLAPPED *o;
    if (argc > 2) mode = argv[2];
    srv = CreateNamedPipeA("\\\\.\\pipe\\r113", PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
                           PIPE_TYPE_BYTE, 1, 4096, 4096, 0, NULL);
    cli = CreateFileA("\\\\.\\pipe\\r113", GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    ev = CreateEventA(NULL, FALSE, FALSE, NULL);
    if (!strncmp(mode, "imm", 3) || !strcmp(mode, "iocp")) port = CreateIoCompletionPort(srv, NULL, 1, 0);
    vm_usage(&res, &com);
    printf("mode %s start: reserved %lu MB committed %lu MB\n", mode, (ULONG)(res >> 20), (ULONG)(com >> 20));
    for (i = 1; i <= n; i++)
    {
        HANDLE th;
        if (!strncmp(mode, "imm", 3)) WriteFile(cli, "x", 1, &w, NULL);
        th = CreateThread(NULL, 0, issuer, NULL, 0, NULL);
        if (!th)
        {
            vm_usage(&res, &com);
            printf("CreateThread %d failed %lu: reserved %lu MB committed %lu MB\n", i, GetLastError(),
                   (ULONG)(res >> 20), (ULONG)(com >> 20));
            return 1;
        }
        if (strcmp(mode, "imm")) WaitForSingleObject(th, INFINITE);
        CloseHandle(th);
        if (!strncmp(mode, "imm", 3)) GetQueuedCompletionStatus(port, &bytes, &key, &o, INFINITE);
        else if (strcmp(mode, "none") && strcmp(mode, "done"))
        {
            WriteFile(cli, "x", 1, &w, NULL);
            if (port) GetQueuedCompletionStatus(port, &bytes, &key, &o, INFINITE);
            else WaitForSingleObject(ev, INFINITE);
            if (ov.Internal == 0xc0000120) /* cancelled at thread exit (Windows): drain the byte */
            {
                static OVERLAPPED ov2;
                aborted++;
                memset(&ov2, 0, sizeof(ov2));
                ov2.hEvent = ev;
                if (!ReadFile(srv, buf, 1, NULL, &ov2) && GetLastError() != ERROR_IO_PENDING) printf("drain %lu\n", GetLastError());
                if (port) GetQueuedCompletionStatus(port, &bytes, &key, &o, INFINITE);
                else WaitForSingleObject(ev, INFINITE);
            }
        }
        if (!(i % (n / 10)))
        {
            vm_usage(&res, &com);
            printf("%6d threads: reserved %lu MB committed %lu MB (reads: %ld immediate, %ld cancelled)\n", i,
                   (ULONG)(res >> 20), (ULONG)(com >> 20), immediate, aborted);
            fflush(stdout);
        }
    }
    return 0;
}
