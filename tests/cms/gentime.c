/* Decode GeneralizedTime variants with X509_CHOICE_OF_TIME; print result, error, SYSTEMTIME. */
#include <windows.h>
#include <wincrypt.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    static const char *t[] = { "20251122002231Z", "20251122002231.7Z", "20251122002231.96Z",
        "20251122002231.969Z", "20251122002231.9691Z", "20251122002231.7",
        "20251122002231.Z", "20251122002231.", "20251122002231,7Z", "20251122002231.9699Z",
        "20251122002231.123456789Z", "20251122002231.7+0100", "20251122002231.96-0130",
        "20251122002231.7+01", "20251122002231.1234+0100", "202511220022.5Z", "2025112200.5Z",
        "20251122002231.7x", "20251122002231.7Zx", "20251122002231.x7Z", "20251122002231.07Z",
        "20251122002231.0Z", "20251122002231.9999Z", "20251122002231.12a4Z",
        "20251122002231x", "20251122002231Zx" };
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
