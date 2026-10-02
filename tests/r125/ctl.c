/* 125: select a string in a combo box of another process's dialog and notify its parent (CBN_SELCHANGE/SELENDOK).
 * x86_64-w64-mingw32-gcc -O2 -municode -o ctl.exe ctl.c */
/* ctl.exe TITLE            : list ComboBox children (index, id, count, text)
 * ctl.exe TITLE ID STRING  : select STRING in the combo with control id ID and notify the parent */
#include <windows.h>
#include <stdio.h>
static HWND combos[64]; static int ncombos;
static BOOL CALLBACK child_cb(HWND hwnd, LPARAM lp)
{
    WCHAR cls[64];
    GetClassNameW(hwnd, cls, 64);
    if (!lstrcmpiW(cls, L"ComboBox") && ncombos < 64) combos[ncombos++] = hwnd;
    return TRUE;
}
int wmain(int argc, WCHAR **argv)
{
    HWND dlg = FindWindowW(NULL, argv[1]);
    int i;
    if (!dlg) { printf("no window\n"); return 1; }
    EnumChildWindows(dlg, child_cb, 0);
    if (argc < 4)
    {
        for (i = 0; i < ncombos; i++)
        {
            WCHAR text[256] = {0}; char buf[512];
            LRESULT sel = SendMessageW(combos[i], CB_GETCURSEL, 0, 0);
            SendMessageW(combos[i], WM_GETTEXT, 256, (LPARAM)text);
            WideCharToMultiByte(CP_UTF8, 0, text, -1, buf, sizeof(buf), NULL, NULL);
            printf("%d hwnd %p id %d count %d sel %d vis %d text '%s'\n", i, combos[i], GetDlgCtrlID(combos[i]),
                   (int)SendMessageW(combos[i], CB_GETCOUNT, 0, 0), (int)sel, IsWindowVisible(combos[i]), buf);
        }
        return 0;
    }
    { int id = _wtoi(argv[2]); for (i = 0; i < ncombos; i++) if (GetDlgCtrlID(combos[i]) == id) break; if (i == ncombos) return 3; }
    {
        LRESULT r = SendMessageW(combos[i], CB_FINDSTRINGEXACT, -1, (LPARAM)argv[3]);
        printf("find %d\n", (int)r);
        if (r < 0) return 2;
        SendMessageW(combos[i], CB_SETCURSEL, r, 0);
        SendMessageW(GetParent(combos[i]), WM_COMMAND, MAKEWPARAM(GetDlgCtrlID(combos[i]), CBN_SELCHANGE), (LPARAM)combos[i]);
        SendMessageW(GetParent(combos[i]), WM_COMMAND, MAKEWPARAM(GetDlgCtrlID(combos[i]), CBN_SELENDOK), (LPARAM)combos[i]);
    }
    return 0;
}
