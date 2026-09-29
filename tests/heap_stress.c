/* Heap allocator stress (issue 073): random sizes and lifetimes, ns/op over time.
 * heap_stress.exe [ops_M=40] [live=20000] [maxsize=1048576] [keep_permille=0.1] [private]
 *  - live: slots of short-lived blocks; each op frees a random slot and refills it
 *  - sizes: log-uniform 16..maxsize (every octave equally likely)
 *  - keep_permille: share of allocations kept forever (grows the heap, pins holes)
 *  - private: HeapCreate(0,0,0) instead of the process heap
 * Prints ns/op (alloc+free pair) and commit per million ops and HeapCompatibilityInformation of the process heap,
 * the CRT heap and the test heap at start and end.
 * Build: x86_64-w64-mingw32-gcc -O2 -o heap_stress.exe heap_stress.c */
#define PSAPI_VERSION 2
#include <windows.h>
#include <psapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static unsigned long long rng = 88172645463325252ull;
static unsigned long long rnd(void) { rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17; return rng; }

static ULONG compat(HANDLE heap)
{
    ULONG info = ~0u;
    HeapQueryInformation(heap, HeapCompatibilityInformation, &info, sizeof(info), NULL);
    return info;
}

static void show(const char *when, HANDLE heap)
{
    HANDLE crt = (HANDLE)_get_heap_handle();
    printf("%s: compat process %lu crt %lu (same %d) test %lu\n", when, compat(GetProcessHeap()),
           compat(crt), crt == GetProcessHeap(), compat(heap));
}

int main(int argc, char **argv)
{
    int ops_m = argc > 1 ? atoi(argv[1]) : 40, live = argc > 2 ? atoi(argv[2]) : 20000;
    double maxsize = argc > 3 ? atof(argv[3]) : 1048576, keep = argc > 4 ? atof(argv[4]) / 1000 : 0.0001;
    HANDLE heap = argc > 5 && !strcmp(argv[5], "private") ? HeapCreate(0, 0, 0) : GetProcessHeap();
    double lmin = log(16), lspan = log(maxsize) - lmin;
    void **slots = calloc(live, sizeof(*slots));
    LARGE_INTEGER f, t0, t1, tstart;
    PROCESS_MEMORY_COUNTERS pmc;
    size_t kept = 0;
    int m, i;

    QueryPerformanceFrequency(&f);
    show("start", heap);
    QueryPerformanceCounter(&tstart);
    for (m = 0; m < ops_m; m++)
    {
        QueryPerformanceCounter(&t0);
        for (i = 0; i < 1000000; i++)
        {
            unsigned long long r = rnd();
            SIZE_T size = (SIZE_T)exp(lmin + lspan * ((r >> 11) & 0xffffff) / 16777216.0);
            void **slot = &slots[(r >> 40) % live];
            void *p = HeapAlloc(heap, 0, size);
            if (!p) { printf("alloc %llu failed at %d.%06d\n", (unsigned long long)size, m, i); return 1; }
            *(char *)p = 1;
            if (keep && (rnd() >> 11) < keep * (1ull << 53)) kept++; /* leak it */
            else { HeapFree(heap, 0, *slot); *slot = p; }
        }
        QueryPerformanceCounter(&t1);
        K32GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc));
        printf("M %3d ns/op %8.1f kept %zu commit_MB %zu t %.1f\n", m, (t1.QuadPart - t0.QuadPart) * 1e3 / f.QuadPart,
               kept, pmc.PagefileUsage >> 20, (double)(t1.QuadPart - tstart.QuadPart) / f.QuadPart);
        fflush(stdout);
    }
    show("end", heap);
    return 0;
}
