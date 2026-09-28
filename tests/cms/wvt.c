/* WinVerifyTrust(GENERIC_VERIFY_V2) on files, no revocation; dumps signer state. Usage: wvt FILE... */
#include <windows.h>
#include <wintrust.h>
#include <softpub.h>
#include <stdio.h>

int main(int argc, char **argv)
{
    int a;
    for (a = 1; a < argc; a++)
    {
        WCHAR path[MAX_PATH];
        WINTRUST_FILE_INFO fi = { sizeof(fi) };
        WINTRUST_DATA td = { sizeof(td) };
        GUID action = WINTRUST_ACTION_GENERIC_VERIFY_V2;
        CRYPT_PROVIDER_DATA *pd;
        LONG hr;

        MultiByteToWideChar(CP_ACP, 0, argv[a], -1, path, MAX_PATH);
        fi.pcwszFilePath = path;
        td.dwUIChoice = WTD_UI_NONE;
        td.fdwRevocationChecks = WTD_REVOKE_NONE;
        td.dwUnionChoice = WTD_CHOICE_FILE;
        td.pFile = &fi;
        td.dwStateAction = WTD_STATEACTION_VERIFY;
        hr = WinVerifyTrust(NULL, &action, &td);
        printf("%s: %08lx\n", argv[a], hr);
        pd = WTHelperProvDataFromStateData(td.hWVTStateData);
        if (pd)
        {
            DWORD i;
            for (i = 0; i < TRUSTERROR_MAX_STEPS; i++)
                if (pd->padwTrustStepErrors[i]) printf("  step %lu error %08lx\n", i, pd->padwTrustStepErrors[i]);
            for (i = 0; i < pd->csSigners; i++)
            {
                SYSTEMTIME st;
                FileTimeToSystemTime(&pd->pasSigners[i].sftVerifyAsOf, &st);
                printf("  signer %lu error %08lx asof %04d-%02d-%02d %02d:%02d counters %lu\n", i,
                        pd->pasSigners[i].dwError, st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute,
                        pd->pasSigners[i].csCounterSigners);
            }
        }
        td.dwStateAction = WTD_STATEACTION_CLOSE;
        WinVerifyTrust(NULL, &action, &td);
    }
    return 0;
}
