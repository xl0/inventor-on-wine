/* WIC component enumeration (098): cost of CreateComponentEnumerator(WICMetadataReader) and
 * whether a metadata reader registered in HKLM after the first enumeration shows up in later
 * enumerations of the same process (cache) or only in a new process.
 * wic_enum.exe            run the test (spawns "wic_enum.exe count" children)
 * wic_enum.exe count      print the number of enumerated metadata readers
 * x86_64-w64-mingw32-gcc -O2 -o wic_enum.exe wic_enum.c -lole32 -lwindowscodecs -luuid -ladvapi32 */
#define COBJMACROS
#define _WIN32_WINNT 0x0600
#define INITGUID
#include <windows.h>
#include <wincodec.h>
#include <wincodecsdk.h>
#include <stdio.h>
DEFINE_GUID(IID_IWICComponentFactory_, 0x412d0c3a,0x9650,0x44fa,0xaf,0x5b,0xdd,0x2a,0x06,0xc8,0xe8,0xfb);

static const WCHAR fake[] = L"{6a2e0d8f-0e2b-4c7f-9a51-0980a1b2c3d4}";
static const WCHAR inst[] = L"Software\\Classes\\CLSID\\{05AF94D8-7174-4CD2-BE4A-4124B80EE4B8}\\Instance\\";

static IWICComponentFactory *factory;

/* RegCopyTree fails on Windows' protected keys: copy what is readable */
static void copy_tree(HKEY src, HKEY dst)
{
    WCHAR name[256]; BYTE data[4096]; DWORD i, len, size, type; HKEY s, d;
    for (i = 0; len = 256, size = sizeof(data), !RegEnumValueW(src, i, name, &len, NULL, &type, data, &size); i++)
        RegSetValueExW(dst, name, 0, type, data, size);
    for (i = 0; len = 256, !RegEnumKeyExW(src, i, name, &len, NULL, NULL, NULL, NULL); i++)
    {
        if (RegOpenKeyExW(src, name, 0, KEY_READ, &s)) continue;
        if (!RegCreateKeyExW(dst, name, 0, NULL, 0, KEY_ALL_ACCESS, NULL, &d, NULL)) { copy_tree(s, d); RegCloseKey(d); }
        RegCloseKey(s);
    }
}

static int count_flags(DWORD flags)
{
    IEnumUnknown *e;
    IUnknown *u;
    int n = 0;
    if (FAILED(IWICComponentFactory_CreateComponentEnumerator(factory, WICMetadataReader, flags, &e))) return -1;
    while (IEnumUnknown_Next(e, 1, &u, NULL) == S_OK) { n++; IUnknown_Release(u); }
    IEnumUnknown_Release(e);
    return n;
}

static int count(void) { return count_flags(WICComponentEnumerateUnsigned); }

static int child_count(void)
{
    char cmd[MAX_PATH + 16];
    STARTUPINFOA si = {sizeof(si)};
    PROCESS_INFORMATION pi;
    DWORD code = 0;
    GetModuleFileNameA(NULL, cmd, MAX_PATH);
    strcat(cmd, " count");
    if (!CreateProcessA(NULL, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) return -2;
    WaitForSingleObject(pi.hProcess, INFINITE);
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
    return code;
}

int main(int argc, char **argv)
{
    WCHAR src[128], dst[128], key[256];
    LARGE_INTEGER f, t0, t1;
    HKEY hsrc, hdst, hinst;
    LSTATUS r;
    int i, n0, n1, n2, n3;

    CoInitialize(NULL);
    if (FAILED(CoCreateInstance(&CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER, &IID_IWICComponentFactory_, (void **)&factory)))
    { printf("no factory\n"); return 1; }
    if (argc > 1) return count();

    n0 = count();
    QueryPerformanceFrequency(&f); QueryPerformanceCounter(&t0);
    for (i = 0; i < 1000; i++) count();
    QueryPerformanceCounter(&t1);
    printf("%d metadata readers, %.1f us per enumeration\n", n0, (t1.QuadPart - t0.QuadPart) * 1e6 / f.QuadPart / 1000);

    /* fake reader: copy of the first enumerated reader's CLSID key under a new CLSID, in HKLM */
    {
        IEnumUnknown *e; IUnknown *u; IWICComponentInfo *info; CLSID clsid;
        IWICComponentFactory_CreateComponentEnumerator(factory, WICMetadataReader, 0, &e);
        IEnumUnknown_Next(e, 1, &u, NULL);
        IUnknown_QueryInterface(u, &IID_IWICComponentInfo, (void **)&info);
        IWICComponentInfo_GetCLSID(info, &clsid);
        StringFromGUID2(&clsid, key, 39);
        IWICComponentInfo_Release(info); IUnknown_Release(u); IEnumUnknown_Release(e);
    }
    swprintf(src, 128, L"CLSID\\%ls", key);
    swprintf(dst, 128, L"Software\\Classes\\CLSID\\%ls", fake);
    r = RegOpenKeyExW(HKEY_CLASSES_ROOT, src, 0, KEY_READ, &hsrc);
    printf("open %ls: %ld\n", src, r);
    if (!r) r = RegCreateKeyExW(HKEY_LOCAL_MACHINE, dst, 0, NULL, 0, KEY_ALL_ACCESS, NULL, &hdst, NULL);
    if (!r)
    {
        HKEY ips;
        static const WCHAR dll[] = L"C:\\Windows\\System32\\WindowsCodecs.dll", both[] = L"Both";
        copy_tree(hsrc, hdst);
        if (!RegCreateKeyExW(hdst, L"InprocServer32", 0, NULL, 0, KEY_ALL_ACCESS, NULL, &ips, NULL))
        {
            RegSetValueExW(ips, NULL, 0, REG_SZ, (const BYTE *)dll, sizeof(dll));
            RegSetValueExW(ips, L"ThreadingModel", 0, REG_SZ, (const BYTE *)both, sizeof(both));
            RegCloseKey(ips);
        }
    }
    printf("copy: %ld\n", r);
    if (!r) { swprintf(key, 256, L"%ls%ls", inst, fake); r = RegCreateKeyExW(HKEY_LOCAL_MACHINE, key, 0, NULL, 0, KEY_ALL_ACCESS, NULL, &hinst, NULL); }
    if (!r) r = RegSetValueExW(hinst, L"CLSID", 0, REG_SZ, (const BYTE *)fake, sizeof(fake));
    if (!r) r = RegSetValueExW(hinst, L"FriendlyName", 0, REG_SZ, (const BYTE *)L"wic_enum fake", sizeof(L"wic_enum fake"));
    printf("register fake reader: %ld\n", r);

    n1 = count();
    n2 = child_count();
    printf("unsigned: before %d, after in this process %d, in a new process %d\n", n0, n1, n2);
    printf("refresh: %d, then without refresh %d, default flags %d\n", count_flags(WICComponentEnumerateUnsigned | WICComponentEnumerateRefresh),
           count(), count_flags(0));
    RegDeleteTreeW(HKEY_LOCAL_MACHINE, dst);
    RegDeleteTreeW(HKEY_LOCAL_MACHINE, key);
    n3 = count();
    printf("after removal %d, refresh %d, then %d\n", n3, count_flags(WICComponentEnumerateUnsigned | WICComponentEnumerateRefresh), count());
    return 0;
}
