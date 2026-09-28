/* URL protocol registered only under HKCU\Software\Classes: visible via HKCR and AssocQueryString? */
#include <windows.h>
#include <shlwapi.h>
#include <stdio.h>
#include <winreg.h>
int main(void)
{
    HKEY k; WCHAR buf[1024]; DWORD sz; HRESULT hr; LONG r;
    RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Classes\\wineassoctest\\shell\\open\\command", 0, NULL, 0, KEY_ALL_ACCESS, NULL, &k, NULL);
    RegSetValueExW(k, NULL, 0, REG_SZ, (BYTE *)L"\"C:\\Windows\\notepad.exe\" \"%1\"", 64);
    RegCloseKey(k);
    RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Classes\\wineassoctest", 0, KEY_ALL_ACCESS, &k);
    RegSetValueExW(k, L"URL Protocol", 0, REG_SZ, (BYTE *)L"", 2);
    RegCloseKey(k);
    r = RegOpenKeyExW(HKEY_CLASSES_ROOT, L"wineassoctest", 0, KEY_READ, &k);
    printf("RegOpenKeyEx(HKCR) %ld\n", r); if (!r) RegCloseKey(k);
    sz = 1024; hr = AssocQueryStringW(ASSOCF_IS_PROTOCOL, ASSOCSTR_FRIENDLYAPPNAME, L"wineassoctest", NULL, buf, &sz);
    printf("FRIENDLYAPPNAME %08lx %ls\n", hr, SUCCEEDED(hr) ? buf : L"");
    sz = 1024; hr = AssocQueryStringW(ASSOCF_IS_PROTOCOL, ASSOCSTR_COMMAND, L"wineassoctest", NULL, buf, &sz);
    printf("COMMAND %08lx %ls\n", hr, SUCCEEDED(hr) ? buf : L"");
    RegDeleteTreeW(HKEY_CURRENT_USER, L"Software\\Classes\\wineassoctest");
    return 0;
}
