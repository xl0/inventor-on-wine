/* bigmap_perf.c [DLL]: costs that every Chromium process start pays (085):
 *  - mapping a big DLL whose sections are not page-aligned in the file (msedge.dll: 330 MB, FileAlignment 0x200)
 *    as an image, first and second time (LoadLibraryEx DONT_RESOLVE_DLL_REFERENCES), and touching its pages;
 *  - a 1 TB placeholder reservation (VirtualAlloc2, like V8's sandbox), splitting off and committing
 *    a part, releasing it back into a placeholder, freeing the whole reservation.
 * Prints ms per step. `bigmap_perf DLL hold` keeps DLL mapped for 15 s (run it first in another process).
 * Build: x86_64-w64-mingw32-gcc -O2 -o bigmap_perf.exe bigmap_perf.c (VirtualAlloc2 via GetProcAddress) */
#include <windows.h>
#include <stdio.h>

#ifndef MEM_RESERVE_PLACEHOLDER
#define MEM_RESERVE_PLACEHOLDER 0x40000
#define MEM_REPLACE_PLACEHOLDER 0x4000
#define MEM_PRESERVE_PLACEHOLDER 0x2
#endif

typedef PVOID (WINAPI *va2_t)(HANDLE, PVOID, SIZE_T, ULONG, ULONG, void *, ULONG);
static LARGE_INTEGER f;
static double now(void) { LARGE_INTEGER c; QueryPerformanceCounter(&c); return c.QuadPart * 1000.0 / f.QuadPart; }

int main(int argc, char **argv)
{
    const char *dll = argc > 1 ? argv[1] : "C:\\Program Files (x86)\\Microsoft\\EdgeWebView\\Application\\154.0.4258.37\\msedge.dll";
    va2_t pVirtualAlloc2 = (va2_t)GetProcAddress(GetModuleHandleA("kernelbase.dll"), "VirtualAlloc2");
    double t;
    HMODULE m;
    int i;

    QueryPerformanceFrequency(&f);
    if (argc > 2 && !strcmp(argv[2], "hold"))  /* keep the DLL mapped for a while (another process's view) */
    {
        t = now();
        m = LoadLibraryExA(dll, NULL, DONT_RESOLVE_DLL_REFERENCES);
        printf("hold: map %.1f ms (%p)\n", now() - t, m);
        Sleep(15000);
        return 0;
    }
    for (i = 0; i < 2; i++)
    {
        t = now();
        m = LoadLibraryExA(dll, NULL, DONT_RESOLVE_DLL_REFERENCES);
        printf("map image %d: %.1f ms (%p)\n", i, now() - t, m);
        if (!m) return 1;
        if (!i)
        {
            /* touch every page of the image (as executing/reading it would) */
            IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)((char *)m + ((IMAGE_DOS_HEADER *)m)->e_lfanew);
            volatile char sum = 0; SIZE_T off;
            t = now();
            for (off = 0; off < nt->OptionalHeader.SizeOfImage; off += 4096) sum += ((char *)m)[off];
            printf("touch image: %.1f ms\n", now() - t);
        }
        t = now();
        FreeLibrary(m);
        printf("unmap image %d: %.1f ms\n", i, now() - t);
    }

    if (pVirtualAlloc2)
    {
        SIZE_T big = (SIZE_T)1 << 40, part = (SIZE_T)1 << 32;
        char *p;
        t = now();
        p = pVirtualAlloc2(NULL, NULL, big, MEM_RESERVE | MEM_RESERVE_PLACEHOLDER, PAGE_NOACCESS, NULL, 0);
        printf("reserve 1TB placeholder: %.1f ms (%p)\n", now() - t, p);
        if (!p) return 1;
        t = now();
        i = VirtualFree(p, part, MEM_RELEASE | MEM_PRESERVE_PLACEHOLDER);
        printf("split 4GB: %.1f ms (%d)\n", now() - t, i);
        t = now();
        i = !!pVirtualAlloc2(NULL, p, part, MEM_RESERVE | MEM_REPLACE_PLACEHOLDER, PAGE_NOACCESS, NULL, 0);
        printf("reserve over 4GB placeholder: %.1f ms (%d)\n", now() - t, i);
        t = now();
        i = !!VirtualAlloc(p, 1 << 20, MEM_COMMIT, PAGE_READWRITE);
        printf("commit 1MB: %.1f ms (%d)\n", now() - t, i);
        t = now();
        i = VirtualFree(p, part, MEM_RELEASE | MEM_PRESERVE_PLACEHOLDER);
        printf("back to placeholder 4GB: %.1f ms (%d)\n", now() - t, i);
        t = now();
        i = VirtualFree(p, 0, MEM_RELEASE) && VirtualFree(p + part, 0, MEM_RELEASE);
        printf("free all: %.1f ms (%d)\n", now() - t, i);
        t = now();
        p = VirtualAlloc(NULL, big, MEM_RESERVE, PAGE_NOACCESS);
        printf("plain reserve 1TB: %.1f ms (%p)\n", now() - t, p);
        t = now();
        i = VirtualFree(p, 0, MEM_RELEASE);
        printf("plain free 1TB: %.1f ms (%d)\n", now() - t, i);
    }
    return 0;
}
