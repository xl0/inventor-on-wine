/* DeriveAppContainerSidFromAppContainerName ground truth (090).
 * x86_64-w64-mingw32-gcc -O2 -o appcontainer_sid.exe appcontainer_sid.c -lbcrypt -ladvapi32 */
#include <windows.h>
#include <sddl.h>
#include <bcrypt.h>
#include <stdio.h>

static HRESULT (WINAPI *pDerive)(const WCHAR *, PSID *);
static HRESULT (WINAPI *pDeriveRestricted)(PSID, const WCHAR *, PSID *);

/* S-1-15-2 + first 7 dwords of SHA256(lowercase UTF-16LE name) */
static void expected(const WCHAR *name, char *out)
{
    WCHAR low[512]; DWORD h[8]; int i;
    lstrcpyW(low, name); CharLowerW(low);
    BCRYPT_ALG_HANDLE alg; BCRYPT_HASH_HANDLE hash;
    BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, NULL, 0);
    BCryptCreateHash(alg, &hash, NULL, 0, NULL, 0, 0);
    BCryptHashData(hash, (BYTE *)low, lstrlenW(low) * 2, 0);
    BCryptFinishHash(hash, (BYTE *)h, 32, 0);
    BCryptDestroyHash(hash); BCryptCloseAlgorithmProvider(alg, 0);
    out += sprintf(out, "S-1-15-2");
    for (i = 0; i < 7; i++) out += sprintf(out, "-%lu", h[i]);
}

static void test(const WCHAR *name)
{
    PSID sid = (PSID)0xdeadbeef; char *str = NULL, exp[256] = "";
    HRESULT hr;
    SetLastError(0xdeadbeef);
    hr = pDerive(name, &sid);
    printf("%-40.40ls len %3d: hr %08lx le %lu", name ? name : L"(null)", name ? lstrlenW(name) : -1, hr, GetLastError());
    if (SUCCEEDED(hr))
    {
        ConvertSidToStringSidA(sid, &str);
        if (name) expected(name, exp);
        printf(" %s %s valid %d heap %d", str, strcmp(str, exp) ? "MISMATCH" : "match", IsValidSid(sid),
               HeapValidate(GetProcessHeap(), 0, sid));
        LocalFree(str);
        printf(" free %p", FreeSid(sid));
    }
    else printf(" sid %p", sid);
    printf("\n");
}

int main(void)
{
    HMODULE u = LoadLibraryA("userenv.dll");
    WCHAR buf[300]; int i;
    pDerive = (void *)GetProcAddress(u, "DeriveAppContainerSidFromAppContainerName");
    pDeriveRestricted = (void *)GetProcAddress(u, "DeriveRestrictedAppContainerSidFromAppContainerSidAndRestrictedName");
    printf("userenv Derive %p Restricted %p CreateProfile %p kernelbase AppContainerDeriveSidFromMoniker %p\n",
           pDerive, pDeriveRestricted, GetProcAddress(u, "CreateAppContainerProfile"),
           GetProcAddress(LoadLibraryA("kernelbase.dll"), "AppContainerDeriveSidFromMoniker"));
    if (!pDerive) return 1;
    test(L"a");
    test(L"Microsoft.Test");
    test(L"MICROSOFT.TEST");
    test(L"microsoft.test");
    test(L"cr.sb.edge");
    test(L"a b");
    test(L"a\\b");
    test(L"a/b");
    test(L"\x00e9t\x00c9");
    test(L"");
    test(NULL);
    for (i = 0; i < 300; i++) buf[i] = 'a' + i % 26;
    for (i = 63; i <= 65; i++) { buf[i] = 0; test(buf); buf[i] = 'a' + i % 26; }
    buf[299] = 0; test(buf);
    if (0) pDerive(L"a", NULL);
    return 0;
}
