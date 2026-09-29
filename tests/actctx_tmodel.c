/* Threading model an activation context records for <comClass threadingModel="X">,
 * for several spellings (063: Inventor's FEA manifest says "free").
 * Prints "value -> model" (1 Apartment, 2 Free, 3 No, 4 Both, 5 Neutral).
 * Build: x86_64-w64-mingw32-gcc -O2 -o actctx_tmodel.exe actctx_tmodel.c */
#include <windows.h>
#include <stdio.h>

static const char *values[] = { "Apartment", "apartment", "Free", "free", "FREE", "Both", "both", "Neutral", "neutral", "", "bogus" };

int main(void)
{
    char dir[MAX_PATH], path[MAX_PATH];
    unsigned i;
    GetTempPathA(MAX_PATH, dir);
    for (i = 0; i < ARRAYSIZE(values); i++)
    {
        static const GUID clsid = {0xe4e2fe54, 0x5a90, 0x4006, {0xac, 0x58, 0x9e, 0x11, 0xde, 0x19, 0x81, 0x7b}};
        ACTCTX_SECTION_KEYED_DATA data = { sizeof(data) };
        ACTCTXA ctx = { sizeof(ctx) };
        ULONG_PTR cookie;
        HANDLE h;
        FILE *f;

        sprintf(path, "%stmodel%u.manifest", dir, i);
        f = fopen(path, "w");
        fprintf(f, "<assembly xmlns=\"urn:schemas-microsoft-com:asm.v1\" manifestVersion=\"1.0\">"
                "<assemblyIdentity type=\"win32\" name=\"tmodel\" version=\"1.0.0.0\"/>"
                "<file name=\"tmodel.dll\"><comClass clsid=\"{E4E2FE54-5A90-4006-AC58-9E11DE19817B}\""
                " threadingModel=\"%s\"/></file></assembly>", values[i]);
        fclose(f);
        ctx.lpSource = path;
        h = CreateActCtxA(&ctx);
        if (h == INVALID_HANDLE_VALUE) { printf("'%s' -> CreateActCtx error %lu\n", values[i], GetLastError()); continue; }
        ActivateActCtx(h, &cookie);
        if (FindActCtxSectionGuid(0, NULL, ACTIVATION_CONTEXT_SECTION_COM_SERVER_REDIRECTION, &clsid, &data))
            printf("'%s' -> %lu\n", values[i], ((ULONG *)data.lpData)[2]);
        else
            printf("'%s' -> FindActCtxSectionGuid error %lu\n", values[i], GetLastError());
        DeactivateActCtx(0, cookie);
        ReleaseActCtx(h);
        DeleteFileA(path);
    }
    return 0;
}
