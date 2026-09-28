/* Probe how Windows uses <exe>.config (privatePath) beyond the process default
 * context: RootConfigurationPath reporting, CreateActCtx variants, odd configs.
 * Usage: cfg.exe [2] (round 1 / round 2; sets up C:\t\cfg\*). Needs no manifest
 * resource (mingw gcc 10 embeds none). */
#include <windows.h>
#include <stdio.h>

static const char dep_manifest[] =
"<assembly xmlns=\"urn:schemas-microsoft-com:asm.v1\" manifestVersion=\"1.0\">"
"<dependency><dependentAssembly>"
"<assemblyIdentity type=\"win32\" name=\"privtest\" version=\"1.0.0.0\" processorArchitecture=\"*\"/>"
"</dependentAssembly></dependency></assembly>";
static const char priv_manifest[] =
"<assembly xmlns=\"urn:schemas-microsoft-com:asm.v1\" manifestVersion=\"1.0\">"
"<assemblyIdentity type=\"win32\" name=\"privtest\" version=\"1.0.0.0\" processorArchitecture=\"amd64\"/>"
"</assembly>";
static const char cfg_fmt[] =
"<configuration><windows><assemblyBinding xmlns=\"urn:schemas-microsoft-com:asm.v1\">"
"<probing privatePath=\"%s\"/></assemblyBinding></windows></configuration>";

static void put(const char *path, const char *data)
{
    char dir[MAX_PATH], *p;
    HANDLE f; DWORD w;
    strcpy(dir, path);
    for (p = dir + 3; (p = strchr(p, '\\')); p++) { *p = 0; CreateDirectoryA(dir, NULL); *p = '\\'; }
    f = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    WriteFile(f, data, strlen(data), &w, NULL);
    CloseHandle(f);
}

static void dump(HANDLE h, DWORD flags)
{
    static BYTE ibuf[8192]; ACTIVATION_CONTEXT_DETAILED_INFORMATION *info = (void *)ibuf; SIZE_T size;
    if (!QueryActCtxW(flags, h, NULL, ActivationContextDetailedInformation, info, sizeof(ibuf), &size))
    { printf("    query err=%lu\n", GetLastError()); return; }
    printf("    cfg type=%lu chars=%lu path=%ls\n", info->ulRootConfigurationPathType,
           info->ulRootConfigurationPathChars, info->lpRootConfigurationPath ? info->lpRootConfigurationPath : L"(null)");
    printf("    root type=%lu path=%ls appdir=%ls\n", info->ulRootManifestPathType,
           info->lpRootManifestPath ? info->lpRootManifestPath : L"(null)", info->lpAppDirPath ? info->lpAppDirPath : L"(null)");
    for (DWORD i = 2; i <= info->ulAssemblyCount; i++)
    {
        BYTE buf[4096]; ACTIVATION_CONTEXT_ASSEMBLY_DETAILED_INFORMATION *a = (void *)buf;
        if (!QueryActCtxW(flags, h, &i, AssemblyDetailedInformationInActivationContext, a, sizeof(buf), &size))
        { printf("    assembly %lu err=%lu\n", i, GetLastError()); continue; }
        printf("    asm %lu: %ls dir=%ls\n", i, a->lpAssemblyManifestPath ? a->lpAssemblyManifestPath : L"(null)",
               a->lpAssemblyDirectoryName ? a->lpAssemblyDirectoryName : L"(null)");
    }
}

static void create(const char *label, DWORD flags, const char *src, HMODULE mod)
{
    WCHAR srcW[MAX_PATH]; ACTCTXW ctx = {sizeof(ctx)}; HANDLE h;
    MultiByteToWideChar(CP_ACP, 0, src ? src : "", -1, srcW, MAX_PATH);
    ctx.dwFlags = flags; ctx.lpSource = src ? srcW : NULL; ctx.hModule = mod;
    ctx.lpResourceName = MAKEINTRESOURCEW(1);
    h = CreateActCtxW(&ctx);
    printf("%s: %s\n", label, h == INVALID_HANDLE_VALUE ? "FAIL" : "ok");
    if (h == INVALID_HANDLE_VALUE) { printf("    err=%lu\n", GetLastError()); return; }
    dump(h, 0);
    ReleaseActCtx(h);
}

