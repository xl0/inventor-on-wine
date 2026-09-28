/* SysLink markup parsing with attributes other than href/id (Autodesk's
 * licensing dialog uses <a target="_blank" href="...">). Per case: links
 * found via LM_GETITEM (index, id, url) and LM_GETIDEALSIZE width (markup
 * shown literally = wider than the plain-text reference). */
#define _WIN32_WINNT 0x0600
#include <windows.h>
#include <commctrl.h>
#include <stdio.h>

static const WCHAR *cases[] = {
    L"How do I fix this?",
    L"<a href=\"https://u/\">How do I fix this?</a>",
    L"<a target=\"_blank\" href=\"https://u/\">How do I fix this?</a>",
    L"<a href=\"https://u/\" target=\"_blank\">How do I fix this?</a>",
    L"<a id=\"i\" target=\"_blank\">How do I fix this?</a>",
    L"<a foo>How do I fix this?</a>",
    L"<a  href=\"https://u/\">How do I fix this?</a>",
    L"<a href='https://u/'>How do I fix this?</a>",
    L"<a href=https://u/>How do I fix this?</a>",
    L"x <a target=\"_blank\" href=\"https://u/\">How</a> y <a href=\"v\">z</a>",
};

int main(void)
{
    ACTCTXA ctx = {sizeof(ctx)};
    INITCOMMONCONTROLSEX icc = {sizeof(icc), ICC_LINK_CLASS};
    char manifest[MAX_PATH];
    BOOL (WINAPI *init)(const INITCOMMONCONTROLSEX *);
    ULONG_PTR cookie;
    FILE *f;
    int i, j;

    GetTempPathA(MAX_PATH, manifest);
    strcat(manifest, "syslinkattr.manifest");
    f = fopen(manifest, "w");
    fputs("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
          "<assembly xmlns=\"urn:schemas-microsoft-com:asm.v1\" manifestVersion=\"1.0\">"
          "<dependency><dependentAssembly><assemblyIdentity type=\"win32\" "
          "name=\"Microsoft.Windows.Common-Controls\" version=\"6.0.0.0\" processorArchitecture=\"*\" "
          "publicKeyToken=\"6595b64144ccf1df\" language=\"*\"/></dependentAssembly></dependency></assembly>", f);
    fclose(f);
    ctx.lpSource = manifest;
    ActivateActCtx(CreateActCtxA(&ctx), &cookie);
    /* not a static import: that would bind comctl32 v5, which has no SysLink */
    init = (void *)GetProcAddress(LoadLibraryA("comctl32.dll"), "InitCommonControlsEx");
    if (!init || !init(&icc)) { printf("InitCommonControlsEx failed\n"); return 1; }

    for (i = 0; i < ARRAYSIZE(cases); i++)
    {
        HWND w = CreateWindowExW(0, WC_LINK, cases[i], WS_POPUP, 0, 0, 600, 40, NULL, NULL, NULL, NULL);
        SIZE sz = {0};
        if (!w) { printf("case %d: CreateWindow failed %lu\n", i, GetLastError()); return 1; }
        SendMessageW(w, LM_GETIDEALHEIGHT, 600 /* = LM_GETIDEALSIZE */, (LPARAM)&sz);
        printf("case %d: %ls\n  ideal width %ld,", i, cases[i], sz.cx);
        for (j = 0; ; j++)
        {
            LITEM it = {LIF_ITEMINDEX | LIF_ITEMID | LIF_URL, j};
            if (!SendMessageW(w, LM_GETITEM, 0, (LPARAM)&it)) break;
            printf(" link %d id '%ls' url '%ls';", j, it.szID, it.szUrl);
        }
        printf(" %d links\n", j);
        DestroyWindow(w);
    }
    return 0;
}
