/* RegLoadKey of a binary (regf) hive, e.g. an MSIX/.adix Registry.dat, under HKEY_USERS (issue 010).
 * Usage: regloadkey_hive.exe HIVE; needs admin (SeRestore/SeBackup).
 * Dumps the loaded tree (key names, value names, types, sizes, data checksums) for diffing. */
#include <windows.h>
#include <stdio.h>
static void priv(const char *name)
{
    HANDLE t; TOKEN_PRIVILEGES tp = { 1 };
    OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES, &t);
    LookupPrivilegeValueA(NULL, name, &tp.Privileges[0].Luid);
    tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    printf("%s %d %lu\n", name, AdjustTokenPrivileges(t, FALSE, &tp, 0, NULL, NULL), GetLastError());
}
static void dump(HKEY key, int depth)
{
    static BYTE data[1 << 20];
    WCHAR name[16384];
    DWORD i, j, len, size, type, sum;
    HKEY sub;
    for (i = 0; len = ARRAYSIZE(name), size = sizeof(data), !RegEnumValueW(key, i, name, &len, 0, &type, data, &size); i++)
    {
        for (j = sum = 0; j < size; j++) sum = sum * 31 + data[j];
        printf("%*s\"%ls\" type %lu size %lu sum %08lx\n", depth * 2, "", name, type, size, sum);
    }
    for (i = 0; len = ARRAYSIZE(name), !RegEnumKeyExW(key, i, name, &len, 0, 0, 0, 0); i++)
    {
        printf("%*s[%ls]\n", depth * 2, "", name);
        if (!RegOpenKeyExW(key, name, 0, KEY_READ, &sub)) { dump(sub, depth + 1); RegCloseKey(sub); }
    }
}
int main(int argc, char **argv)
{
    HKEY key; LONG r;
    priv("SeRestorePrivilege"); priv("SeBackupPrivilege");
    r = RegLoadKeyA(HKEY_USERS, "wine008hive", argv[1]);
    printf("RegLoadKey %ld\n", r);
    r = RegOpenKeyExA(HKEY_USERS, "wine008hive", 0, KEY_READ, &key);
    printf("RegOpenKeyEx %ld\n", r);
    if (!r) { dump(key, 0); RegCloseKey(key); }
    printf("RegUnLoadKey %ld\n", RegUnLoadKeyA(HKEY_USERS, "wine008hive"));
    return 0;
}
