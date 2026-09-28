/* Probes for the wineserver regf loader / RegLoadAppKey (issue 010 review).
 * regf_attack.exe load HIVE      RegLoadAppKey + dump tree (names, value counts)
 * regf_attack.exe existing HIVE  NtLoadKeyEx(REG_APP_HIVE) onto an existing HKCU key, then close
 * regf_attack.exe nullroot HIVE  NtLoadKeyEx(REG_APP_HIVE, roothandle NULL) (wow64 thunk) */
#define _WIN32_WINNT 0x0600
#include <windows.h>
#include <winternl.h>
#include <stdio.h>

#ifndef REG_APP_HIVE
#define REG_APP_HIVE 0x10
#endif

static NTSTATUS (WINAPI *pNtLoadKeyEx)(const OBJECT_ATTRIBUTES *, OBJECT_ATTRIBUTES *, ULONG, HANDLE, HANDLE,
                                       ACCESS_MASK, HANDLE *, IO_STATUS_BLOCK *);
static BOOLEAN (WINAPI *pRtlDosPathNameToNtPathName_U)(const WCHAR *, UNICODE_STRING *, WCHAR **, void *);

static void dump(HKEY key, const WCHAR *path, int depth)
{
    WCHAR name[1024], sub_path[4096];
    DWORD i, len, values;
    HKEY sub;

    RegQueryInfoKeyW(key, 0, 0, 0, 0, 0, 0, &values, 0, 0, 0, 0);
    printf("key [%ls] values %lu\n", path, values);
    if (depth > 4) return;
    for (i = 0; len = ARRAYSIZE(name), !RegEnumKeyExW(key, i, name, &len, 0, 0, 0, 0); i++)
    {
        if (i >= 3) { printf("  ...\n"); break; }
        swprintf(sub_path, ARRAYSIZE(sub_path), L"%ls\\%ls", path, name);
        if (!RegOpenKeyExW(key, name, 0, KEY_READ, &sub)) { dump(sub, sub_path, depth + 1); RegCloseKey(sub); }
        else printf("open [%ls] failed\n", sub_path);
    }
}

static NTSTATUS load_ex(const char *key_path, const char *hive, HANDLE *root)
{
    OBJECT_ATTRIBUTES key_attr, file_attr;
    UNICODE_STRING key_name, file_name;
    WCHAR keyW[MAX_PATH], fileW[MAX_PATH];

    MultiByteToWideChar(CP_ACP, 0, key_path, -1, keyW, MAX_PATH);
    MultiByteToWideChar(CP_ACP, 0, hive, -1, fileW, MAX_PATH);
    RtlInitUnicodeString(&key_name, keyW);
    pRtlDosPathNameToNtPathName_U(fileW, &file_name, NULL, NULL);
    InitializeObjectAttributes(&key_attr, &key_name, OBJ_CASE_INSENSITIVE, 0, NULL);
    InitializeObjectAttributes(&file_attr, &file_name, OBJ_CASE_INSENSITIVE, 0, NULL);
    return pNtLoadKeyEx(&key_attr, &file_attr, REG_APP_HIVE, 0, 0, KEY_READ, root, NULL);
}

int main(int argc, char **argv)
{
    HMODULE ntdll = GetModuleHandleA("ntdll.dll");
    HKEY key;
    LONG r;

    pNtLoadKeyEx = (void *)GetProcAddress(ntdll, "NtLoadKeyEx");
    pRtlDosPathNameToNtPathName_U = (void *)GetProcAddress(ntdll, "RtlDosPathNameToNtPathName_U");
    if (argc < 3) return 1;

    if (!strcmp(argv[1], "load"))
    {
        WCHAR file[MAX_PATH];
        MultiByteToWideChar(CP_ACP, 0, argv[2], -1, file, MAX_PATH);
        r = RegLoadAppKeyW(file, &key, KEY_READ, 0, 0);
        printf("RegLoadAppKey %ld\n", r);
        if (!r) { dump(key, L"", 0); RegCloseKey(key); }
    }
    else if (!strcmp(argv[1], "existing"))
    {
        char user_path[512];
        HANDLE root = 0;
        NTSTATUS status;
        HKEY sub;

        RegCreateKeyA(HKEY_CURRENT_USER, "Software\\WineAppHiveTest\\Precious", &sub);
        RegCloseKey(sub);
        /* HKCU of the default wine user */
        snprintf(user_path, sizeof(user_path),
                 "\\Registry\\User\\S-1-5-21-0-0-0-1000\\Software\\WineAppHiveTest");
        status = load_ex(user_path, argv[2], &root);
        printf("NtLoadKeyEx on existing key %#lx root %p\n", status, root);
        if (root) CloseHandle(root);
        r = RegOpenKeyExA(HKEY_CURRENT_USER, "Software\\WineAppHiveTest\\Precious", 0, KEY_READ, &sub);
        printf("pre-existing subkey after close: %ld\n", r);
        if (!r) RegCloseKey(sub);
        RegDeleteTreeA(HKEY_CURRENT_USER, "Software\\WineAppHiveTest");
    }
    else if (!strcmp(argv[1], "nullroot"))
    {
        NTSTATUS status = load_ex("\\Registry\\A\\WineNullRoot", argv[2], NULL);
        printf("NtLoadKeyEx roothandle NULL: %#lx\n", status);
    }
    return 0;
}
