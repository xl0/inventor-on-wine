/* Cost of the synchronization/mapping calls Inventor makes per placed occurrence (097):
 * MapViewOfFile+UnmapViewOfFile of a 128 KB view (utx.dll, ~40 per occurrence),
 * uncontended WaitForSingleObject+ReleaseMutex (AdpSDKCore/FwUI, ~100 per occurrence),
 * SetEvent, VirtualProtect of 8 KB (rse.dll, ~300 per occurrence).
 * Usage: mapview_perf.exe [ITERS]
 * Build: x86_64-w64-mingw32-gcc -O2 -o mapview_perf.exe mapview_perf.c */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

static LARGE_INTEGER freq;
static double now(void) { LARGE_INTEGER t; QueryPerformanceCounter(&t); return (double)t.QuadPart / freq.QuadPart; }

int main(int argc, char **argv)
{
    int iters = argc > 1 ? atoi(argv[1]) : 20000, i;
    HANDLE section, mutex, event;
    char *p, *mem;
    DWORD old;
    double t;

    QueryPerformanceFrequency(&freq);
    section = CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, 16 << 20, NULL);
    t = now();
    for (i = 0; i < iters; i++)
    {
        p = MapViewOfFile(section, FILE_MAP_WRITE, 0, (i % 64) * 0x40000, 0x20000);
        p[0] = 1;
        UnmapViewOfFile(p);
    }
    printf("map+write+unmap 128K view: %.2f us\n", (now() - t) * 1e6 / iters);

    mutex = CreateMutexA(NULL, FALSE, NULL);
    t = now();
    for (i = 0; i < iters; i++) { WaitForSingleObject(mutex, INFINITE); ReleaseMutex(mutex); }
    printf("wait+release mutex: %.2f us\n", (now() - t) * 1e6 / iters);

    event = CreateEventA(NULL, FALSE, FALSE, NULL);
    t = now();
    for (i = 0; i < iters; i++) SetEvent(event);
    printf("SetEvent: %.2f us\n", (now() - t) * 1e6 / iters);

    mem = VirtualAlloc(NULL, 1 << 20, MEM_COMMIT, PAGE_READWRITE);
    t = now();
    for (i = 0; i < iters; i++)
    {
        VirtualProtect(mem + (i % 64) * 0x2000, 0x2000, PAGE_READONLY, &old);
        VirtualProtect(mem + (i % 64) * 0x2000, 0x2000, PAGE_READWRITE, &old);
    }
    printf("VirtualProtect RO+RW 8K: %.2f us\n", (now() - t) * 1e6 / iters);
    return 0;
}
