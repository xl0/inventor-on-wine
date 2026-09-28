/* COM activation vs per-user class registrations (HKCU\Software\Classes).
 * Only writes HKCU. Run elevated and non-elevated to compare. COM caches lookups per process, so
 * argv[1] runs one check in a fresh process: override, partial, ifaceover, iface[pre][sta], ole32.
 * Build: x86_64-w64-mingw32-gcc -O2 -o com_peruser.exe com_peruser.c -lole32 -luuid -ladvapi32 */
#define _WIN32_WINNT 0x0600
#define COBJMACROS
#include <windows.h>
#include <stdio.h>

static const char clsid_a[] = "{5d1f1c2e-4a3b-4e7a-9b61-0c30a0300001}";  /* per-user only */
static const char filtergraph[] = "{e436ebb3-524f-11ce-9f53-0020af0ba770}";  /* HKLM (quartz) */
static const char iid_x[] = "{5d1f1c2e-4a3b-4e7a-9b61-0c30a0300002}";

static void mk(const char *path, const char *name, const char *val)
{
    HKEY k;
    char full[512];
    snprintf(full, sizeof(full), "Software\\Classes\\%s", path);
    RegCreateKeyExA(HKEY_CURRENT_USER, full, 0, NULL, 0, KEY_ALL_ACCESS, NULL, &k, NULL);
    if (val) RegSetValueExA(k, name, 0, REG_SZ, (const BYTE *)val, strlen(val) + 1);
    RegCloseKey(k);
}

static void del(const char *path)
{
    char full[512];
    snprintf(full, sizeof(full), "Software\\Classes\\%s", path);
    RegDeleteTreeA(HKEY_CURRENT_USER, full);
    RegDeleteKeyA(HKEY_CURRENT_USER, full);
}

static CLSID guid(const char *s)
{
    WCHAR w[64];
    CLSID c;
    MultiByteToWideChar(CP_ACP, 0, s, -1, w, 64);
    CLSIDFromString(w, &c);
    return c;
}

static HRESULT create(const char *s)
{
    CLSID c = guid(s);
    IUnknown *unk = NULL;
    HRESULT hr = CoCreateInstance(&c, NULL, CLSCTX_INPROC_SERVER, &IID_IUnknown, (void **)&unk);
    if (unk) IUnknown_Release(unk);
    return hr;
}

