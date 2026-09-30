/* Which access rights each *_SECURITY_INFORMATION flag needs (094).
 * A) NtSetSecurityObject on a handle opened with exactly one right, per info flag and object kind.
 * B) SetNamedSecurityInfoW per info flag on objects whose DACL is only an OWNER RIGHTS (S-1-3-4) ACE
 *    granting FULL minus one right: a failure shows which right the API opens the object with.
 * C) read back the label after SetNamedSecurityInfo / SetSecurityInfo(LABEL).
 * x86_64-w64-mingw32-gcc -O2 -o setsecinfo_access.exe setsecinfo_access.c -ladvapi32 -lntdll */
#include <windows.h>
#include <winternl.h>
#include <aclapi.h>
#include <securitybaseapi.h>
#include <stdio.h>

NTSTATUS NTAPI NtSetSecurityObject(HANDLE, SECURITY_INFORMATION, PSECURITY_DESCRIPTOR);

enum kind { K_FILE, K_DIR, K_KEY, K_EVENT };
static const char *kname[] = {"file", "dir", "key", "event"};
static WCHAR base[MAX_PATH], keybase[MAX_PATH];
static int counter;

static struct { DWORD info; const char *name; } infos[] = {
    {OWNER_SECURITY_INFORMATION, "OWNER"}, {GROUP_SECURITY_INFORMATION, "GROUP"},
    {DACL_SECURITY_INFORMATION, "DACL"}, {SACL_SECURITY_INFORMATION, "SACL"},
    {LABEL_SECURITY_INFORMATION, "LABEL"}, {ATTRIBUTE_SECURITY_INFORMATION, "ATTRIBUTE"},
    {SCOPE_SECURITY_INFORMATION, "SCOPE"}, {0x80 /* PROCESS_TRUST_LABEL */, "TRUST"},
    {BACKUP_SECURITY_INFORMATION, "BACKUP"},
    {PROTECTED_DACL_SECURITY_INFORMATION, "PROT_DACL"}, {UNPROTECTED_DACL_SECURITY_INFORMATION, "UNPROT_DACL"},
    {PROTECTED_SACL_SECURITY_INFORMATION, "PROT_SACL"}, {UNPROTECTED_SACL_SECURITY_INFORMATION, "UNPROT_SACL"},
    {DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION, "DACL|PROT_DACL"},
    {DACL_SECURITY_INFORMATION | LABEL_SECURITY_INFORMATION, "DACL|LABEL"},
};
static struct { DWORD access; const char *name; } rights[] = {
    {READ_CONTROL, "RC"}, {WRITE_DAC, "WDAC"}, {WRITE_OWNER, "WO"}, {ACCESS_SYSTEM_SECURITY, "ASS"},
};

static PSID user, group, lowlabel, ownerrights;
static PACL sacl_label, sacl_empty;

static PACL owner_rights_dacl(DWORD mask)
{
    PACL acl = LocalAlloc(LPTR, 256);
    InitializeAcl(acl, 256, ACL_REVISION);
    AddAccessAllowedAce(acl, ACL_REVISION, mask, ownerrights);
    return acl;
}

/* create a fresh object with DACL = OWNER RIGHTS:mask, owner = user; return its name */
static void create_obj(enum kind k, DWORD mask, WCHAR *name)
{
    SECURITY_DESCRIPTOR sd;
    SECURITY_ATTRIBUTES sa = {sizeof(sa), &sd, FALSE};
    HANDLE h; HKEY key;
    InitializeSecurityDescriptor(&sd, SECURITY_DESCRIPTOR_REVISION);
    SetSecurityDescriptorOwner(&sd, user, FALSE);
    SetSecurityDescriptorDacl(&sd, TRUE, owner_rights_dacl(mask), FALSE);
    SetSecurityDescriptorControl(&sd, SE_DACL_PROTECTED, SE_DACL_PROTECTED);
    switch (k)
    {
    case K_FILE:
        _snwprintf(name, MAX_PATH, L"%s\\f%d", base, counter++);
        h = CreateFileW(name, GENERIC_WRITE, 0, &sa, CREATE_NEW, 0, NULL);
        if (h == INVALID_HANDLE_VALUE) printf("create file %lu\n", GetLastError());
        CloseHandle(h);
        break;
    case K_DIR:
        _snwprintf(name, MAX_PATH, L"%s\\d%d", base, counter++);
        if (!CreateDirectoryW(name, &sa)) printf("create dir %lu\n", GetLastError());
        break;
    case K_KEY:
        _snwprintf(name, MAX_PATH, L"%s\\k%d", keybase, counter++);
        if (RegCreateKeyExW(HKEY_CURRENT_USER, name, 0, NULL, 0, KEY_READ, &sa, &key, NULL)) printf("create key failed\n");
        RegCloseKey(key);
        { WCHAR full[MAX_PATH]; _snwprintf(full, MAX_PATH, L"CURRENT_USER\\%s", name); wcscpy(name, full); }
        break;
    default: break;
    }
}

