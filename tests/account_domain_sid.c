/* GetWindowsAccountDomainSid / EqualDomainSid results for well-known and account SIDs
 * (064: .NET SecurityIdentifier.AccountDomainSid throws Win32Exception on anything but
 * success / ERROR_NON_ACCOUNT_SID / ERROR_INSUFFICIENT_BUFFER).
 * Second table: argument validation order (NULL/invalid sid x NULL out x size values).
 * Build: x86_64-w64-mingw32-gcc -O2 -o account_domain_sid.exe account_domain_sid.c -ladvapi32 */
#include <windows.h>
#include <sddl.h>
#include <stdio.h>

static const char *sids[] = { "S-1-1-0", "S-1-5-18", "S-1-5-19", "S-1-5-20", "S-1-5-32-544", "S-1-5-32-545",
    "S-1-5-11", "S-1-2-0", "S-1-5-4", "S-1-5-21-1-2-3", "S-1-5-21-1-2-3-1000", "S-1-5-21-0-0-0-513",
    "S-1-5-21-1-2-3-4-5", "S-1-5-21-3623811015-3361044348-30300820-1001",
    "S-1-5-21-3623811015-3361044348-30300820-500", "S-1-5-80-1-2-3-4-5",
    "S-1-5-22-1-2-3-4", "S-1-16-12288", "S-1-5-21-1-2", "S-1-5-21", "S-1-5", "S-1-3-21-1-2-3-4",
    "S-1-5-32-21-1-2-3", "S-1-5-5-21-1-2" };

static void dump(const char *name, PSID sid, BOOL use_out, DWORD *psize)
{
    BYTE buf[SECURITY_MAX_SID_SIZE];
    char *str = NULL;
    BOOL r;
    DWORD in = psize ? *psize : 0;

    memset(buf, 0xcc, sizeof(buf));
    SetLastError(0xdeadbeef);
    r = GetWindowsAccountDomainSid(sid, use_out ? buf : NULL, psize);
    if (r) ConvertSidToStringSidA(buf, &str);
    printf("  %-10s out=%d size %s%-3lu -> %d err %-10lu size %-5ld buf[0]=%02x %s\n", name, use_out,
           psize ? "" : "NULL", in, r, r ? 0 : GetLastError(), psize ? (long)*psize : -1, buf[0], str ? str : "");
    LocalFree(str);
}

int main(void)
{
    static const DWORD sizes[] = { 0, 1, 23, 24, 68 };
    BYTE bad[SECURITY_MAX_SID_SIZE];
    PSID other, acct, nonacct;
    unsigned i, j, k;

    for (i = 0; i < ARRAYSIZE(sids); i++)
    {
        PSID sid;
        BYTE buf[SECURITY_MAX_SID_SIZE];
        DWORD size = sizeof(buf);
        char *str = NULL;
        BOOL equal = 7, r;

        if (!ConvertStringSidToSidA(sids[i], &sid)) { printf("%-22s parse failed %lu\n", sids[i], GetLastError()); continue; }
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

    /* malformed: revision 2, and subauth count 16 */
    ConvertStringSidToSidA("S-1-5-21-1-2-3-1000", &acct);
    ConvertStringSidToSidA("S-1-1-0", &nonacct);
    memcpy(bad, acct, GetLengthSid(acct));
    bad[0] = 2;
    {
        struct { const char *name; PSID sid; } cases[] = { { "NULL", NULL }, { "badrev", bad }, { "S-1-1-0", nonacct },
            { "acct", acct } };
        for (i = 0; i < ARRAYSIZE(cases); i++)
        {
            printf("%s\n", cases[i].name);
            for (j = 0; j < 2; j++)
            {
                dump(cases[i].name, cases[i].sid, j, NULL);
                for (k = 0; k < ARRAYSIZE(sizes); k++)
                {
                    DWORD size = sizes[k];
                    dump(cases[i].name, cases[i].sid, j, &size);
                }
            }
        }
    }
    bad[0] = 1;
    bad[1] = 16;
    { DWORD size = 68; printf("count16 (valid=%d)\n", IsValidSid(bad)); dump("count16", bad, 1, &size); }
    return 0;
}
