/* Decode GeneralizedTime variants with X509_CHOICE_OF_TIME; print result, error, SYSTEMTIME. */
#include <windows.h>
#include <wincrypt.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    static const char *t[] = { "20251122002231Z", "20251122002231.7Z", "20251122002231.96Z",
        "20251122002231.969Z", "20251122002231.9691Z", "20251122002231.7" };
    int i;
    for (i = 0; i < sizeof(t) / sizeof(t[0]); i++)
    {
        BYTE buf[64]; FILETIME ft; DWORD sz = sizeof(ft); SYSTEMTIME st = {0}; BOOL ret;
        size_t n = strlen(t[i]);
        buf[0] = 0x18; buf[1] = n; memcpy(buf + 2, t[i], n);
        SetLastError(0xdeadbeef);
        ret = CryptDecodeObjectEx(X509_ASN_ENCODING, X509_CHOICE_OF_TIME, buf, n + 2, 0, NULL, &ft, &sz);
        if (ret) FileTimeToSystemTime(&ft, &st);
        printf("%-22s ret %d err %08lx  %04d-%02d-%02d %02d:%02d:%02d.%03d\n", t[i], ret, ret ? 0 : GetLastError(),
               st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    }
    return 0;
}
