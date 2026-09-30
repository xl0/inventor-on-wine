/* Loader stress for issue 092: threads load/free DLLs (plain, with an import, with a forwarder) while
 * others look them up (GetModuleHandle[Ex], GetProcAddress, RtlPcToFileHeader) and call into them.
 * Refcount bugs show up as crashes (code unmapped under a caller) or modules left loaded at the end.
 * Usage: stress.exe [seconds] [threads] [keep]. keep: the main thread holds every DLL loaded (lookups
 * only, no unloads). DLLs next to it (B as in dm.c):
 * for i in 0 1 2 3; do $B -O2 -shared -o s$i.dll dm.c; done
 * $B -O2 -shared -DTOP -o simp.dll dm.c s0.dll (imports s0); $B -O2 -shared -o sfwd.dll dm.c sfwd.def
 * $B -O2 -shared -DNESTED -o snest.dll dm.c (loads s2 and sfwd from its DllMain)
 * x86_64-w64-mingw32-gcc -O2 -o stress.exe stress.c */
#include <windows.h>
#include <stdio.h>

static const char *names[] = { "s0.dll", "s1.dll", "s2.dll", "s3.dll", "simp.dll", "sfwd.dll", "snest.dll" };
static const char *funcs[] = { "dm_func", "dm_func", "dm_func", "dm_func", "dm_top_func", "fwd", "dm_func" };
static const int values[] = { 42, 42, 42, 42, 43, 42, 42 };
static volatile LONG stop, failures, ops;
static PVOID (WINAPI *pRtlPcToFileHeader)(PVOID, PVOID *);

#define FAIL(...) do { if (InterlockedIncrement(&failures) < 20) printf(__VA_ARGS__); } while (0)

static void check_call(HMODULE h, int i)
{
    int (*f)(void) = (void *)GetProcAddress(h, funcs[i]);
    void *base;
    if (!f) { FAIL("GetProcAddress(%s %p) failed %lu\n", names[i], h, GetLastError()); return; }
    if (f() != values[i]) FAIL("bad value from %s\n", names[i]);
    if (i != 5 && pRtlPcToFileHeader(f, &base) != h) FAIL("RtlPcToFileHeader mismatch %s\n", names[i]);
}

static DWORD WINAPI worker(void *arg)
{
    unsigned int seed = (unsigned int)(ULONG_PTR)arg * 7919 + GetTickCount();
    while (!stop)
    {
        int i = (seed = seed * 1103515245 + 12345) >> 16;
        int op = (i >> 8) % 4;
        HMODULE h, h2;
        i %= ARRAYSIZE(names);
        switch (op)
        {
        case 0: case 1:  /* load, use, free */
            if (!(h = LoadLibraryA(names[i]))) { FAIL("load %s failed %lu\n", names[i], GetLastError()); break; }
            check_call(h, i);
            FreeLibrary(h);
            break;
        case 2:  /* lookup + addref */
            if (GetModuleHandleExA(0, names[i], &h2)) { check_call(h2, i); FreeLibrary(h2); }
            break;
        case 3:  /* plain lookup, only compare */
            h = GetModuleHandleA(names[i]);
            (void)h;
            break;
        }
        InterlockedIncrement(&ops);
    }
    return 0;
}

int main(int argc, char **argv)
{
    int secs = argc > 1 ? atoi(argv[1]) : 20, n = argc > 2 ? atoi(argv[2]) : 8, i;
    HANDLE t[64], keep[ARRAYSIZE(names)];
    setvbuf(stdout, NULL, _IONBF, 0);
    pRtlPcToFileHeader = (void *)GetProcAddress(GetModuleHandleA("ntdll.dll"), "RtlPcToFileHeader");
    if (argc > 3) for (i = 0; i < ARRAYSIZE(names); i++) keep[i] = LoadLibraryA(names[i]);
    for (i = 0; i < n; i++) t[i] = CreateThread(NULL, 0, worker, (void *)(ULONG_PTR)i, 0, NULL);
    Sleep(secs * 1000);
    stop = 1;
    WaitForMultipleObjects(n, t, TRUE, INFINITE);
    if (argc > 3) for (i = 0; i < ARRAYSIZE(names); i++) FreeLibrary(keep[i]);
    for (i = 0; i < ARRAYSIZE(names); i++)
        if (GetModuleHandleA(names[i])) FAIL("%s still loaded\n", names[i]);
    printf("%ld ops, %ld failures\n", ops, failures);
    return failures != 0;
}
