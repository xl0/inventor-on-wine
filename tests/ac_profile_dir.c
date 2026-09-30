/* Chromium's AppContainer profile dir steps (sandbox/win/src/app_container_base.cc CreateAppContainerDirectory):
 * create DIR\AC, read its DACL, set a low mandatory label (OI|CI, NO_WRITE_UP) + grant FILE_ALL_ACCESS to an AC SID,
 * write DACL|LABEL back, create AC\Temp. Prints which step fails (090).
 * x86_64-w64-mingw32-gcc -O2 -o ac_profile_dir.exe ac_profile_dir.c -ladvapi32 */
#include <windows.h>
#include <aclapi.h>
#include <stdio.h>

int main(int argc, char **argv)
{
    char dir[MAX_PATH], ac[MAX_PATH];
    SID_IDENTIFIER_AUTHORITY app = {SECURITY_APP_PACKAGE_AUTHORITY}, ml = {SECURITY_MANDATORY_LABEL_AUTHORITY};
    PSID sid, low; PACL dacl = NULL, newdacl = NULL, sacl; PSECURITY_DESCRIPTOR sd = NULL;
    EXPLICIT_ACCESSA ea = {0}; DWORD r; BYTE buf[256];

    sprintf(dir, "%s\\ac_profile_test_%lu", getenv("TEMP"), GetTickCount());
    sprintf(ac, "%s\\AC", dir);
    printf("CreateDirectory %d %lu\n", CreateDirectoryA(dir, NULL), GetLastError());
    printf("CreateDirectory AC %d %lu\n", CreateDirectoryA(ac, NULL), GetLastError());
    r = GetNamedSecurityInfoA(ac, SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, NULL, NULL, &dacl, NULL, &sd);
    printf("GetNamedSecurityInfo DACL %lu\n", r);

    AllocateAndInitializeSid(&app, 8, 2, 1, 2, 3, 4, 5, 6, 7, &sid);
    AllocateAndInitializeSid(&ml, 1, SECURITY_MANDATORY_LOW_RID, 0, 0, 0, 0, 0, 0, 0, &low);
    ea.grfAccessPermissions = FILE_ALL_ACCESS; ea.grfAccessMode = GRANT_ACCESS;
    ea.grfInheritance = OBJECT_INHERIT_ACE | CONTAINER_INHERIT_ACE;
    ea.Trustee.TrusteeForm = TRUSTEE_IS_SID; ea.Trustee.ptstrName = sid;
    r = SetEntriesInAclA(1, &ea, dacl, &newdacl);
    printf("SetEntriesInAcl %lu\n", r);
    sacl = (PACL)buf;
    InitializeAcl(sacl, sizeof(buf), ACL_REVISION);
    printf("AddMandatoryAce %d %lu\n", AddMandatoryAce(sacl, ACL_REVISION, OBJECT_INHERIT_ACE | CONTAINER_INHERIT_ACE,
           SYSTEM_MANDATORY_LABEL_NO_WRITE_UP, low), GetLastError());
    r = SetNamedSecurityInfoA(ac, SE_FILE_OBJECT, DACL_SECURITY_INFORMATION | LABEL_SECURITY_INFORMATION,
                              NULL, NULL, newdacl, sacl);
    printf("SetNamedSecurityInfo DACL|LABEL %lu\n", r);
    r = SetNamedSecurityInfoA(ac, SE_FILE_OBJECT, LABEL_SECURITY_INFORMATION, NULL, NULL, NULL, sacl);
    printf("SetNamedSecurityInfo LABEL %lu\n", r);
    r = SetNamedSecurityInfoA(ac, SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, NULL, NULL, newdacl, NULL);
    printf("SetNamedSecurityInfo DACL %lu\n", r);
    {
        HANDLE h = CreateFileA(ac, READ_CONTROL | WRITE_DAC | WRITE_OWNER, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                               NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
        r = SetSecurityInfo(h, SE_FILE_OBJECT, DACL_SECURITY_INFORMATION | LABEL_SECURITY_INFORMATION, NULL, NULL, newdacl, sacl);
        printf("SetSecurityInfo handle(RC|WDAC|WO) DACL|LABEL %lu\n", r);
        CloseHandle(h);
    }
    strcat(ac, "\\Temp");
    printf("CreateDirectory Temp %d %lu\n", CreateDirectoryA(ac, NULL), GetLastError());
    return 0;
}