static void launch(const char *dir)
{
    char path[MAX_PATH], cmd[MAX_PATH + 16]; STARTUPINFOA si = {sizeof(si)}; PROCESS_INFORMATION pi; DWORD code;
    snprintf(path, sizeof(path), "%s\\child.exe", dir);
    snprintf(cmd, sizeof(cmd), "\"%s\" child", path);
    printf("launch %s:\n", dir); fflush(stdout);
    if (!CreateProcessA(path, cmd, NULL, NULL, FALSE, 0, NULL, dir, &si, &pi))
    { printf("    CreateProcess err=%lu\n", GetLastError()); return; }
    WaitForSingleObject(pi.hProcess, 10000); GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
}

/* exe dir with child.exe + child.exe.manifest (+ optional config) */
static void child_case(const char *name, const char *cfg, const char *privdir)
{
    char dir[MAX_PATH], p[MAX_PATH], self[MAX_PATH];
    snprintf(dir, sizeof(dir), "C:\\t\\cfg\\%s", name);
    snprintf(p, sizeof(p), "%s\\child.exe.manifest", dir); put(p, dep_manifest);
    GetModuleFileNameA(NULL, self, MAX_PATH);
    snprintf(p, sizeof(p), "%s\\child.exe", dir); CopyFileA(self, p, FALSE);
    if (cfg) { snprintf(p, sizeof(p), "%s\\child.exe.config", dir); put(p, cfg); }
    if (privdir) { snprintf(p, sizeof(p), "%s\\%s\\privtest.manifest", dir, privdir); put(p, priv_manifest); }
    launch(dir);
}

