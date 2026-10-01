/* CreateProcess with PROC_THREAD_ATTRIBUTE_SECURITY_CAPABILITIES (issue 095): attribute validation,
 * the child's token and what the child can access.
 * Build: x86_64-w64-mingw32-gcc -O2 -o seccaps_probe.exe seccaps_probe.c -ladvapi32
 * Run elevated or not; the child is this exe with argv[1] = "child". */
#define _WIN32_WINNT 0x0A00
#include <windows.h>
#include <sddl.h>
#include <aclapi.h>
#include <userenv.h>
#include <stdio.h>

#define TokenIsLessPrivilegedAppContainer 46
#ifndef PROC_THREAD_ATTRIBUTE_ALL_APPLICATION_PACKAGES_POLICY
#define PROC_THREAD_ATTRIBUTE_ALL_APPLICATION_PACKAGES_POLICY 0x2000f
#endif
#define PROC_THREAD_ATTRIBUTE_COMPONENT_FILTER 0x2001a
#ifndef PROC_THREAD_ATTRIBUTE_CHILD_PROCESS_POLICY
#define PROC_THREAD_ATTRIBUTE_CHILD_PROCESS_POLICY 0x2000e
#endif

static char dir[MAX_PATH], exe[MAX_PATH];

static void print_sid(const char *what, PSID sid)
{
    char *s;
    if (!sid) { printf(" %s NULL", what); return; }
    if (ConvertSidToStringSidA(sid, &s)) { printf(" %s %s", what, s); LocalFree(s); }
}

static void dump_token(HANDLE t)
{
    static char buf[8192];
    DWORD len, v, i;
    TOKEN_TYPE type;
    TOKEN_GROUPS *g = (TOKEN_GROUPS *)buf;

    GetTokenInformation(t, TokenType, &type, sizeof(type), &len);
    printf("  type %d", type);
    v = 0xdead;
    if (GetTokenInformation(t, TokenIsAppContainer, &v, sizeof(v), &len)) printf(" isappcontainer %lu", v);
    else printf(" isappcontainer err %lu", GetLastError());
    if (GetTokenInformation(t, TokenAppContainerSid, buf, sizeof(buf), &len))
        print_sid("acsid", ((TOKEN_APPCONTAINER_INFORMATION *)buf)->TokenAppContainer);
    else printf(" acsid err %lu", GetLastError());
    v = 0xdead;
    if (GetTokenInformation(t, TokenAppContainerNumber, &v, sizeof(v), &len)) printf(" acnum %lu", v);
    else printf(" acnum err %lu", GetLastError());
    v = 0xdead;
    if (GetTokenInformation(t, TokenIsLessPrivilegedAppContainer, &v, sizeof(v), &len)) printf(" lpac %lu", v);
    else printf(" lpac err %lu", GetLastError());
    if (GetTokenInformation(t, TokenIntegrityLevel, buf, sizeof(buf), &len))
        print_sid("il", ((TOKEN_MANDATORY_LABEL *)buf)->Label.Sid);
    v = 0xdead;
    if (GetTokenInformation(t, TokenElevation, &v, sizeof(v), &len)) printf(" elevated %lu", v);
    if (GetTokenInformation(t, TokenUser, buf, sizeof(buf), &len)) print_sid("user", ((TOKEN_USER *)buf)->User.Sid);
    printf("\n");
    if (GetTokenInformation(t, TokenCapabilities, buf, sizeof(buf), &len))
    {
        printf("  caps %lu:", g->GroupCount);
        for (i = 0; i < g->GroupCount; i++) { print_sid("", g->Groups[i].Sid); printf("/%lx", g->Groups[i].Attributes); }
        printf("\n");
    }
    else printf("  caps err %lu\n", GetLastError());
    if (GetTokenInformation(t, TokenGroups, buf, sizeof(buf), &len))
    {
        printf("  groups %lu:", g->GroupCount);
        for (i = 0; i < g->GroupCount; i++) { print_sid("", g->Groups[i].Sid); printf("/%lx", g->Groups[i].Attributes); }
        printf("\n");
    }
    if (GetTokenInformation(t, TokenPrivileges, buf, sizeof(buf), &len))
    {
        TOKEN_PRIVILEGES *p = (TOKEN_PRIVILEGES *)buf;
        printf("  privileges %lu:", p->PrivilegeCount);
        for (i = 0; i < p->PrivilegeCount; i++)
        {
            char name[64]; DWORD n = sizeof(name);
            LookupPrivilegeNameA(NULL, &p->Privileges[i].Luid, name, &n);
            printf(" %s/%lx", name, p->Privileges[i].Attributes);
        }
        printf("\n");
    }
}