static void fill_sd(SECURITY_DESCRIPTOR *sd, DWORD info, PACL dacl)
{
    InitializeSecurityDescriptor(sd, SECURITY_DESCRIPTOR_REVISION);
    SetSecurityDescriptorOwner(sd, user, FALSE);
    SetSecurityDescriptorGroup(sd, group, FALSE);
    SetSecurityDescriptorDacl(sd, TRUE, dacl, FALSE);
    SetSecurityDescriptorSacl(sd, TRUE, (info & SACL_SECURITY_INFORMATION) ? sacl_empty : sacl_label, FALSE);
}

static HANDLE open_obj(enum kind k, const WCHAR *name, DWORD access)
{
    HANDLE h; HKEY key; LONG r;
    switch (k)
    {
    case K_FILE: case K_DIR:
        h = CreateFileW(name, access, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING,
                        FILE_FLAG_BACKUP_SEMANTICS, NULL);
        return h == INVALID_HANDLE_VALUE ? NULL : h;
    case K_KEY:
        r = RegOpenKeyExW(HKEY_CURRENT_USER, name + 13, 0, access, &key);
        return r ? NULL : (HANDLE)key;
    case K_EVENT:
        { WCHAR ev[64]; _snwprintf(ev, 64, L"setsecinfo_094_ev%d", counter++); h = CreateEventW(NULL, FALSE, FALSE, ev); }
        { HANDLE d; DuplicateHandle(GetCurrentProcess(), h, GetCurrentProcess(), &d, access, FALSE, 0); CloseHandle(h); return d; }
    }
    return NULL;
}

static void close_obj(enum kind k, HANDLE h)
{
    if (k == K_KEY) RegCloseKey(h); else CloseHandle(h);
}

static void part_a(void)
{
    WCHAR name[MAX_PATH];
    SECURITY_DESCRIPTOR sd;
    PACL full = owner_rights_dacl(GENERIC_ALL);
    int k, i, j;

    printf("A) NtSetSecurityObject status per handle access (object DACL: OWNER RIGHTS full)\n%-16s", "");
    for (j = 0; j < 4; j++) printf(" %-10s", rights[j].name);
    printf("\n");
    for (k = 0; k < 4; k++)
    {
        printf("%s:\n", kname[k]);
        for (i = 0; i < ARRAYSIZE(infos); i++)
        {
            printf("  %-14s", infos[i].name);
            for (j = 0; j < 4; j++)
            {
                HANDLE h;
                NTSTATUS st;
                if (k != K_EVENT) create_obj(k, GENERIC_ALL, name);
                if (!(h = open_obj(k, name, rights[j].access))) { printf(" open%-6lu", GetLastError()); continue; }
                fill_sd(&sd, infos[i].info, full);
                st = NtSetSecurityObject(h, infos[i].info, &sd);
                printf(" %08lx  ", st);
                close_obj(k, h);
            }
            printf("\n");
        }
    }
}

static void part_b(void)
{
    static const struct { DWORD access; const char *name; } drop[] = {
        {0, "-"}, {READ_CONTROL, "RC"}, {WRITE_DAC, "WDAC"}, {WRITE_OWNER, "WO"}, {SYNCHRONIZE, "SYNC"},
        {FILE_READ_ATTRIBUTES, "0x80"}, {FILE_READ_DATA /* KEY_QUERY_VALUE */, "0x1"}, {FILE_READ_EA /* KEY_ENUMERATE_SUB_KEYS */, "0x8"},
        {0x10 /* KEY_NOTIFY */, "0x10"},
    };
    WCHAR name[MAX_PATH];
    int k, i, j;
    DWORD fullmask = STANDARD_RIGHTS_ALL | 0x1ff; /* file: FILE_ALL_ACCESS; key: KEY_ALL_ACCESS covers 0x3f */

    printf("B) SetNamedSecurityInfoW result; object DACL = OWNER RIGHTS: all rights minus the column's\n%-16s", "");
    for (j = 0; j < ARRAYSIZE(drop); j++) printf(" %-6s", drop[j].name);
    printf("\n");
    for (k = 0; k < 3; k++)
    {
        printf("%s:\n", kname[k]);
        for (i = 0; i < ARRAYSIZE(infos); i++)
        {
            printf("  %-14s", infos[i].name);
            for (j = 0; j < ARRAYSIZE(drop); j++)
            {
                DWORD mask = fullmask & ~drop[j].access, r;
                PACL dacl = owner_rights_dacl(mask);
                create_obj(k, mask, name);
                r = SetNamedSecurityInfoW(name, k == K_KEY ? SE_REGISTRY_KEY : SE_FILE_OBJECT, infos[i].info,
                                          user, group, dacl, (infos[i].info & SACL_SECURITY_INFORMATION) ? sacl_empty : sacl_label);
                printf(" %-6lu", r);
            }
            printf("\n");
        }
    }
}

static void dump_label(const char *what, PACL sacl, DWORD r)
{
    ACE_HEADER *ace;
    printf("  %-40s r=%lu", what, r);
    if (!r && sacl && sacl->AceCount && GetAce(sacl, 0, (void **)&ace))
        printf(" count %u type %#x flags %#x mask %#lx", sacl->AceCount, ace->AceType, ace->AceFlags,
               ((SYSTEM_MANDATORY_LABEL_ACE *)ace)->Mask);
    else if (!r) printf(" sacl %p count %u", sacl, sacl ? sacl->AceCount : 0);
    printf("\n");
}

