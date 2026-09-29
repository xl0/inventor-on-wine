/* GetWindowsAccountDomainSid / EqualDomainSid results for well-known and account SIDs
 * (064: .NET SecurityIdentifier.AccountDomainSid throws Win32Exception on anything but
 * success / ERROR_NON_ACCOUNT_SID / ERROR_INSUFFICIENT_BUFFER).
 * Build: x86_64-w64-mingw32-gcc -O2 -o account_domain_sid.exe account_domain_sid.c -ladvapi32 */
#include <windows.h>
#include <sddl.h>
#include <stdio.h>

static const char *sids[] = { "S-1-1-0", "S-1-5-18", "S-1-5-32-544", "S-1-5-11", "S-1-2-0", "S-1-5-4",
    "S-1-5-21-1-2-3", "S-1-5-21-1-2-3-1000", "S-1-5-21-0-0-0-513", "S-1-5-21-1-2-3-4-5", "S-1-5-80-1-2-3-4-5",
    "S-1-5-22-1-2-3-4", "S-1-16-12288", "S-1-5-21-1-2" };

int main(void)
{
    unsigned i;
    for (i = 0; i < ARRAYSIZE(sids); i++)
    {
        PSID sid, other;
        BYTE buf[SECURITY_MAX_SID_SIZE];
        DWORD size = sizeof(buf);
        char *str = NULL;
        BOOL equal = 7, r;

        ConvertStringSidToSidA(sids[i], &sid);
        SetLastError(0xdeadbeef);
        r = GetWindowsAccountDomainSid(sid, buf, &size);
        if (r) ConvertSidToStringSidA(buf, &str);
        printf("%-22s GetWindowsAccountDomainSid %d err %lu size %lu %s", sids[i], r, r ? 0 : GetLastError(), size, str ? str : "");
        ConvertStringSidToSidA("S-1-5-21-1-2-3-500", &other);
        SetLastError(0xdeadbeef);
        r = EqualDomainSid(sid, other, &equal);
        printf(" | EqualDomainSid(.., S-1-5-21-1-2-3-500) %d err %lu equal %d\n", r, r ? 0 : GetLastError(), equal);
        LocalFree(str);
    }
    return 0;
}