static void try_open(const char *what, const char *path, DWORD access, DWORD flags)
{
    HANDLE h = CreateFileA(path, access, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                           OPEN_EXISTING, flags, NULL);
    printf("  open %s: %s", what, h == INVALID_HANDLE_VALUE ? "fail" : "ok");
    if (h == INVALID_HANDLE_VALUE) printf(" %lu", GetLastError()); else CloseHandle(h);
    printf("\n");
}

static int child(void)
{
    char path[MAX_PATH], *p;
    HANDLE t, h, ev;
    HKEY key;
    LONG r;

    OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &t);
    printf(" child process token:\n");
    dump_token(t);
    GetEnvironmentVariableA("USERPROFILE", path, sizeof(path));
    try_open("userprofile dir", path, FILE_LIST_DIRECTORY, FILE_FLAG_BACKUP_SEMANTICS);
    {
        static const char *vars[] = {"LOCALAPPDATA", "APPDATA", "TEMP", "TMP", "USERPROFILE", "PROBE095"};
        int i;
        for (i = 0; i < 6; i++)
        {
            path[0] = 0;
            GetEnvironmentVariableA(vars[i], path, sizeof(path));
            printf("  %s=%s\n", vars[i], path);
        }
        printf("  attrs of LOCALAPPDATA: %lx\n", GetFileAttributesA(getenv("LOCALAPPDATA")));
    }
    try_open("system32 kernel32.dll", "C:\\Windows\\System32\\kernel32.dll", GENERIC_READ, 0);
    sprintf(path, "%s\\acdir", dir);
    try_open("acdir (package SID ACE)", path, FILE_LIST_DIRECTORY, FILE_FLAG_BACKUP_SEMANTICS);
    sprintf(path, "%s\\acdir\\new.txt", dir);
    h = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    printf("  create acdir\\new.txt: %s %lu\n", h == INVALID_HANDLE_VALUE ? "fail" : "ok", GetLastError());
    if (h != INVALID_HANDLE_VALUE) { CloseHandle(h); DeleteFileA(path); }
    sprintf(path, "%s\\nodir", dir);
    try_open("nodir (no ACE)", path, FILE_LIST_DIRECTORY, FILE_FLAG_BACKUP_SEMANTICS);
    sprintf(path, "%s\\allpkg", dir);
    try_open("allpkg (ALL APPLICATION PACKAGES ACE)", path, FILE_LIST_DIRECTORY, FILE_FLAG_BACKUP_SEMANTICS);
    r = RegOpenKeyExA(HKEY_CURRENT_USER, "Software", 0, KEY_READ, &key);
    printf("  HKCU\\Software KEY_READ: %ld\n", r); if (!r) RegCloseKey(key);
    r = RegOpenKeyExA(HKEY_LOCAL_MACHINE, "Software\\Microsoft\\Windows NT\\CurrentVersion", 0, KEY_READ, &key);
    printf("  HKLM\\...\\CurrentVersion KEY_READ: %ld\n", r); if (!r) RegCloseKey(key);
    ev = CreateEventA(NULL, FALSE, FALSE, "Local\\seccaps095");
    printf("  CreateEvent Local\\seccaps095: %p %lu\n", ev, GetLastError());
    ev = OpenEventA(SYNCHRONIZE, FALSE, "Global\\seccaps095parent");
    printf("  OpenEvent Global\\seccaps095parent (parent's): %p %lu\n", ev, GetLastError());
    ev = OpenEventA(SYNCHRONIZE, FALSE, "seccaps095parent");
    printf("  OpenEvent seccaps095parent (parent's, unqualified): %p %lu\n", ev, GetLastError());
    p = GetCommandLineA();
    (void)p;
    fflush(stdout);
    return 42;
}

