/* Mirrors the msix SDK Win32 SignatureValidator origin checks on an AppxSignature.p7x and dumps
 * WinVerifyTrust(WTD_CHOICE_BLOB) signer state (issue 008). Usage: p7x_winverifytrust.exe FILE.p7x */
#include <windows.h>
#include <wincrypt.h>
#include <wintrust.h>
#include <softpub.h>
#include <stdio.h>

int main(int argc, char **argv)
{
    FILE *f = fopen(argv[1], "rb");
    static BYTE buf[1 << 21];
    DWORD size = fread(buf, 1, sizeof(buf), f), i;
    BYTE *p7s = buf + 4;
    DWORD p7s_size = size - 4;
    CRYPT_DATA_BLOB blob = { p7s_size, p7s };
    HCERTSTORE store;
    HCRYPTMSG msg;
    CMSG_SIGNER_INFO *si;
    CERT_INFO ci;
    PCCERT_CONTEXT cert;
    CERT_CHAIN_PARA para = { sizeof(para) };
    PCCERT_CHAIN_CONTEXT chain;
    CERT_CHAIN_POLICY_PARA pp = { sizeof(pp) };
    CERT_CHAIN_POLICY_STATUS ps = { sizeof(ps) };
    WINTRUST_BLOB_INFO bi = { sizeof(bi) };
    WINTRUST_DATA td = { sizeof(td) };
    GUID action = WINTRUST_ACTION_GENERIC_VERIFY_V2;
    GUID p7x = { 0x5598cff1, 0x68db, 0x4340, { 0xb5, 0x7f, 0x1c, 0xac, 0xf8, 0x8c, 0x9a, 0x51 } };
    BOOL ret;
    LONG hr;

    fclose(f);
    printf("size %lu magic %08lx\n", size, *(DWORD *)buf);
    ret = CryptQueryObject(CERT_QUERY_OBJECT_BLOB, &blob, CERT_QUERY_CONTENT_FLAG_PKCS7_SIGNED,
            CERT_QUERY_FORMAT_FLAG_BINARY, 0, NULL, NULL, NULL, &store, &msg, NULL);
    printf("CryptQueryObject %d\n", ret);
    CryptMsgGetParam(msg, CMSG_SIGNER_INFO_PARAM, 0, NULL, &size);
    si = malloc(size);
    CryptMsgGetParam(msg, CMSG_SIGNER_INFO_PARAM, 0, si, &size);
    ci.Issuer = si->Issuer;
    ci.SerialNumber = si->SerialNumber;
    cert = CertGetSubjectCertificateFromStore(store, X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, &ci);
    printf("signing cert %p\n", cert);
    para.RequestedUsage.dwType = USAGE_MATCH_TYPE_AND;
    ret = CertGetCertificateChain(HCCE_LOCAL_MACHINE, cert, NULL, store, &para,
            CERT_CHAIN_CACHE_ONLY_URL_RETRIEVAL, NULL, &chain);
    printf("CertGetCertificateChain %d error %08lx info %08lx elements %lu\n", ret,
            chain->TrustStatus.dwErrorStatus, chain->TrustStatus.dwInfoStatus, chain->rgpChain[0]->cElement);
    for (i = 0; i < chain->rgpChain[0]->cElement; i++)
    {
        char name[256];
        CertGetNameStringA(chain->rgpChain[0]->rgpElement[i]->pCertContext, CERT_NAME_SIMPLE_DISPLAY_TYPE, 0, NULL, name, sizeof(name));
        printf("  [%lu] %s error %08lx info %08lx\n", i, name,
                chain->rgpChain[0]->rgpElement[i]->TrustStatus.dwErrorStatus,
                chain->rgpChain[0]->rgpElement[i]->TrustStatus.dwInfoStatus);
    }
    ret = CertVerifyCertificateChainPolicy(CERT_CHAIN_POLICY_AUTHENTICODE, chain, &pp, &ps);
    printf("policy authenticode %d error %08lx\n", ret, ps.dwError);
    ret = CertVerifyCertificateChainPolicy(CERT_CHAIN_POLICY_BASE, chain, &pp, &ps);
    printf("policy base %d error %08lx\n", ret, ps.dwError);

    bi.gSubject = p7x;
    bi.cbMemObject = size = 4 + p7s_size;
    bi.pbMemObject = buf;
    td.dwUIChoice = WTD_UI_NONE;
    td.fdwRevocationChecks = WTD_REVOKE_NONE;
    td.dwUnionChoice = WTD_CHOICE_BLOB;
    td.dwStateAction = WTD_STATEACTION_VERIFY;
    td.dwProvFlags = WTD_CACHE_ONLY_URL_RETRIEVAL | WTD_REVOCATION_CHECK_NONE;
    td.pBlob = &bi;
    hr = WinVerifyTrust(INVALID_HANDLE_VALUE, &action, &td);
    printf("WinVerifyTrust blob %08lx\n", hr);
    {
        CRYPT_PROVIDER_DATA *pd = WTHelperProvDataFromStateData(td.hWVTStateData);
        SYSTEMTIME st;
        if (pd)
        {
            printf("steps: %08lx %08lx %08lx %08lx signers %lu\n", pd->padwTrustStepErrors[TRUSTERROR_STEP_FINAL_WVTINIT],
                   pd->padwTrustStepErrors[TRUSTERROR_STEP_FINAL_OBJPROV], pd->padwTrustStepErrors[TRUSTERROR_STEP_FINAL_SIGPROV],
                   pd->padwTrustStepErrors[TRUSTERROR_STEP_FINAL_POLICYPROV], pd->csSigners);
            if (pd->pPDSip) printf("sip subject %08lx\n", pd->pPDSip->gSubject.Data1);
            for (i = 0; i < pd->csSigners; i++)
            {
                CRYPT_PROVIDER_SGNR *s = &pd->pasSigners[i];
                FileTimeToSystemTime(&s->sftVerifyAsOf, &st);
                printf("signer %lu asof %04d-%02d-%02d %02d:%02d:%02d err %08lx certs %lu counters %lu chain %08lx\n", i,
                       st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, s->dwError, s->csCertChain,
                       s->csCounterSigners, s->pChainContext ? s->pChainContext->TrustStatus.dwErrorStatus : 0xdead);
                if (s->csCounterSigners)
                {
                    FileTimeToSystemTime(&s->pasCounterSigners[0].sftVerifyAsOf, &st);
                    printf("  counter asof %04d-%02d-%02d %02d:%02d:%02d err %08lx certs %lu chain %08lx\n",
                           st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, s->pasCounterSigners[0].dwError,
                           s->pasCounterSigners[0].csCertChain, s->pasCounterSigners[0].pChainContext ?
                           s->pasCounterSigners[0].pChainContext->TrustStatus.dwErrorStatus : 0xdead);
                }
            }
        }
        td.dwStateAction = WTD_STATEACTION_CLOSE;
        WinVerifyTrust(INVALID_HANDLE_VALUE, &action, &td);
    }
    return 0;
}
