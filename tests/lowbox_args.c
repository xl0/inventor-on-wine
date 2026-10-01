/* NtCreateLowBoxToken argument validation and TokenAppContainerSid size queries (issue 095).
 * Build: x86_64-w64-mingw32-gcc -O2 -o lowbox_args.exe lowbox_args.c -ladvapi32
 * Each NtCreateLowBoxToken case runs in a child (argv[1] = case number) since some may crash. */
#include <windows.h>
#include <winternl.h>
#include <sddl.h>
#include <stdio.h>
#include <stdlib.h>

typedef LONG (WINAPI *lowbox_t)(HANDLE *, HANDLE, ACCESS_MASK, void *, PSID, ULONG, SID_AND_ATTRIBUTES *, ULONG, HANDLE *);

static void query_sizes(const char *what, HANDLE t)
{
    char buf[256];
    DWORD len = 0xdead;
    BOOL ret;
    unsigned int sizes[] = {0, 4, sizeof(void *), sizeof(void *) + 4, sizeof(void *) + 8, 20, 47, 48, 255};
    int i;

    for (i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++)
    {
        len = 0xdead;
        memset(buf, 0xcc, sizeof(buf));
        ret = GetTokenInformation(t, TokenAppContainerSid, sizes[i] ? buf : NULL, sizes[i], &len);
        printf("%s TokenAppContainerSid size %u: ret %d err %lu len %lu ptr %s\n", what, sizes[i], ret,
               ret ? 0 : GetLastError(), len, ret ? (*(void **)buf ? "set" : "NULL") : "-");
    }
}

