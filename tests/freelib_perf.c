/* FreeLibrary cost when the module stays loaded (081): loads every DLL in system32 that
 * loads (up to MAX), then times LoadLibrary and FreeLibrary of an already loaded DLL.
 * Usage: freelib_perf.exe [MAX] [ITERS]
 * Build: x86_64-w64-mingw32-gcc -O2 -o freelib_perf.exe freelib_perf.c */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

int main( int argc, char **argv )
{
    int max = argc > 1 ? atoi( argv[1] ) : 300, iters = argc > 2 ? atoi( argv[2] ) : 20000, n = 0, i;
    char path[MAX_PATH];
    WIN32_FIND_DATAA fd;
    LARGE_INTEGER f, t0, t1;
    HANDLE find;
    HMODULE mod;

    GetSystemDirectoryA( path, MAX_PATH );
    strcat( path, "\\*.dll" );
    find = FindFirstFileA( path, &fd );
    do
    {
        /* no DllMain side effects: map without resolving imports or calling DllMain */
        if (LoadLibraryExA( fd.cFileName, NULL, DONT_RESOLVE_DLL_REFERENCES )) n++;
    } while (n < max && FindNextFileA( find, &fd ));
    FindClose( find );

    mod = LoadLibraryA( "oleaut32.dll" );
    QueryPerformanceFrequency( &f );
    QueryPerformanceCounter( &t0 );
    for (i = 0; i < iters; i++) LoadLibraryA( "oleaut32.dll" );
    QueryPerformanceCounter( &t1 );
    printf( "%d extra modules: LoadLibrary of a loaded DLL %.0f ns\n", n,
            (double)(t1.QuadPart - t0.QuadPart) * 1e9 / f.QuadPart / iters );
    QueryPerformanceCounter( &t0 );
    for (i = 0; i < iters; i++) FreeLibrary( mod );
    QueryPerformanceCounter( &t1 );
    printf( "%d extra modules: FreeLibrary of a DLL that stays loaded %.0f ns\n", n,
            (double)(t1.QuadPart - t0.QuadPart) * 1e9 / f.QuadPart / iters );
    FreeLibrary( mod );
    return 0;
}
