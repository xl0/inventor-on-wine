/* Add a local PostScript printer on port FILE: (Wine: wineps.drv + a PPD) and make
 * it the default, for print tests on a prefix without CUPS printers.
 * Usage: addprinter.exe NAME PPD-PATH   (Windows ships "Microsoft Print to PDF" instead)
 * Build: x86_64-w64-mingw32-gcc -O2 -municode -o addprinter.exe addprinter.c -lwinspool */
#include <windows.h>
#include <winspool.h>
#include <stdio.h>

int wmain(int argc, WCHAR **argv)
{
    DRIVER_INFO_3W di = {0};
    PRINTER_INFO_2W pi = {0};
    HANDLE h;

    if (argc < 3) return 2;
    di.cVersion = 3;
    di.pName = argv[1];
    di.pEnvironment = NULL;
    di.pDriverPath = (WCHAR *)L"wineps.drv";
    di.pConfigFile = (WCHAR *)L"wineps.drv";
    di.pDataFile = argv[2];
    di.pDefaultDataType = (WCHAR *)L"RAW";
    if (!AddPrinterDriverExW(NULL, 3, (BYTE *)&di, APD_COPY_NEW_FILES | APD_COPY_FROM_DIRECTORY)
        && GetLastError() != ERROR_PRINTER_DRIVER_ALREADY_INSTALLED)
    {
        printf("AddPrinterDriverEx failed %lu\n", GetLastError());
        return 1;
    }
    AddPrintProcessorW(NULL, NULL, (WCHAR *)L"wineps.drv", (WCHAR *)L"wineps");
    pi.pPrinterName = argv[1];
    pi.pPortName = (WCHAR *)L"FILE:";
    pi.pDriverName = argv[1];
    pi.pPrintProcessor = (WCHAR *)L"wineps";
    pi.pDatatype = (WCHAR *)L"RAW";
    pi.Attributes = PRINTER_ATTRIBUTE_LOCAL;
    if (!(h = AddPrinterW(NULL, 2, (BYTE *)&pi)) && GetLastError() != ERROR_PRINTER_ALREADY_EXISTS)
    {
        printf("AddPrinter failed %lu\n", GetLastError());
        return 1;
    }
    if (h) ClosePrinter(h);
    if (!SetDefaultPrinterW(argv[1])) printf("SetDefaultPrinter failed %lu\n", GetLastError());
    printf("ok\n");
    return 0;
}