static void grant(const char *path, PSID sid, DWORD access)
{
    EXPLICIT_ACCESSA ea = {0};
    PACL old = NULL, acl = NULL;
    PSECURITY_DESCRIPTOR sd;
    DWORD r;

    GetNamedSecurityInfoA(path, SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, NULL, NULL, &old, NULL, &sd);
    ea.grfAccessPermissions = access;
    ea.grfAccessMode = GRANT_ACCESS;
    ea.grfInheritance = SUB_CONTAINERS_AND_OBJECTS_INHERIT;
    ea.Trustee.TrusteeForm = TRUSTEE_IS_SID;
    ea.Trustee.ptstrName = sid;
    SetEntriesInAclA(1, &ea, old, &acl);
    r = SetNamedSecurityInfoA((char *)path, SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, NULL, NULL, acl, NULL);
    if (r) printf("grant %s: %lu\n", path, r);
    LocalFree(acl); LocalFree(sd);
}

static void attr_size(const char *what, DWORD_PTR attr, SIZE_T size)
{
    char list_buf[256], value[64] = {0};
    LPPROC_THREAD_ATTRIBUTE_LIST list = (void *)list_buf;
    SIZE_T sz = sizeof(list_buf);
    BOOL ret;

    InitializeProcThreadAttributeList(list, 1, 0, &sz);
    SetLastError(0xdeadbeef);
    ret = UpdateProcThreadAttribute(list, 0, attr, value, size, NULL, NULL);
    printf("Update %s size %u: %d err %lu\n", what, (unsigned)size, ret, ret ? 0 : GetLastError());
    DeleteProcThreadAttributeList(list);
}

/* launch the child; token may be NULL; sc NULL = no attribute */
static char *launch_env;
static const char *child_args = "child";
static void launch(const char *what, HANDLE token, SECURITY_CAPABILITIES *sc, DWORD *lpac)
{
    char list_buf[512], cmd[MAX_PATH + 16];
    STARTUPINFOEXA si = {{sizeof(si)}};
    PROCESS_INFORMATION pi;
    SIZE_T sz = sizeof(list_buf);
    DWORD code = 0;
    BOOL ret;

    printf("== %s\n", what);
    fflush(stdout);
    si.lpAttributeList = (void *)list_buf;
    InitializeProcThreadAttributeList(si.lpAttributeList, 2, 0, &sz);
    if (sc && !UpdateProcThreadAttribute(si.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_SECURITY_CAPABILITIES,
                                         sc, sizeof(*sc), NULL, NULL))
        printf(" Update SECURITY_CAPABILITIES failed %lu\n", GetLastError());
    if (lpac && !UpdateProcThreadAttribute(si.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_ALL_APPLICATION_PACKAGES_POLICY,
                                           lpac, sizeof(*lpac), NULL, NULL))
        printf(" Update ALL_APPLICATION_PACKAGES_POLICY failed %lu\n", GetLastError());
    si.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    si.StartupInfo.hStdOutput = si.StartupInfo.hStdError = GetStdHandle(STD_OUTPUT_HANDLE);
    sprintf(cmd, "\"%s\" %s", exe, child_args);
    SetLastError(0xdeadbeef);
    if (token)
        ret = CreateProcessAsUserA(token, exe, cmd, NULL, NULL, TRUE, EXTENDED_STARTUPINFO_PRESENT, launch_env, NULL,
                                   &si.StartupInfo, &pi);
    else
        ret = CreateProcessA(exe, cmd, NULL, NULL, TRUE, EXTENDED_STARTUPINFO_PRESENT, launch_env, NULL,
                             &si.StartupInfo, &pi);
    if (!ret)
    {
        printf(" CreateProcess failed %lu\n", GetLastError());
        DeleteProcThreadAttributeList(si.lpAttributeList);
        return;
    }
    WaitForSingleObject(pi.hProcess, 20000);
    GetExitCodeProcess(pi.hProcess, &code);
    printf(" child exit %lu\n", code);
    CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
    DeleteProcThreadAttributeList(si.lpAttributeList);
}

