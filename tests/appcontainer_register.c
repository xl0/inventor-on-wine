/* kernelbase AppContainerRegisterSid/UnregisterSid/LookupMoniker/FreeMemory ground truth (090).
 * Chromium's sandbox (app_container_base.cc) binds all four and CHECKs they exist.
 * x86_64-w64-mingw32-gcc -O2 -o appcontainer_register.exe appcontainer_register.c -ladvapi32 -luserenv */
#include <windows.h>
#include <sddl.h>
#include <stdio.h>

static HRESULT (WINAPI *pRegister)(PSID, const WCHAR *, const WCHAR *);
static HRESULT (WINAPI *pUnregister)(PSID);
static HRESULT (WINAPI *pLookup)(PSID, WCHAR **);
static void (WINAPI *pFree)(void *);
static HRESULT (WINAPI *pDerive)(const WCHAR *, PSID *);
static HRESULT (WINAPI *pDeleteProfile)(const WCHAR *);

#define MAPPINGS L"Software\\Classes\\Local Settings\\Software\\Microsoft\\Windows\\CurrentVersion\\AppContainer\\Mappings"
#define STORAGE L"Software\\Classes\\Local Settings\\Software\\Microsoft\\Windows\\CurrentVersion\\AppContainer\\Storage"

static void dump_key(HKEY root, const WCHAR *base, const WCHAR *sidstr)
{
    WCHAR path[512], name[256]; BYTE data[1024]; DWORD i, nlen, dlen, type; HKEY key; LSTATUS r;
    swprintf(path, 512, L"%ls\\%ls", base, sidstr);
    if ((r = RegOpenKeyExW(root, path, 0, KEY_READ, &key))) { printf("  %ls: no key (%ld)\n", base + 90, r); return; }
    printf("  %ls\\SID:\n", base + 90);
    for (i = 0; nlen = 256, dlen = sizeof(data), !RegEnumValueW(key, i, name, &nlen, NULL, &type, data, &dlen); i++)
    {
        if (type == REG_SZ) printf("    value '%ls' SZ '%ls'\n", name, (WCHAR *)data);
        else printf("    value '%ls' type %lu len %lu\n", name, type, dlen);
    }
    for (i = 0; nlen = 256, !RegEnumKeyExW(key, i, name, &nlen, NULL, NULL, NULL, NULL); i++) printf("    subkey '%ls'\n", name);
    RegCloseKey(key);
}

static void state(const char *what, PSID sid, const WCHAR *sidstr)
{
    WCHAR *m = (WCHAR *)0xdeadbeef; HRESULT hr = pLookup(sid, &m);
    printf("%s: lookup hr %08lx moniker %ls\n", what, hr, SUCCEEDED(hr) ? m : L"-");
    if (SUCCEEDED(hr)) { printf("  heap %d\n", HeapValidate(GetProcessHeap(), 0, m)); pFree(m); }
    else printf("  out %p\n", m);
    dump_key(HKEY_CURRENT_USER, MAPPINGS, sidstr);
    dump_key(HKEY_CURRENT_USER, STORAGE, sidstr);
}

int main(void)
{
    HMODULE kb = GetModuleHandleA("kernelbase.dll"), ue = LoadLibraryA("userenv.dll");
    WCHAR name[64], *sidstr; PSID sid, other; HRESULT hr; SID_IDENTIFIER_AUTHORITY nt = {SECURITY_NT_AUTHORITY};
    PSID user_sid;

    pRegister = (void *)GetProcAddress(kb, "AppContainerRegisterSid");
    pUnregister = (void *)GetProcAddress(kb, "AppContainerUnregisterSid");
    pLookup = (void *)GetProcAddress(kb, "AppContainerLookupMoniker");
    pFree = (void *)GetProcAddress(kb, "AppContainerFreeMemory");
    pDerive = (void *)GetProcAddress(ue, "DeriveAppContainerSidFromAppContainerName");
    pDeleteProfile = (void *)GetProcAddress(ue, "DeleteAppContainerProfile");
    printf("kernelbase %p %p %p %p kernel32 %p %p %p %p\n", pRegister, pUnregister, pLookup, pFree,
           GetProcAddress(GetModuleHandleA("kernel32.dll"), "AppContainerRegisterSid"),
           GetProcAddress(GetModuleHandleA("kernel32.dll"), "AppContainerUnregisterSid"),
           GetProcAddress(GetModuleHandleA("kernel32.dll"), "AppContainerLookupMoniker"),
           GetProcAddress(GetModuleHandleA("kernel32.dll"), "AppContainerFreeMemory"));
    if (!pRegister || !pUnregister || !pLookup || !pFree) return 1;

    swprintf(name, 64, L"wine.test.%lu", GetTickCount());
    pDerive(name, &sid);
    ConvertSidToStringSidW(sid, &sidstr);
    printf("name %ls sid %ls\n", name, sidstr);
    state("before", sid, sidstr);

    hr = pRegister(sid, name, L"Display Name");
    printf("register hr %08lx\n", hr);
    state("after register", sid, sidstr);
    hr = pRegister(sid, name, L"Display Name");
    printf("register again hr %08lx\n", hr);
    hr = pRegister(sid, L"other.moniker", L"x");
    printf("register again other moniker hr %08lx\n", hr);
    state("after register again", sid, sidstr);

    hr = pUnregister(sid);
    printf("unregister hr %08lx\n", hr);
    state("after unregister", sid, sidstr);
    hr = pUnregister(sid);
    printf("unregister again hr %08lx\n", hr);

    /* SID not derived from the moniker */
    pDerive(L"wine.test.other", &other);
    hr = pRegister(other, name, name);
    printf("register mismatched sid/moniker hr %08lx\n", hr);
    if (SUCCEEDED(hr)) { state("mismatched", other, L"-"); printf("unregister %08lx\n", pUnregister(other)); }

    /* non-appcontainer SIDs */
    AllocateAndInitializeSid(&nt, 1, SECURITY_LOCAL_SYSTEM_RID, 0, 0, 0, 0, 0, 0, 0, &user_sid);
    printf("register S-1-5-18 hr %08lx\n", hr = pRegister(user_sid, name, name));
    if (SUCCEEDED(hr)) pUnregister(user_sid);
    printf("lookup S-1-5-18 hr %08lx\n", pLookup(user_sid, &sidstr));
    printf("unregister S-1-5-18 hr %08lx\n", pUnregister(user_sid));
    printf("register NULL moniker hr %08lx\n", hr = pRegister(sid, NULL, name));
    if (SUCCEEDED(hr)) pUnregister(sid);
    printf("register empty moniker hr %08lx\n", hr = pRegister(sid, L"", name));
    if (SUCCEEDED(hr)) pUnregister(sid);
    printf("register NULL display hr %08lx\n", hr = pRegister(sid, name, NULL));
    if (SUCCEEDED(hr)) { state("null display", sid, L"-"); pUnregister(sid); }
    printf("register NULL sid hr %08lx\n", pRegister(NULL, name, name));
    printf("unregister NULL sid hr %08lx\n", pUnregister(NULL));
    printf("lookup NULL sid hr %08lx\n", pLookup(NULL, &sidstr));
    pFree(NULL);
    printf("free NULL ok\n");
    return 0;
}