int main(int argc, char **argv)
{
    char cfg[1024], p[MAX_PATH];

    setvbuf(stdout, NULL, _IONBF, 0);
    if (argc > 1 && !strcmp(argv[1], "child"))
    {
        printf("  default (flags 0):\n"); dump(NULL, 0);
        printf("  USE_ACTIVE:\n"); dump(NULL, QUERY_ACTCTX_FLAG_USE_ACTIVE_ACTCTX);
        /* CreateActCtx from own module resource: none, so use lpSource manifest */
        create("  create own manifest file (child.exe.manifest)", 0, "child.exe.manifest", NULL);
        return 0;
    }

    if (argc > 1 && !strcmp(argv[1], "2")) goto round2;
    snprintf(cfg, sizeof(cfg), cfg_fmt, "sub");
    child_case("sub", cfg, "sub");
    child_case("nosub_cfg", cfg, NULL);  /* config present, dependency in appdir */
    snprintf(p, sizeof(p), "C:\\t\\cfg\\nosub_cfg\\privtest.manifest"); put(p, priv_manifest);
    launch("C:\\t\\cfg\\nosub_cfg");
    child_case("emptycfg", "<configuration/>", "privtest");      /* config w/o probing, dep in appdir\privtest */
    child_case("badxml", "<configuration><windows>", "privtest"); /* malformed config */
    child_case("garbage", "not xml at all", "privtest");
    snprintf(cfg, sizeof(cfg), cfg_fmt, "C:\\t\\cfg\\abslib");
    put("C:\\t\\cfg\\abslib\\privtest.manifest", priv_manifest);
    child_case("abs", cfg, NULL);
    snprintf(cfg, sizeof(cfg), cfg_fmt, "sub/fwd");
    child_case("fwdslash", cfg, "sub\\fwd");
    snprintf(cfg, sizeof(cfg), cfg_fmt, " sub ; b ");
    child_case("spaces", cfg, "sub");
    snprintf(cfg, sizeof(cfg), cfg_fmt, "sub\\");
    child_case("trailing", cfg, "sub");
    /* two probing elements */
    child_case("twoprobing", "<configuration><windows><assemblyBinding xmlns=\"urn:schemas-microsoft-com:asm.v1\">"
               "<probing privatePath=\"a\"/><probing privatePath=\"sub\"/></assemblyBinding></windows></configuration>", "sub");
    /* uppercase/other element names */
    child_case("case", "<Configuration><Windows><AssemblyBinding xmlns=\"urn:schemas-microsoft-com:asm.v1\">"
               "<Probing privatePath=\"sub\"/></AssemblyBinding></Windows></Configuration>", "sub");
    /* extra wrapper element around windows */
    child_case("nested", "<configuration><foo><windows><assemblyBinding xmlns=\"urn:schemas-microsoft-com:asm.v1\">"
               "<probing privatePath=\"sub\"/></assemblyBinding></windows></foo></configuration>", "sub");
    /* both runtime and windows */
    child_case("rt_and_win", "<?xml version=\"1.0\"?><configuration><runtime><foo/></runtime><windows><assemblyBinding xmlns=\"urn:schemas-microsoft-com:asm.v1\">"
               "<probing privatePath=\"sub\"/></assemblyBinding></windows></configuration>", "sub");

    /* CreateActCtx variants in parent (our own exe has no config) */
    put("C:\\t\\cfg\\ca\\app.manifest", dep_manifest);
    put("C:\\t\\cfg\\ca\\sub\\privtest.manifest", priv_manifest);
    snprintf(cfg, sizeof(cfg), cfg_fmt, "sub");
    put("C:\\t\\cfg\\ca\\app.manifest.config", cfg);
    put("C:\\t\\cfg\\ca\\app.config", cfg);
    put("C:\\t\\cfg\\ca\\child.exe.config", cfg);
    CopyFileA("C:\\t\\cfg\\sub\\child.exe", "C:\\t\\cfg\\ca\\child.exe", FALSE);
    put("C:\\t\\cfg\\ca\\child.exe.manifest", dep_manifest);
    create("create app.manifest", 0, "C:\\t\\cfg\\ca\\app.manifest", NULL);
    create("create child.exe res1 (external manifest)", ACTCTX_FLAG_RESOURCE_NAME_VALID, "C:\\t\\cfg\\ca\\child.exe", NULL);
    create("create child.exe.manifest", 0, "C:\\t\\cfg\\ca\\child.exe.manifest", NULL);
    /* config for our own exe, then CreateActCtx from a manifest elsewhere */
    GetModuleFileNameA(NULL, p, MAX_PATH); strcat(p, ".config"); put(p, cfg);
    create("create app.manifest with own cfg", 0, "C:\\t\\cfg\\ca\\app.manifest", NULL);
    DeleteFileA(p);
    return 0;

round2:
    /* no config file at all: is RootConfigurationPath still reported? */
    child_case("r2_nocfg", NULL, NULL);
    snprintf(p, sizeof(p), "C:\\t\\cfg\\r2_nocfg\\privtest.manifest"); put(p, priv_manifest);
    launch("C:\\t\\cfg\\r2_nocfg");
    /* bad configs with the dependency in appdir: config error fatal or ignored? */
    snprintf(cfg, sizeof(cfg), cfg_fmt, "C:\\t\\cfg\\abslib");
    child_case("r2_abs", cfg, "privtest");
    snprintf(cfg, sizeof(cfg), cfg_fmt, "sub\\");
    child_case("r2_trailing", cfg, "privtest");
    snprintf(cfg, sizeof(cfg), cfg_fmt, " sub ; b ");
    child_case("r2_spaces", cfg, "privtest");
    child_case("r2_badxml", "<configuration><windows>", "privtest");
    child_case("r2_nsonly", "<configuration><windows><assemblyBinding><probing privatePath=\"sub\"/></assemblyBinding></windows></configuration>", "privtest");
    snprintf(cfg, sizeof(cfg), cfg_fmt, "..\\up");
    child_case("r2_up", cfg, "privtest");
    snprintf(cfg, sizeof(cfg), cfg_fmt, "");
    child_case("r2_empty", cfg, "privtest");
    /* trailing slash, dependency only there */
    snprintf(cfg, sizeof(cfg), cfg_fmt, "sub/");
    child_case("r2_trailfwd", cfg, "sub");
    snprintf(cfg, sizeof(cfg), cfg_fmt, "sub;");
    child_case("r2_trailsemi", cfg, "sub");
    snprintf(cfg, sizeof(cfg), cfg_fmt, ".\\sub");
    child_case("r2_dot", cfg, "sub");
    /* two probing elements: last wins or both? */
    child_case("r2_twoprobing", "<configuration><windows><assemblyBinding xmlns=\"urn:schemas-microsoft-com:asm.v1\">"
               "<probing privatePath=\"sub\"/><probing privatePath=\"a\"/></assemblyBinding></windows></configuration>", "sub");
    /* two assemblyBinding */
    child_case("r2_twobinding", "<configuration><windows><assemblyBinding xmlns=\"urn:schemas-microsoft-com:asm.v1\">"
               "<probing privatePath=\"a\"/></assemblyBinding><assemblyBinding xmlns=\"urn:schemas-microsoft-com:asm.v1\">"
               "<probing privatePath=\"sub\"/></assemblyBinding></windows></configuration>", "sub");
    /* other elements in assemblyBinding / windows, comments, xml decl, BOM-less utf-8 */
    child_case("r2_extra", "<?xml version=\"1.0\" encoding=\"utf-8\"?><!-- c --><configuration><startup><supportedRuntime version=\"v4.0\"/></startup>"
               "<windows><foo/><assemblyBinding xmlns=\"urn:schemas-microsoft-com:asm.v1\"><dependentAssembly><assemblyIdentity name=\"x\"/></dependentAssembly>"
               "<probing privatePath=\"sub\" foo=\"bar\"/></assemblyBinding></windows></configuration>", "sub");
    /* bad config but manifest with no dependencies */
    put("C:\\t\\cfg\\r2_nodeps\\child.exe.manifest", priv_manifest);
    GetModuleFileNameA(NULL, p, MAX_PATH); CopyFileA(p, "C:\\t\\cfg\\r2_nodeps\\child.exe", FALSE);
    put("C:\\t\\cfg\\r2_nodeps\\child.exe.config", "garbage");
    launch("C:\\t\\cfg\\r2_nodeps");
    /* no manifest, bad config */
    CopyFileA(p, "C:\\t\\cfg\\r2_nomani\\child.exe", FALSE);
    put("C:\\t\\cfg\\r2_nomani\\child.exe.config", "garbage");
    launch("C:\\t\\cfg\\r2_nomani");
    /* CreateActCtx: config/probing base when appdir != manifest dir; odd manifest names */
    snprintf(cfg, sizeof(cfg), cfg_fmt, "sub");
    put("C:\\t\\cfg\\cb\\m\\app.manifest", dep_manifest);
    put("C:\\t\\cfg\\cb\\m\\app.config", cfg);
    put("C:\\t\\cfg\\cb\\m\\sub\\privtest.manifest", priv_manifest);
    create("create m\\app.manifest", 0, "C:\\t\\cfg\\cb\\m\\app.manifest", NULL);
    {
        WCHAR srcW[] = L"C:\\t\\cfg\\cb\\m\\app.manifest", appW[] = L"C:\\t\\cfg\\cb\\a\\";
        ACTCTXW ctx = {sizeof(ctx)}; HANDLE h;
        ctx.dwFlags = ACTCTX_FLAG_APPLICATION_NAME_VALID; ctx.lpSource = srcW; ctx.lpApplicationName = appW;
        h = CreateActCtxW(&ctx);
        printf("create m\\app.manifest appname=a\\: %s err=%lu\n", h == INVALID_HANDLE_VALUE ? "FAIL" : "ok", GetLastError());
        if (h != INVALID_HANDLE_VALUE) { dump(h, 0); ReleaseActCtx(h); }
        put("C:\\t\\cfg\\cb\\a\\sub\\privtest.manifest", priv_manifest);
        put("C:\\t\\cfg\\cb\\a\\app.config", "<configuration/>");
        DeleteFileA("C:\\t\\cfg\\cb\\m\\sub\\privtest.manifest");
        h = CreateActCtxW(&ctx);
        printf("create m\\app.manifest appname=a\\, priv in a\\sub: %s err=%lu\n", h == INVALID_HANDLE_VALUE ? "FAIL" : "ok", GetLastError());
        if (h != INVALID_HANDLE_VALUE) { dump(h, 0); ReleaseActCtx(h); }
        put("C:\\t\\cfg\\cb\\m\\sub\\privtest.manifest", priv_manifest);
    }
    put("C:\\t\\cfg\\cb\\m\\foo.xml", dep_manifest);
    put("C:\\t\\cfg\\cb\\m\\foo.config", cfg);
    create("create m\\foo.xml", 0, "C:\\t\\cfg\\cb\\m\\foo.xml", NULL);
    put("C:\\t\\cfg\\cb\\m\\noext", dep_manifest);
    put("C:\\t\\cfg\\cb\\m\\noext.config", cfg);
    create("create m\\noext", 0, "C:\\t\\cfg\\cb\\m\\noext", NULL);
    put("C:\\t\\cfg\\cb\\m\\bad.manifest", dep_manifest);
    put("C:\\t\\cfg\\cb\\m\\bad.config", "garbage");
    create("create m\\bad.manifest (garbage cfg)", 0, "C:\\t\\cfg\\cb\\m\\bad.manifest", NULL);
    put("C:\\t\\cfg\\cb\\m\\nocfg.manifest", dep_manifest);
    create("create m\\nocfg.manifest", 0, "C:\\t\\cfg\\cb\\m\\nocfg.manifest", NULL);
    /* process default of this exe (no manifest) */
    printf("self default:\n"); dump(NULL, 0);
    return 0;
}
