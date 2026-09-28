/* WofSetFileDataLocation(file, WOF_PROVIDER_FILE, XPRESS4K) and the raw FSCTL_SET_EXTERNAL_BACKING
 * on a file per path given (NTFS vs a filesystem without WOF). Edge's setup.exe calls it.
 * Build: x86_64-w64-mingw32-gcc -O2 -o wofset.exe wofset.c */
#include <windows.h>
#include <winioctl.h>
#include <stdio.h>
#ifndef FSCTL_SET_EXTERNAL_BACKING
#define FSCTL_SET_EXTERNAL_BACKING CTL_CODE(FILE_DEVICE_FILE_SYSTEM, 195, METHOD_BUFFERED, FILE_SPECIAL_ACCESS)
#endif
typedef HRESULT (WINAPI *pWofSetFileDataLocation)(HANDLE, ULONG, void *, ULONG);
int main(int argc, char **argv)
{
    pWofSetFileDataLocation pWof = (void *)GetProcAddress(LoadLibraryA("wofutil.dll"), "WofSetFileDataLocation");
    struct { ULONG Version, Provider; } ext = {1, 2};                /* WOF_EXTERNAL_INFO: WOF_CURRENT_VERSION, WOF_PROVIDER_FILE */
    struct { ULONG Version, Algorithm, Flags; } fpi = {1, 0, 0};     /* FILE_PROVIDER_EXTERNAL_INFO_V1, XPRESS4K */
    struct { ULONG Algorithm; } info = {0};                           /* WOF_FILE_COMPRESSION_INFO_V1 */
    BYTE buf[sizeof(ext) + sizeof(fpi)];
    int i;
    for (i = 1; i < argc; i++)
    {
        HANDLE h = CreateFileA(argv[i], GENERIC_READ | GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
        DWORD written, ret; HRESULT hr;
        char data[65536]; memset(data, 'a', sizeof(data));
        WriteFile(h, data, sizeof(data), &written, NULL);
        hr = pWof(h, 2, &info, sizeof(info));
        printf("%s: WofSetFileDataLocation hr %#lx\n", argv[i], hr);
        memcpy(buf, &ext, sizeof(ext)); memcpy(buf + sizeof(ext), &fpi, sizeof(fpi));
        SetLastError(0xdeadbeef);
        ret = DeviceIoControl(h, FSCTL_SET_EXTERNAL_BACKING, buf, sizeof(buf), NULL, 0, &written, NULL);
        printf("%s: FSCTL_SET_EXTERNAL_BACKING ret %lu err %lu\n", argv[i], ret, GetLastError());
        CloseHandle(h); DeleteFileA(argv[i]);
    }
    return 0;
}