static void part_c(void)
{
    WCHAR name[MAX_PATH];
    PSECURITY_DESCRIPTOR sd;
    PACL sacl, dacl = owner_rights_dacl(GENERIC_ALL);
    DWORD r;
    int k;

    printf("C) label readback (GetNamedSecurityInfo / GetSecurityInfo LABEL)\n");
    for (k = 0; k < 4; k++)
    {
        HANDLE h;
        if (k != K_EVENT)
        {
            SE_OBJECT_TYPE t = k == K_KEY ? SE_REGISTRY_KEY : SE_FILE_OBJECT;
            create_obj(k, GENERIC_ALL, name);
            r = SetNamedSecurityInfoW(name, t, DACL_SECURITY_INFORMATION | LABEL_SECURITY_INFORMATION, NULL, NULL, dacl, sacl_label);
            printf(" %s SetNamed DACL|LABEL %lu\n", kname[k], r);
            sacl = NULL; r = GetNamedSecurityInfoW(name, t, LABEL_SECURITY_INFORMATION, NULL, NULL, NULL, &sacl, &sd);
            dump_label("GetNamed LABEL", sacl, r);
            create_obj(k, GENERIC_ALL, name);
            h = open_obj(k, name, READ_CONTROL | WRITE_OWNER);
        }
        else h = open_obj(k, NULL, READ_CONTROL | WRITE_OWNER);
        r = SetSecurityInfo(h, k == K_KEY ? SE_REGISTRY_KEY : k == K_EVENT ? SE_KERNEL_OBJECT : SE_FILE_OBJECT,
                            LABEL_SECURITY_INFORMATION, NULL, NULL, NULL, sacl_label);
        printf(" %s SetSecurityInfo(RC|WO) LABEL %lu\n", kname[k], r);
        sacl = NULL; r = GetSecurityInfo(h, k == K_KEY ? SE_REGISTRY_KEY : k == K_EVENT ? SE_KERNEL_OBJECT : SE_FILE_OBJECT,
                                         LABEL_SECURITY_INFORMATION, NULL, NULL, NULL, &sacl, &sd);
        dump_label("GetSecurityInfo LABEL", sacl, r);
        close_obj(k, h);
    }
}

int main(void)
{
    SID_IDENTIFIER_AUTHORITY ml = {SECURITY_MANDATORY_LABEL_AUTHORITY}, cr = {SECURITY_CREATOR_SID_AUTHORITY};
    BYTE ubuf[256], gbuf[256];
    HANDLE token;
    DWORD len;
    TOKEN_PRIVILEGES tp;
    HKEY key;

    OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY | TOKEN_ADJUST_PRIVILEGES, &token);
    GetTokenInformation(token, TokenUser, ubuf, sizeof(ubuf), &len);
    user = ((TOKEN_USER *)ubuf)->User.Sid;
    GetTokenInformation(token, TokenPrimaryGroup, gbuf, sizeof(gbuf), &len);
    group = ((TOKEN_PRIMARY_GROUP *)gbuf)->PrimaryGroup;
    tp.PrivilegeCount = 1;
    LookupPrivilegeValueA(NULL, "SeSecurityPrivilege", &tp.Privileges[0].Luid);
    tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    AdjustTokenPrivileges(token, FALSE, &tp, 0, NULL, NULL);
    printf("SeSecurityPrivilege enable: %lu\n", GetLastError());

    AllocateAndInitializeSid(&ml, 1, SECURITY_MANDATORY_LOW_RID, 0, 0, 0, 0, 0, 0, 0, &lowlabel);
    AllocateAndInitializeSid(&cr, 1, 4 /* OWNER RIGHTS */, 0, 0, 0, 0, 0, 0, 0, &ownerrights);
    sacl_label = LocalAlloc(LPTR, 256);
    InitializeAcl(sacl_label, 256, ACL_REVISION);
    AddMandatoryAce(sacl_label, ACL_REVISION, OBJECT_INHERIT_ACE | CONTAINER_INHERIT_ACE, SYSTEM_MANDATORY_LABEL_NO_WRITE_UP, lowlabel);
    sacl_empty = LocalAlloc(LPTR, 64);
    InitializeAcl(sacl_empty, 64, ACL_REVISION);

    GetTempPathW(MAX_PATH, base);
    _snwprintf(base + wcslen(base), MAX_PATH, L"setsecinfo_%lu", GetTickCount());
    CreateDirectoryW(base, NULL);
    _snwprintf(keybase, MAX_PATH, L"Software\\setsecinfo_094_%lu", GetTickCount());
    RegCreateKeyExW(HKEY_CURRENT_USER, keybase, 0, NULL, 0, KEY_ALL_ACCESS, NULL, &key, NULL);
    RegCloseKey(key);

    part_a();
    part_b();
    part_c();
    printf("done\n");
    return 0;
}
