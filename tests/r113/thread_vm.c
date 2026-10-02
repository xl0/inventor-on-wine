/* thread_vm.exe [N]: address space per thread: reserved/committed (VirtualQuery) before and after
 * creating N suspended threads, per region size histogram of the new regions (113).
 * Build: i686-w64-mingw32-gcc -O2 -o thread_vm32.exe thread_vm.c */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

static DWORD WINAPI nop(void *arg) { return 0; }

static void usage(SIZE_T *res, SIZE_T *com, SIZE_T *top)
{
    MEMORY_BASIC_INFORMATION mbi;
    char *p = NULL;
    *res = *com = *top = 0;
    while (VirtualQuery(p, &mbi, sizeof(mbi)))
    {
        if (mbi.State == MEM_COMMIT) *com += mbi.RegionSize;
        if (mbi.State != MEM_FREE) { *res += mbi.RegionSize; *top = (SIZE_T)mbi.BaseAddress + mbi.RegionSize; }
        p = (char *)mbi.BaseAddress + mbi.RegionSize;
        if (!p) break;
    }
}

int main(int argc, char **argv)
{
    int i, n = argc > 1 ? atoi(argv[1]) : 100;
    SIZE_T r0, c0, t0, r1, c1, t1;
    HANDLE *th = malloc(n * sizeof(*th));
    MEMORY_BASIC_INFORMATION mbi;
    usage(&r0, &c0, &t0);
    {
        LARGE_INTEGER f, a, b, c;
        QueryPerformanceFrequency(&f);
        QueryPerformanceCounter(&a);
        for (i = 0; i < n; i++) th[i] = CreateThread(NULL, 0, nop, NULL, CREATE_SUSPENDED, NULL);
        QueryPerformanceCounter(&b);
        for (i = 0; i < n; i++) { HANDLE h = CreateThread(NULL, 0, nop, NULL, 0, NULL); WaitForSingleObject(h, INFINITE); CloseHandle(h); }
        QueryPerformanceCounter(&c);
        printf("CreateThread (suspended) %lu us, create+run+exit+wait %lu us\n",
               (ULONG)((b.QuadPart - a.QuadPart) * 1000000 / f.QuadPart / n), (ULONG)((c.QuadPart - b.QuadPart) * 1000000 / f.QuadPart / n));
    }
    usage(&r1, &c1, &t1);
    printf("%d suspended threads: reserved +%lu KB/thread, committed +%lu KB/thread (top %p)\n", n,
           (ULONG)((r1 - r0) / n >> 10), (ULONG)((c1 - c0) / n >> 10), (void *)t1);
    /* regions of the last thread's stack */
    {
        CONTEXT ctx = { .ContextFlags = CONTEXT_CONTROL };
        GetThreadContext(th[n - 1], &ctx);
#ifdef _WIN64
        VirtualQuery((void *)ctx.Rsp, &mbi, sizeof(mbi));
#else
        VirtualQuery((void *)ctx.Esp, &mbi, sizeof(mbi));
#endif
        printf("last thread stack allocation %p\n", mbi.AllocationBase);
    }
    for (i = 0; i < n; i++) { ResumeThread(th[i]); WaitForSingleObject(th[i], INFINITE); CloseHandle(th[i]); }
    usage(&r1, &c1, &t1);
    printf("after exit: reserved %+ld KB, committed %+ld KB\n", (long)(r1 - r0) >> 10, (long)(c1 - c0) >> 10);
    return 0;
}
