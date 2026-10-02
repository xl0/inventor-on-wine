/* 125: clip.exe [TEXT]: put TEXT (default: 4 CJK chars) on the clipboard as CF_UNICODETEXT.
 * x86_64-w64-mingw32-gcc -O2 -municode -o clip.exe clip.c */
#include <windows.h>
int wmain(int argc, WCHAR **argv)
{
    static const WCHAR cjk[] = {0x4e2d,0x6587,0x6d4b,0x8bd5,0};
    const WCHAR *text = argc > 1 ? argv[1] : cjk;
    SIZE_T size = (lstrlenW(text) + 1) * sizeof(WCHAR);
    HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, size);
    memcpy(GlobalLock(h), text, size); GlobalUnlock(h);
    if (!OpenClipboard(NULL)) return 1;
    EmptyClipboard();
    SetClipboardData(CF_UNICODETEXT, h);
    CloseClipboard();
    return 0;
}
