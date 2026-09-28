/* Dump rows of an MSI SQL query: msiq DB "SELECT ..." */
#include <windows.h>
#include <msiquery.h>
#include <stdio.h>
int main(int argc, char **argv)
{
    MSIHANDLE db, view, rec; UINT i, n;
    if (MsiOpenDatabaseA(argv[1], MSIDBOPEN_READONLY, &db)) return 1;
    if (MsiDatabaseOpenViewA(db, argv[2], &view) || MsiViewExecute(view, 0)) return 2;
    while (!MsiViewFetch(view, &rec))
    {
        n = MsiRecordGetFieldCount(rec);
        for (i = 1; i <= n; i++) { char buf[8192]; DWORD sz = sizeof(buf); buf[0] = 0; MsiRecordGetStringA(rec, i, buf, &sz); printf("%s%s", buf, i < n ? " | " : "\n"); }
        MsiCloseHandle(rec);
    }
    return 0;
}
