/* RegLoadKey of a binary (regf) hive, e.g. an MSIX/.adix Registry.dat, under HKEY_USERS (issue 010).
 * Usage: regloadkey_hive.exe HIVE; needs admin (SeRestore/SeBackup). */
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
int main(int argc, char **argv)
{
    HKEY key; LONG r; char name[256]; DWORD i, len;
    priv("SeRestorePrivilege"); priv("SeBackupPrivilege");
    r = RegLoadKeyA(HKEY_USERS, "wine008hive", argv[1]);
    printf("RegLoadKey %ld\n", r);
    r = RegOpenKeyExA(HKEY_USERS, "wine008hive\\Registry", 0, KEY_READ, &key);
    printf("RegOpenKeyEx Registry %ld\n", r);
    if (!r) { for (i = 0; len = sizeof(name), !RegEnumKeyExA(key, i, name, &len, 0, 0, 0, 0); i++) printf("  %s\n", name); RegCloseKey(key); }
    printf("RegUnLoadKey %ld\n", RegUnLoadKeyA(HKEY_USERS, "wine008hive"));
    return 0;
}