static int mode(const char *m)
{
    char path[256];
    HRESULT hr;
    CLSID c, out;

    CoInitializeEx(NULL, strstr(m, "sta") ? COINIT_APARTMENTTHREADED : COINIT_MULTITHREADED);
    snprintf(path, sizeof(path), "CLSID\\%s", filtergraph);
    if (!strcmp(m, "override"))
    {
        snprintf(path, sizeof(path), "CLSID\\%s\\InprocServer32", filtergraph);
        mk(path, NULL, "C:\\nonexistent\\nothere.dll");
        printf("fresh: FilterGraph + HKCU InprocServer32 = missing dll: %#lx\n", create(filtergraph));
    }
    else if (!strcmp(m, "partial"))
    {
        mk(path, NULL, "stale per-user copy");
        printf("fresh: FilterGraph + HKCU key without InprocServer32: %#lx\n", create(filtergraph));
    }
    else if (!strcmp(m, "ifaceover"))
    {
        mk("Interface\\{000214E6-0000-0000-C000-000000000046}\\ProxyStubClsid32", NULL, clsid_a);
        c = guid("{000214E6-0000-0000-C000-000000000046}");
        hr = CoGetPSClsid(&c, &out);
        printf("fresh: IShellFolder with HKCU ProxyStubClsid32: %#lx %08lx\n", hr, out.Data1);
        strcpy(path, "Interface\\{000214E6-0000-0000-C000-000000000046}");
    }
    else if (!strncmp(m, "iface", 5))
    {
        if (strstr(m, "pre"))
        {
            c = guid(iid_x);
            printf("pre-call: %#lx\n", CoGetPSClsid(&c, &out));
        }
        snprintf(path, sizeof(path), "Interface\\%s\\ProxyStubClsid32", iid_x);
        mk(path, NULL, "{00020424-0000-0000-C000-000000000046}");
        c = guid(iid_x);
        hr = CoGetPSClsid(&c, &out);
        printf("per-user Interface -> PSOAInterface: %#lx\n", hr);
        del(path);
        c = guid("{000214E6-0000-0000-C000-000000000046}");
        hr = CoGetPSClsid(&c, &out);
        printf("IShellFolder baseline: %#lx %08lx\n", hr, out.Data1);
        mk("Interface\\{000214E6-0000-0000-C000-000000000046}\\ProxyStubClsid32", NULL, clsid_a);
        c = guid("{000214E6-0000-0000-C000-000000000046}");
        hr = CoGetPSClsid(&c, &out);
        printf("IShellFolder with HKCU ProxyStubClsid32: %#lx %s\n", hr, (out.Data1 == 0x5d1f1c2e ? "HKCU" : "HKLM"));
        del("Interface\\{000214E6-0000-0000-C000-000000000046}");
        mk("Interface\\{000214E6-0000-0000-C000-000000000046}\\ProxyStubClsid32", NULL, "{00000320-0000-0000-C000-000000000046}");
        c = guid("{000214E6-0000-0000-C000-000000000046}");
        hr = CoGetPSClsid(&c, &out);
        printf("IShellFolder with HKCU ProxyStubClsid32 = PSFactoryBuffer: %#lx %08lx\n", hr, out.Data1);
        del("Interface\\{000214E6-0000-0000-C000-000000000046}");
        mk("Interface\\{000214E6-0000-0000-C000-000000000046}", NULL, "IPersistStream");
        c = guid("{000214E6-0000-0000-C000-000000000046}");
        hr = CoGetPSClsid(&c, &out);
        printf("IShellFolder with HKCU key, no ProxyStubClsid32: %#lx %08lx\n", hr, out.Data1);
        del("Interface\\{000214E6-0000-0000-C000-000000000046}");
        snprintf(path, sizeof(path), "Interface\\%s", iid_x);
    }
    else if (!strcmp(m, "ole32"))
    {
        WCHAR *name = NULL;
        snprintf(path, sizeof(path), "CLSID\\%s", clsid_a);
        mk(path, NULL, "PerUserName");
        c = guid(clsid_a);
        hr = OleRegGetUserType(&c, USERCLASSTYPE_FULL, &name);
        printf("ole32 OleRegGetUserType per-user CLSID: %#lx %ls\n", hr, hr ? L"" : name);
        mk("CLSID\\{e436ebb3-524f-11ce-9f53-0020af0ba770}", NULL, "PerUserFilterGraphName");
        c = guid(filtergraph);
        hr = OleRegGetUserType(&c, USERCLASSTYPE_FULL, &name);
        printf("ole32 OleRegGetUserType FilterGraph with HKCU name: %#lx %ls\n", hr, hr ? L"" : name);
        del("CLSID\\{e436ebb3-524f-11ce-9f53-0020af0ba770}");
    }
    del(path);
    CoUninitialize();
    return 0;
}

