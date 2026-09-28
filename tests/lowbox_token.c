/* NtCreateLowBoxToken / kernelbase CreateAppContainerToken ground truth (issue 026).
 * Build: x86_64-w64-mingw32-gcc -O2 -o lowbox_token.exe lowbox_token.c -lntdll -ladvapi32 */
#include <windows.h>
#include <winternl.h>
#include <sddl.h>
#include <stdio.h>

typedef NTSTATUS (WINAPI *pNtCreateLowBoxToken_t)(HANDLE *, HANDLE, ACCESS_MASK, OBJECT_ATTRIBUTES *, PSID, ULONG,
                                                  SID_AND_ATTRIBUTES *, ULONG, HANDLE *);
typedef BOOL (WINAPI *pCreateAppContainerToken_t)(HANDLE, SECURITY_CAPABILITIES *, HANDLE *);
typedef NTSTATUS (WINAPI *pNtQueryObject_t)(HANDLE, int, void *, ULONG, ULONG *);

static pNtQueryObject_t pNtQueryObject;

static void dump(const char *what, HANDLE t)
{
    char buf[1024]; DWORD len, v; TOKEN_TYPE type; ULONG ob[14];
    char *s;
    if (!t) { printf("%s: NULL handle\n", what); return; }
    pNtQueryObject(t, 0, ob, sizeof(ob), NULL);
    GetTokenInformation(t, TokenType, &type, sizeof(type), &len);
    printf("%s: access %08lx type %d", what, ob[1], type);
    if (type == TokenImpersonation && GetTokenInformation(t, TokenImpersonationLevel, &v, sizeof(v), &len))
        printf(" level %lu", v);
    v = 0xdead;
    printf(" isappcontainer %d/%lu", GetTokenInformation(t, TokenIsAppContainer, &v, sizeof(v), &len), v);
    if (GetTokenInformation(t, TokenAppContainerSid, buf, sizeof(buf), &len))
    {
        PSID sid = ((TOKEN_APPCONTAINER_INFORMATION *)buf)->TokenAppContainer;
        if (sid && ConvertSidToStringSidA(sid, &s)) { printf(" acsid %s", s); LocalFree(s); }
        else printf(" acsid NULL len %lu", len);
    }
    else printf(" acsid err %lu", GetLastError());
    if (GetTokenInformation(t, TokenCapabilities, buf, sizeof(buf), &len))
        printf(" caps %lu", ((TOKEN_GROUPS *)buf)->GroupCount);
    else printf(" caps err %lu", GetLastError());
    if (GetTokenInformation(t, TokenIntegrityLevel, buf, sizeof(buf), &len) &&
        ConvertSidToStringSidA(((TOKEN_MANDATORY_LABEL *)buf)->Label.Sid, &s)) { printf(" il %s", s); LocalFree(s); }
    if (GetTokenInformation(t, TokenAppContainerNumber, &v, sizeof(v), &len)) printf(" acnum %lu", v);
    else printf(" acnum err %lu", GetLastError());
    printf("\n");
}

int main(void)
{
    HMODULE ntdll = GetModuleHandleA("ntdll.dll"), kb = GetModuleHandleA("kernelbase.dll");
    pNtCreateLowBoxToken_t pNtCreateLowBoxToken = (void *)GetProcAddress(ntdll, "NtCreateLowBoxToken");
    pCreateAppContainerToken_t pCreateAppContainerToken = (void *)GetProcAddress(kb, "CreateAppContainerToken");
    HANDLE proc, tok = NULL, imp;
    PSID acsid, cap;
    SID_AND_ATTRIBUTES caps[1];
    SECURITY_CAPABILITIES sc;
    NTSTATUS status;
    BOOL ret;

    pNtQueryObject = (void *)GetProcAddress(ntdll, "NtQueryObject");
    printf("NtCreateLowBoxToken %p CreateAppContainerToken %p\n", pNtCreateLowBoxToken, pCreateAppContainerToken);
    ConvertStringSidToSidA("S-1-15-2-3251537155-1984446955-2931258699-841473695-1938553385-924012159-129201922", &acsid);
    ConvertStringSidToSidA("S-1-15-3-1", &cap); /* internetClient */
    caps[0].Sid = cap; caps[0].Attributes = SE_GROUP_ENABLED;
    OpenProcessToken(GetCurrentProcess(), TOKEN_ALL_ACCESS, &proc);
    dump("process", proc);

    status = pNtCreateLowBoxToken(&tok, proc, TOKEN_ALL_ACCESS, NULL, acsid, 1, caps, 0, NULL);
    printf("NtCreateLowBoxToken(ALL) %08lx\n", status); dump("lowbox", tok); if (tok) CloseHandle(tok);
    tok = NULL;
    status = pNtCreateLowBoxToken(&tok, proc, TOKEN_QUERY, NULL, acsid, 0, NULL, 0, NULL);
    printf("NtCreateLowBoxToken(QUERY, 0 caps) %08lx\n", status); dump("lowbox", tok);
    {
        HANDLE t2 = NULL;
        status = pNtCreateLowBoxToken(&t2, tok, TOKEN_QUERY, NULL, acsid, 0, NULL, 0, NULL);
        printf("NtCreateLowBoxToken(from lowbox w/o DUPLICATE) %08lx\n", status); if (t2) CloseHandle(t2);
    }
    if (tok) CloseHandle(tok);
    tok = NULL;
    status = pNtCreateLowBoxToken(&tok, proc, TOKEN_ALL_ACCESS, NULL, NULL, 0, NULL, 0, NULL);
    printf("NtCreateLowBoxToken(NULL sid) %08lx %p\n", status, tok);
    tok = NULL;
    status = pNtCreateLowBoxToken(&tok, proc, TOKEN_ALL_ACCESS, NULL, cap, 0, NULL, 0, NULL);
    printf("NtCreateLowBoxToken(capability sid as package) %08lx %p\n", status, tok);
    if (tok) CloseHandle(tok);

    DuplicateTokenEx(proc, TOKEN_ALL_ACCESS, NULL, SecurityImpersonation, TokenImpersonation, &imp);
    tok = NULL;
    status = pNtCreateLowBoxToken(&tok, imp, TOKEN_ALL_ACCESS, NULL, acsid, 0, NULL, 0, NULL);
    printf("NtCreateLowBoxToken(impersonation src) %08lx\n", status); dump("lowbox-imp", tok); if (tok) CloseHandle(tok);

    if (pCreateAppContainerToken)
    {
        sc.AppContainerSid = acsid; sc.Capabilities = caps; sc.CapabilityCount = 1; sc.Reserved = 0;
        tok = NULL;
        SetLastError(0xdeadbeef);
        ret = pCreateAppContainerToken(proc, &sc, &tok);
        printf("CreateAppContainerToken %d err %lu\n", ret, GetLastError()); dump("appcontainer", tok);
        if (tok) CloseHandle(tok);
        tok = NULL;
        SetLastError(0xdeadbeef);
        ret = pCreateAppContainerToken(NULL, &sc, &tok);
        printf("CreateAppContainerToken(NULL token) %d err %lu\n", ret, GetLastError()); dump("appcontainer-null", tok);
        if (tok) CloseHandle(tok);
        tok = NULL;
        SetLastError(0xdeadbeef);
        ret = pCreateAppContainerToken(imp, &sc, &tok);
        printf("CreateAppContainerToken(imp) %d err %lu\n", ret, GetLastError()); dump("appcontainer-imp", tok);
    }
    return 0;
}
