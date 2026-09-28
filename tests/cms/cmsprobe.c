/* Dumps how crypt32 decodes a PKCS #7 / CMS signed message (issue 013). Usage: cmsprobe FILE.der... */
#include <windows.h>
#include <wincrypt.h>
#include <stdio.h>

static void dump_param(HCRYPTMSG msg, DWORD count_param, DWORD param, const char *name)
{
    DWORD count = 0, size = sizeof(count), i;
    BOOL ret = CryptMsgGetParam(msg, count_param, 0, &count, &size);
    printf("  %s count ret %d err %08lx count %lu\n", name, ret, ret ? 0 : GetLastError(), count);
    for (i = 0; ret && i < count + 1; i++)
    {
        BYTE buf[8192];
        size = sizeof(buf);
        if (CryptMsgGetParam(msg, param, i, buf, &size))
            printf("    [%lu] size %lu tag %02x\n", i, size, buf[0]);
        else
            printf("    [%lu] err %08lx\n", i, GetLastError());
    }
}

int main(int argc, char **argv)
{
    int a;
    for (a = 1; a < argc; a++)
    {
        static BYTE buf[1 << 20];
        FILE *f = fopen(argv[a], "rb");
        DWORD size = fread(buf, 1, sizeof(buf), f), n;
        HCRYPTMSG msg;
        HCERTSTORE store;
        const DWORD enc = X509_ASN_ENCODING | PKCS_7_ASN_ENCODING;
        BOOL ret;

        fclose(f);
        printf("%s: size %lu\n", argv[a], size);
        msg = CryptMsgOpenToDecode(enc, 0, 0, 0, NULL, NULL);
        ret = CryptMsgUpdate(msg, buf, size, TRUE);
        printf("  update %d err %08lx\n", ret, ret ? 0 : GetLastError());
        if (!ret) { CryptMsgClose(msg); continue; }
        dump_param(msg, CMSG_CERT_COUNT_PARAM, CMSG_CERT_PARAM, "cert");
        dump_param(msg, CMSG_CRL_COUNT_PARAM, CMSG_CRL_PARAM, "crl");
        dump_param(msg, CMSG_ATTR_CERT_COUNT_PARAM, CMSG_ATTR_CERT_PARAM, "attrcert");
        n = 0; size = sizeof(n);
        ret = CryptMsgGetParam(msg, CMSG_SIGNER_COUNT_PARAM, 0, &n, &size);
        printf("  signers ret %d count %lu\n", ret, n);
        store = CertOpenStore(CERT_STORE_PROV_MSG, enc, 0, 0, msg);
        printf("  store %d err %08lx\n", !!store, store ? 0 : GetLastError());
        if (store)
        {
            PCCERT_CONTEXT c = NULL;
            PCCRL_CONTEXT crl = NULL;
            DWORD flags = 0, nc = 0, ncrl = 0;
            while ((c = CertEnumCertificatesInStore(store, c))) nc++;
            while ((crl = CertGetCRLFromStore(store, NULL, crl, &flags))) ncrl++;
            printf("  store certs %lu crls %lu\n", nc, ncrl);
            CertCloseStore(store, 0);
        }
        CryptMsgClose(msg);
    }
    return 0;
}