int main(int argc, char **argv)
{
    TOKEN_ELEVATION_TYPE type = 0;
    TOKEN_ELEVATION elev = {0};
    HANDLE token;
    DWORD len;
    HKEY key, root, empty;
    LONG res;
    CLSID c, out;
    HRESULT hr;
    char path[256];

    if (argc > 1) return mode(argv[1]);
    OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token);
    GetTokenInformation(token, TokenElevationType, &type, sizeof(type), &len);
    GetTokenInformation(token, TokenElevation, &elev, sizeof(elev), &len);
    printf("elevation type %d elevated %lu\n", type, elev.TokenIsElevated);

    CoInitializeEx(NULL, COINIT_MULTITHREADED);

    snprintf(path, sizeof(path), "CLSID\\%s\\InprocServer32", clsid_a);
    mk(path, NULL, "C:\\Windows\\System32\\quartz.dll");
    mk(path, "ThreadingModel", "Both");
    printf("per-user CLSID (inproc = quartz, unknown class): %#lx (0x80040111 = found, 0x80040154 = not)\n", create(clsid_a));
    res = RegOpenKeyExA(HKEY_CLASSES_ROOT, path, 0, KEY_READ, &key);
    printf("  RegOpenKeyEx HKCR: %ld\n", res);
    if (!res) RegCloseKey(key);

    mk("Wine.PerUser.Test\\CLSID", NULL, clsid_a);
    hr = CLSIDFromProgID(L"Wine.PerUser.Test", &c);
    printf("per-user ProgID: %#lx\n", hr);

    snprintf(path, sizeof(path), "Interface\\%s\\ProxyStubClsid32", iid_x);
    mk(path, NULL, clsid_a);
    c = guid(iid_x);
    hr = CoGetPSClsid(&c, &out);
    printf("per-user Interface CoGetPSClsid: %#lx\n", hr);

    printf("FilterGraph plain: %#lx\n", create(filtergraph));
    snprintf(path, sizeof(path), "CLSID\\%s", filtergraph);
    mk(path, NULL, "stale per-user copy");
    printf("FilterGraph + HKCU key without InprocServer32: %#lx\n", create(filtergraph));
    snprintf(path, sizeof(path), "CLSID\\%s\\InprocServer32", filtergraph);
    mk(path, NULL, "C:\\nonexistent\\nothere.dll");
    printf("FilterGraph + HKCU InprocServer32 = missing dll: %#lx (0x8007007e = HKCU used)\n", create(filtergraph));

    res = RegOpenUserClassesRoot(token, 0, KEY_READ, &root);
    printf("RegOpenUserClassesRoot: %ld handle %p tag %d\n", res, root, (int)((ULONG_PTR)root & 3));
    if (!res)
    {
        snprintf(path, sizeof(path), "CLSID\\%s", clsid_a);
        res = RegOpenKeyExA(root, path, 0, KEY_READ, &key);
        printf("  open per-user CLSID: %ld\n", res);
        if (!res) RegCloseKey(key);
        res = RegOpenKeyExA(root, "CLSID\\{e436ebb3-524f-11ce-9f53-0020af0ba770}\\InprocServer32", 0, KEY_READ, &key);
        printf("  open FilterGraph InprocServer32: %ld\n", res);
        if (!res)
        {
            char buf[MAX_PATH]; DWORD sz = sizeof(buf);
            res = RegQueryValueExA(key, NULL, NULL, NULL, (BYTE *)buf, &sz);
            printf("    default: %ld %s\n", res, res ? "" : buf);
            RegCloseKey(key);
        }
        res = RegOpenKeyExA(root, "exefile", 0, KEY_READ, &key);
        printf("  open machine exefile: %ld\n", res);
        if (!res) RegCloseKey(key);
        res = RegOpenKeyExA(HKEY_CLASSES_ROOT, "exefile", 0, KEY_READ, &key);
        printf("  (via HKCR: %ld)\n", res);
        if (!res) RegCloseKey(key);
    }

    /* HKCR redirected to an empty key */
    RegCreateKeyExA(HKEY_CURRENT_USER, "Software\\WineComPerUserEmpty", 0, NULL, 0, KEY_ALL_ACCESS, NULL, &empty, NULL);
    res = RegOverridePredefKey(HKEY_CLASSES_ROOT, empty);
    printf("override HKCR: %ld\n", res);
    printf("  per-user CLSID: %#lx\n", create(clsid_a));
    printf("  FilterGraph (HKCU missing dll): %#lx\n", create(filtergraph));
    snprintf(path, sizeof(path), "CLSID\\%s", filtergraph);
    del(path);
    printf("  FilterGraph (no HKCU): %#lx\n", create(filtergraph));
    res = RegOpenUserClassesRoot(token, 0, KEY_READ, &key);
    printf("  RegOpenUserClassesRoot: %ld tag %d\n", res, (int)((ULONG_PTR)key & 3));
    if (!res)
    {
        HKEY sub;
        res = RegOpenKeyExA(key, "exefile", 0, KEY_READ, &sub);
        printf("    open machine exefile: %ld\n", res);
        if (!res) RegCloseKey(sub);
        res = RegOpenKeyExA(key, "CLSID\\{e436ebb3-524f-11ce-9f53-0020af0ba770}\\InprocServer32", 0, KEY_READ, &sub);
        printf("    open machine-only FilterGraph InprocServer32: %ld\n", res);
        if (!res) RegCloseKey(sub);
    }
    RegOverridePredefKey(HKEY_CLASSES_ROOT, NULL);
    RegCloseKey(empty);
    RegDeleteKeyA(HKEY_CURRENT_USER, "Software\\WineComPerUserEmpty");

    snprintf(path, sizeof(path), "CLSID\\%s", clsid_a);
    del(path);
    snprintf(path, sizeof(path), "Interface\\%s", iid_x);
    del(path);
    del("Wine.PerUser.Test");
    CoUninitialize();
    return 0;
}