int main(int argc, char **argv)
{
    lowbox_t pNtCreateLowBoxToken = (lowbox_t)GetProcAddress(GetModuleHandleA("ntdll.dll"), "NtCreateLowBoxToken");
    HANDLE tok, lb = NULL;
    PSID pkg, cap;
    SID_AND_ATTRIBUTES caps[2];
    char cmd[MAX_PATH + 32], self[MAX_PATH];
    LONG st;
    int c;

    setvbuf(stdout, NULL, _IONBF, 0);
    OpenProcessToken(GetCurrentProcess(), TOKEN_ALL_ACCESS, &tok);
    ConvertStringSidToSidA("S-1-15-2-1-2-3-4-5-6-7", &pkg);
    ConvertStringSidToSidA("S-1-15-3-1", &cap);
    caps[0].Sid = cap; caps[0].Attributes = SE_GROUP_ENABLED;
    caps[1].Sid = cap; caps[1].Attributes = 0;

    if (argc > 1)
    {
        c = atoi(argv[1]);
        switch (c)
        {
        case 0: caps[1].Sid = NULL; st = pNtCreateLowBoxToken(&lb, tok, TOKEN_ALL_ACCESS, NULL, pkg, 2, caps, 0, NULL); break;
        case 1: st = pNtCreateLowBoxToken(&lb, tok, TOKEN_ALL_ACCESS, NULL, pkg, 2, NULL, 0, NULL); break;
        case 2: st = pNtCreateLowBoxToken(&lb, tok, TOKEN_ALL_ACCESS, NULL, pkg, 0x40000001, caps, 0, NULL); break;
        case 3: st = pNtCreateLowBoxToken(&lb, tok, TOKEN_ALL_ACCESS, NULL, pkg, 0, NULL, 0, NULL); break;
        case 4: /* capability with 16 sub-authorities */
        {
            BYTE big[8 + 16 * 4];
            SID *s = (SID *)big;
            memset(big, 0, sizeof(big));
            s->Revision = 1; s->SubAuthorityCount = 16; s->IdentifierAuthority.Value[5] = 15;
            s->SubAuthority[0] = 3;
            caps[1].Sid = s;
            st = pNtCreateLowBoxToken(&lb, tok, TOKEN_ALL_ACCESS, NULL, pkg, 2, caps, 0, NULL);
            break;
        }
        case 5: /* capability with 15 sub-authorities */
        {
            BYTE big[8 + 15 * 4];
            SID *s = (SID *)big;
            memset(big, 0, sizeof(big));
            s->Revision = 1; s->SubAuthorityCount = 15; s->IdentifierAuthority.Value[5] = 15;
            s->SubAuthority[0] = 3;
            caps[1].Sid = s;
            st = pNtCreateLowBoxToken(&lb, tok, TOKEN_ALL_ACCESS, NULL, pkg, 2, caps, 0, NULL);
            break;
        }
        case 6: /* capability S-1-15-3 without sub-authority beyond the RID */
        {
            PSID s;
            ConvertStringSidToSidA("S-1-15-3", &s);
            caps[1].Sid = s;
            st = pNtCreateLowBoxToken(&lb, tok, TOKEN_ALL_ACCESS, NULL, pkg, 2, caps, 0, NULL);
            break;
        }
        case 7: /* capability with revision 2 */
        {
            BYTE b[12] = {2, 1, 0, 0, 0, 0, 0, 15, 3, 0, 0, 0};
            caps[1].Sid = b;
            st = pNtCreateLowBoxToken(&lb, tok, TOKEN_ALL_ACCESS, NULL, pkg, 2, caps, 0, NULL);
            break;
        }
        case 8: st = pNtCreateLowBoxToken(&lb, tok, 0, NULL, pkg, 0, NULL, 0, NULL); break;
        case 9: case 10: case 11: case 12:
        {
            static const ULONG counts[] = {1000, 4096, 4097, 65536};
            ULONG n = counts[c - 9], i;
            SID_AND_ATTRIBUTES *many = malloc(n * sizeof(*many));
            for (i = 0; i < n; i++)
            {
                SID *d = malloc(16);
                d->Revision = 1; d->SubAuthorityCount = 2; memset(&d->IdentifierAuthority, 0, 6);
                d->IdentifierAuthority.Value[5] = 15; d->SubAuthority[0] = 3;
                ((DWORD *)d)[3] = i + 1;
                many[i].Sid = malloc(16); memcpy(many[i].Sid, d, 16); many[i].Attributes = SE_GROUP_ENABLED;
            }
            st = pNtCreateLowBoxToken(&lb, tok, TOKEN_ALL_ACCESS, NULL, pkg, n, many, 0, NULL);
            printf("count %lu: ", n);
            break;
        }
        case 13:
        {
            HANDLE t2;
            OpenProcessToken(GetCurrentProcess(), TOKEN_DUPLICATE | TOKEN_QUERY, &t2);
            st = pNtCreateLowBoxToken(&lb, t2, 0, NULL, pkg, 0, NULL, 0, NULL);
            if (!st)
            {
                OBJECT_BASIC_INFORMATION info;
                NtQueryObject(lb, ObjectBasicInformation, &info, sizeof(info), NULL);
                printf("case 13 granted %08lx\n", info.GrantedAccess);
            }
            break;
        }
        default: return 99;
        }
        printf("case %d: status %08lx handle %p\n", c, st, lb);
        if (c == 8 && !st)
        {
            OBJECT_BASIC_INFORMATION info;
            NtQueryObject(lb, ObjectBasicInformation, &info, sizeof(info), NULL);
            printf("case 8 granted %08lx\n", info.GrantedAccess);
        }
        return 0;
    }

    query_sizes("process", tok);
    st = pNtCreateLowBoxToken(&lb, tok, TOKEN_ALL_ACCESS, NULL, pkg, 1, caps, 0, NULL);
    printf("lowbox %08lx\n", st);
    if (!st) query_sizes("lowbox", lb);

    GetModuleFileNameA(NULL, self, sizeof(self));
    for (c = 0; c <= 13; c++)
    {
        STARTUPINFOA si = {sizeof(si)};
        PROCESS_INFORMATION pi;
        DWORD code;
        sprintf(cmd, "\"%s\" %d", self, c);
        CreateProcessA(self, cmd, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi);
        WaitForSingleObject(pi.hProcess, 10000);
        GetExitCodeProcess(pi.hProcess, &code);
        if (code) printf("case %d: exit %08lx\n", c, code);
        CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
    }
    return 0;
}