int main(int argc, char **argv)
{
    SID_IDENTIFIER_AUTHORITY pkg_auth = {SECURITY_APP_PACKAGE_AUTHORITY};
    PSID acsid, cap1, cap2, allpkg, admins, bad;
    SID_AND_ATTRIBUTES caps[2];
    SECURITY_CAPABILITIES sc;
    HANDLE tok, dup, ev;
    char path[MAX_PATH], *p;
    DWORD lpac = 1;
    HRESULT hr;
    HRESULT (WINAPI *pRegister)(PSID, const WCHAR *, const WCHAR *);
    HRESULT (WINAPI *pUnregister)(PSID);

    GetModuleFileNameA(NULL, exe, sizeof(exe));
    strcpy(dir, exe);
    if ((p = strrchr(dir, '\\'))) *p = 0;
    setvbuf(stdout, NULL, _IONBF, 0);
    if (argc > 1 && !strcmp(argv[1], "child")) return child();

    hr = ((HRESULT (WINAPI *)(const WCHAR *, PSID *))GetProcAddress(LoadLibraryA("userenv.dll"),
            "DeriveAppContainerSidFromAppContainerName"))(L"wine.test.seccaps095", &acsid);
    if (hr) { printf("Derive failed %08lx\n", hr); return 1; }
    print_sid("package", acsid); printf("\n");
    ConvertStringSidToSidA("S-1-15-3-1", &cap1);           /* internetClient */
    ConvertStringSidToSidA("S-1-15-3-1024-1-2-3-4-5-6-7-8", &cap2); /* custom named-capability-like */
    AllocateAndInitializeSid(&pkg_auth, 2, SECURITY_APP_PACKAGE_BASE_RID, SECURITY_BUILTIN_PACKAGE_ANY_PACKAGE,
                             0, 0, 0, 0, 0, 0, &allpkg); /* S-1-15-2-1 */
    ConvertStringSidToSidA("S-1-5-32-544", &admins);
    caps[0].Sid = cap1; caps[0].Attributes = SE_GROUP_ENABLED;
    caps[1].Sid = cap2; caps[1].Attributes = 0;
    sc.AppContainerSid = acsid; sc.Capabilities = caps; sc.CapabilityCount = 2; sc.Reserved = 0;

    if (argc > 2 && !strcmp(argv[1], "run"))  /* run argv[2] (in this dir) in the app container */
    {
        pRegister = (void *)GetProcAddress(GetModuleHandleA("kernelbase.dll"), "AppContainerRegisterSid");
        pUnregister = (void *)GetProcAddress(GetModuleHandleA("kernelbase.dll"), "AppContainerUnregisterSid");
        pRegister(acsid, L"wine.test.seccaps095", L"seccaps095");
        sprintf(exe, "%s\\%s", dir, argv[2]);
        grant(exe, acsid, GENERIC_READ | GENERIC_EXECUTE);
        child_args = "process seccaps";
        launch("run", NULL, &sc, NULL);
        pUnregister(acsid);
        return 0;
    }
    printf("sizeof(SECURITY_CAPABILITIES) %u\n", (unsigned)sizeof(SECURITY_CAPABILITIES));
    attr_size("SECURITY_CAPABILITIES", PROC_THREAD_ATTRIBUTE_SECURITY_CAPABILITIES, sizeof(SECURITY_CAPABILITIES));
    attr_size("SECURITY_CAPABILITIES", PROC_THREAD_ATTRIBUTE_SECURITY_CAPABILITIES, sizeof(SECURITY_CAPABILITIES) - 1);
    attr_size("SECURITY_CAPABILITIES", PROC_THREAD_ATTRIBUTE_SECURITY_CAPABILITIES, sizeof(SECURITY_CAPABILITIES) + 1);
    attr_size("SECURITY_CAPABILITIES", PROC_THREAD_ATTRIBUTE_SECURITY_CAPABILITIES, 0);
    attr_size("SECURITY_CAPABILITIES|THREAD", PROC_THREAD_ATTRIBUTE_SECURITY_CAPABILITIES | PROC_THREAD_ATTRIBUTE_THREAD,
              sizeof(SECURITY_CAPABILITIES));
    attr_size("ALL_APPLICATION_PACKAGES_POLICY", PROC_THREAD_ATTRIBUTE_ALL_APPLICATION_PACKAGES_POLICY, 4);
    attr_size("ALL_APPLICATION_PACKAGES_POLICY", PROC_THREAD_ATTRIBUTE_ALL_APPLICATION_PACKAGES_POLICY, 8);
    attr_size("ALL_APPLICATION_PACKAGES_POLICY", PROC_THREAD_ATTRIBUTE_ALL_APPLICATION_PACKAGES_POLICY, 2);
    attr_size("ALL_APPLICATION_PACKAGES_POLICY", PROC_THREAD_ATTRIBUTE_ALL_APPLICATION_PACKAGES_POLICY, 0);
    attr_size("COMPONENT_FILTER", PROC_THREAD_ATTRIBUTE_COMPONENT_FILTER, 4);
    attr_size("COMPONENT_FILTER", PROC_THREAD_ATTRIBUTE_COMPONENT_FILTER, 8);
    attr_size("CHILD_PROCESS_POLICY", PROC_THREAD_ATTRIBUTE_CHILD_PROCESS_POLICY, 4);
    attr_size("CHILD_PROCESS_POLICY", PROC_THREAD_ATTRIBUTE_CHILD_PROCESS_POLICY, 8);
    attr_size("CHILD_PROCESS_POLICY", PROC_THREAD_ATTRIBUTE_CHILD_PROCESS_POLICY, 2);
    attr_size("MITIGATION_POLICY", PROC_THREAD_ATTRIBUTE_MITIGATION_POLICY, 16);
    attr_size("MITIGATION_POLICY", PROC_THREAD_ATTRIBUTE_MITIGATION_POLICY, 24);

    OpenProcessToken(GetCurrentProcess(), TOKEN_ALL_ACCESS, &tok);
    printf("parent token:\n");
    dump_token(tok);

    ev = CreateEventA(NULL, FALSE, FALSE, "seccaps095parent");
    sprintf(path, "%s\\acdir", dir); CreateDirectoryA(path, NULL); grant(path, acsid, GENERIC_ALL);
    sprintf(path, "%s\\nodir", dir); CreateDirectoryA(path, NULL);
    sprintf(path, "%s\\allpkg", dir); CreateDirectoryA(path, NULL); grant(path, allpkg, GENERIC_READ | GENERIC_EXECUTE);

    launch("no attribute (baseline)", NULL, NULL, NULL);
    launch("caps, unregistered, exe without package ACE", NULL, &sc, NULL);
    grant(exe, acsid, GENERIC_READ | GENERIC_EXECUTE);
    launch("caps, unregistered", NULL, &sc, NULL);
    pRegister = (void *)GetProcAddress(GetModuleHandleA("kernelbase.dll"), "AppContainerRegisterSid");
    pUnregister = (void *)GetProcAddress(GetModuleHandleA("kernelbase.dll"), "AppContainerUnregisterSid");
    hr = pRegister(acsid, L"wine.test.seccaps095", L"seccaps095");
    printf("AppContainerRegisterSid %08lx\n", hr);
    launch("caps", NULL, &sc, NULL);
    launch("caps + ALL_APPLICATION_PACKAGES opt-out", NULL, &sc, &lpac);
    launch_env = "PROBE095=1\0LOCALAPPDATA=C:\\explicit\0TEMP=C:\\explicit\\temp\0SystemRoot=C:\\Windows\0";
    launch("caps, explicit environment", NULL, &sc, NULL);
    launch_env = NULL;
    lpac = 0;
    launch("caps + ALL_APPLICATION_PACKAGES policy 0", NULL, &sc, &lpac);
    sc.CapabilityCount = 0; sc.Capabilities = NULL;
    launch("no capabilities", NULL, &sc, NULL);
    sc.Capabilities = caps; sc.CapabilityCount = 2;

    DuplicateTokenEx(tok, TOKEN_ALL_ACCESS, NULL, SecurityImpersonation, TokenPrimary, &dup);
    launch("CreateProcessAsUser(primary dup) + caps", dup, &sc, NULL);
    CloseHandle(dup);
    {
        SECURITY_CAPABILITIES sc2 = sc;
        HANDLE lowbox = NULL;
        BOOL (WINAPI *pCreateAppContainerToken)(HANDLE, SECURITY_CAPABILITIES *, HANDLE *) =
            (void *)GetProcAddress(GetModuleHandleA("kernelbase.dll"), "CreateAppContainerToken");
        if (pCreateAppContainerToken(NULL, &sc, &lowbox))
        {
            launch("CreateProcessAsUser(lowbox token), no attribute", lowbox, NULL, NULL);
            ConvertStringSidToSidA("S-1-15-2-1-2-3-4-5-6-7", &sc2.AppContainerSid);
            grant(exe, sc2.AppContainerSid, GENERIC_READ | GENERIC_EXECUTE);
            launch("CreateProcessAsUser(lowbox token) + caps of another package", lowbox, &sc2, NULL);
            CloseHandle(lowbox);
        }
        else printf("CreateAppContainerToken failed %lu\n", GetLastError());
    }

    sc.AppContainerSid = cap1;
    launch("capability SID as package SID", NULL, &sc, NULL);
    sc.AppContainerSid = NULL;
    launch("NULL package SID", NULL, &sc, NULL);
    sc.AppContainerSid = allpkg;
    launch("ALL APPLICATION PACKAGES as package SID", NULL, &sc, NULL);
    sc.AppContainerSid = acsid;
    caps[1].Sid = admins;
    launch("non-capability SID in capabilities", NULL, &sc, NULL);
    bad = acsid;
    caps[1].Sid = bad;
    launch("package SID in capabilities", NULL, &sc, NULL);
    caps[1].Sid = cap2;
    {
        typedef LONG (WINAPI *lowbox_t)(HANDLE *, HANDLE, ACCESS_MASK, void *, PSID, ULONG, SID_AND_ATTRIBUTES *, ULONG, HANDLE *);
        lowbox_t pNtCreateLowBoxToken = (lowbox_t)GetProcAddress(GetModuleHandleA("ntdll.dll"), "NtCreateLowBoxToken");
        HANDLE lb = NULL, lb2 = NULL;
        PSID other;
        LONG st;
        ConvertStringSidToSidA("S-1-15-2-1-2-3-4-5-6-7", &other);
        st = pNtCreateLowBoxToken(&lb, tok, TOKEN_ALL_ACCESS, NULL, acsid, 2, caps, 0, NULL);
        printf("NtCreateLowBoxToken %08lx\n", st);
        st = pNtCreateLowBoxToken(&lb2, lb, TOKEN_ALL_ACCESS, NULL, acsid, 0, NULL, 0, NULL);
        printf("NtCreateLowBoxToken from lowbox, same package %08lx\n", st);
        if (!st) { dump_token(lb2); CloseHandle(lb2); }
        st = pNtCreateLowBoxToken(&lb2, lb, TOKEN_ALL_ACCESS, NULL, other, 0, NULL, 0, NULL);
        printf("NtCreateLowBoxToken from lowbox, other package %08lx\n", st);
        if (!st) { dump_token(lb2); CloseHandle(lb2); }
        st = pNtCreateLowBoxToken(&lb2, tok, TOKEN_ALL_ACCESS, NULL, acsid, 1, &caps[1], 0, NULL);
        printf("NtCreateLowBoxToken cap with attrs 0 %08lx\n", st);
        if (!st) { dump_token(lb2); CloseHandle(lb2); }
        caps[1].Sid = admins;
        st = pNtCreateLowBoxToken(&lb2, tok, TOKEN_ALL_ACCESS, NULL, acsid, 2, caps, 0, NULL);
        printf("NtCreateLowBoxToken non-capability SID %08lx\n", st);
        caps[1].Sid = cap2;
    }
    sprintf(path, "%s\\Packages\\wine.test.seccaps095", getenv("LOCALAPPDATA"));
    printf("parent: attrs of %s: %lx\n", path, GetFileAttributesA(path));

    hr = pUnregister(acsid);
    printf("AppContainerUnregisterSid %08lx\n", hr);
    launch("caps after unregister", NULL, &sc, NULL);
    printf("parent token after:\n");
    dump_token(tok);
    CloseHandle(ev);
    return 0;
}
