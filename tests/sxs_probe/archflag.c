/* Does CreateActCtx honor ACTCTX_FLAG_PROCESSOR_ARCHITECTURE_VALID when resolving
 * processorArchitecture="*" dependencies? Private assembly 'privtest' exists as
 * x86 (dir x86) or amd64 (dir amd64); try each wProcessorArchitecture. */
#include <windows.h>
#include <stdio.h>
static void put(const char *path, const char *s) { FILE *f = fopen(path, "wb"); fputs(s, f); fclose(f); }
int main(void)
{
    static const char *arches[] = {"x86", "amd64"};
    static const WORD pa[] = {PROCESSOR_ARCHITECTURE_INTEL, PROCESSOR_ARCHITECTURE_AMD64};
    char path[MAX_PATH], buf[1024];
    CreateDirectoryA("C:\\t\\004", NULL);
    for (int i = 0; i < 2; i++)
    {
        snprintf(path, sizeof(path), "C:\\t\\004\\%s", arches[i]); CreateDirectoryA(path, NULL);
        snprintf(path, sizeof(path), "C:\\t\\004\\%s\\root.manifest", arches[i]);
        put(path, "<?xml version=\"1.0\"?><assembly xmlns=\"urn:schemas-microsoft-com:asm.v1\" manifestVersion=\"1.0\">"
            "<dependency><dependentAssembly><assemblyIdentity type=\"win32\" name=\"privtest\" version=\"1.0.0.0\" processorArchitecture=\"*\"/>"
            "</dependentAssembly></dependency></assembly>");
        snprintf(path, sizeof(path), "C:\\t\\004\\%s\\privtest.manifest", arches[i]);
        snprintf(buf, sizeof(buf), "<?xml version=\"1.0\"?><assembly xmlns=\"urn:schemas-microsoft-com:asm.v1\" manifestVersion=\"1.0\">"
                 "<assemblyIdentity type=\"win32\" name=\"privtest\" version=\"1.0.0.0\" processorArchitecture=\"%s\"/></assembly>", arches[i]);
        put(path, buf);
        for (int j = -1; j < 2; j++)
        {
            ACTCTXA ctx = {sizeof(ctx)}; HANDLE h;
            snprintf(path, sizeof(path), "C:\\t\\004\\%s\\root.manifest", arches[i]);
            ctx.lpSource = path;
            if (j >= 0) { ctx.dwFlags = ACTCTX_FLAG_PROCESSOR_ARCHITECTURE_VALID; ctx.wProcessorArchitecture = pa[j]; }
            h = CreateActCtxA(&ctx);
            printf("private %-5s flag %-5s: %s %lu\n", arches[i], j < 0 ? "none" : arches[j],
                   h == INVALID_HANDLE_VALUE ? "fail" : "ok", h == INVALID_HANDLE_VALUE ? GetLastError() : 0);
            if (h != INVALID_HANDLE_VALUE) ReleaseActCtx(h);
        }
    }
    return 0;
}
