/* Per-file cost of a memory-mapped file read as Autodesk's xirang does it (098):
 * CreateFile, CreateFileMapping(0), MapViewOfFileEx, CloseHandle(mapping), DuplicateHandle(file),
 * touch the view, UnmapViewOfFile, CloseHandle x2.
 * mapview_perf.exe [N] [THREADS] [VIEWS]: N iterations over one small file; THREADS idle threads and
 * VIEWS other mapped views (of a pagefile section) exist meanwhile.
 * x86_64-w64-mingw32-gcc -O2 -o mapview_perf.exe mapview_perf.c */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

static DWORD WINAPI idle(void *arg) { Sleep(INFINITE); return 0; }

int main(int argc, char **argv)
{
    int n = argc > 1 ? atoi(argv[1]) : 2000, threads = argc > 2 ? atoi(argv[2]) : 0, i;
    int views = argc > 3 ? atoi(argv[3]) : 0;
    HANDLE sec = CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, 65536, NULL);
    char path[MAX_PATH], buf[4096] = {1};
    LARGE_INTEGER f, t0, t1, t2;
    HANDLE file;
    DWORD w;
    volatile char sum = 0;

    for (i = 0; i < views; i++) MapViewOfFile(sec, FILE_MAP_READ, 0, 0, 4096);
    for (i = 0; i < threads; i++) CloseHandle(CreateThread(NULL, 0, idle, NULL, 0, NULL));
    GetTempPathA(MAX_PATH, path);
    strcat(path, "mapview_perf.bin");
    file = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    for (i = 0; i < 16; i++) WriteFile(file, buf, sizeof(buf), &w, NULL);
    CloseHandle(file);

    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&t0);
    for (i = 0; i < n; i++)
    {
        HANDLE file = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL), map, dup;
        char *p;
        map = CreateFileMappingA(file, NULL, PAGE_READONLY, 0, 0, NULL);
        p = MapViewOfFileEx(map, FILE_MAP_READ, 0, 0, 0, NULL);
        CloseHandle(map);
        DuplicateHandle(GetCurrentProcess(), file, GetCurrentProcess(), &dup, 0, FALSE, DUPLICATE_SAME_ACCESS);
        sum += p[0] + p[40000];
        UnmapViewOfFile(p);
        CloseHandle(dup);
        CloseHandle(file);
    }
    QueryPerformanceCounter(&t1);
    for (i = 0; i < n; i++)
    {
        HANDLE file = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
        CloseHandle(file);
    }
    QueryPerformanceCounter(&t2);
    printf("%d iterations, %d threads, %d views: map cycle %.1f us (open+close alone %.1f us)\n", n, threads, views,
           (t1.QuadPart - t0.QuadPart) * 1e6 / f.QuadPart / n, (t2.QuadPart - t1.QuadPart) * 1e6 / f.QuadPart / n);
    DeleteFileA(path);
    return 0;
}
