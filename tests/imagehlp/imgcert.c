/* imagehlp certificate functions on a PE file (issue 019). Usage: imgcert FILE...
 * Build: x86_64-w64-mingw32-gcc -O2 -o imgcert.exe imgcert.c -limagehlp */
#include <windows.h>
#include <imagehlp.h>
#include <stdio.h>

int main(int argc, char **argv)
{
    int a;
    for (a = 1; a < argc; a++)
    {
        HANDLE f = CreateFileA(argv[a], GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
        WIN_CERTIFICATE hdr, *cert;
        DWORD count = 0, len = 0, idx[4];
        BOOL ret;

        if (f == INVALID_HANDLE_VALUE) { printf("%s: open error %lu\n", argv[a], GetLastError()); continue; }
        SetLastError(0xdeadbeef);
        ret = ImageEnumerateCertificates(f, CERT_SECTION_TYPE_ANY, &count, idx, 4);
        printf("%s: enum ret %d count %lu err %lu\n", argv[a], ret, count, GetLastError());
        SetLastError(0xdeadbeef);
        ret = ImageGetCertificateHeader(f, 0, &hdr);
        printf("  header ret %d err %lu len %lx rev %x type %x\n", ret, GetLastError(),
               ret ? hdr.dwLength : 0, ret ? hdr.wRevision : 0, ret ? hdr.wCertificateType : 0);
        SetLastError(0xdeadbeef);
        ret = ImageGetCertificateData(f, 0, NULL, &len);
        printf("  data(NULL) ret %d err %lu len %lx\n", ret, GetLastError(), len);
        if (len && (cert = malloc(len)))
        {
            SetLastError(0xdeadbeef);
            ret = ImageGetCertificateData(f, 0, cert, &len);
            printf("  data ret %d err %lu len %lx dwLength %lx first %02x%02x%02x%02x\n", ret, GetLastError(), len,
                   cert->dwLength, cert->bCertificate[0], cert->bCertificate[1], cert->bCertificate[2], cert->bCertificate[3]);
            free(cert);
        }
        CloseHandle(f);
    }
    return 0;
}
