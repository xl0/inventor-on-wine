/* Print the manifest path of the 'privtest' dependency in the process default
 * activation context ("-" if it was not resolved). Exit 42 when found. */
#include <windows.h>
#include <stdio.h>
int main(void)
{
    const DWORD flags = QUERY_ACTCTX_FLAG_USE_ACTIVE_ACTCTX;
    static BYTE ibuf[8192]; ACTIVATION_CONTEXT_DETAILED_INFORMATION *info = (void *)ibuf; SIZE_T size;
    if (!QueryActCtxW(flags, NULL, NULL, ActivationContextDetailedInformation, info, sizeof(ibuf), &size))
    { printf("- (query err=%lu)\n", GetLastError()); return 1; }
    for (DWORD i = 2; i <= info->ulAssemblyCount; i++)
    {
        BYTE buf[4096]; ACTIVATION_CONTEXT_ASSEMBLY_DETAILED_INFORMATION *a = (void *)buf;
        if (!QueryActCtxW(flags, NULL, &i, AssemblyDetailedInformationInActivationContext, a, sizeof(buf), &size))
        { printf("  (assembly %lu query err=%lu)\n", i, GetLastError()); continue; }
        if (a->lpAssemblyEncodedAssemblyIdentity && wcsstr(a->lpAssemblyEncodedAssemblyIdentity, L"privtest"))
        { printf("%ls\n", a->lpAssemblyManifestPath ? a->lpAssemblyManifestPath : L"(null)"); return 42; }
    }
    printf("- (%lu assemblies)\n", info->ulAssemblyCount); return 1;
}
