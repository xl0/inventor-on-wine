/* List windows with an OLE drop target registration (read-only), flagging those
 * owned by a different process than their top-level ancestor (034).
 * Build: x86_64-w64-mingw32-gcc -O2 -o droptargets.exe droptargets.c -luser32 */
#include <windows.h>
#include <tlhelp32.h>
#include <stdio.h>

static void exe_name(DWORD pid, char *buf)
{
    PROCESSENTRY32 pe = { sizeof(pe) };
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    strcpy(buf, "?");
    if (Process32First(snap, &pe)) do if (pe.th32ProcessID == pid) { strcpy(buf, pe.szExeFile); break; } while (Process32Next(snap, &pe));
    CloseHandle(snap);
}

static BOOL CALLBACK child(HWND hwnd, LPARAM top)
{
    HANDLE t = GetPropW(hwnd, L"OleDropTargetInterface");
    DWORD pid, tpid;
    char cls[64], exe[MAX_PATH], texe[MAX_PATH];
    if (!t) return TRUE;
    GetWindowThreadProcessId(hwnd, &pid);
    GetWindowThreadProcessId((HWND)top, &tpid);
    GetClassNameA(hwnd, cls, sizeof(cls));
    exe_name(pid, exe); exe_name(tpid, texe);
    printf("%s hwnd %p %-32s target %p owner %04lx %s, top %p %04lx %s\n", pid != tpid ? "XPROC" : "     ",
           hwnd, cls, t, pid, exe, (HWND)top, tpid, texe);
    return TRUE;
}

static BOOL CALLBACK top(HWND hwnd, LPARAM p)
{
    child(hwnd, (LPARAM)hwnd);
    EnumChildWindows(hwnd, child, (LPARAM)hwnd);
    return TRUE;
}

int main(void) { EnumWindows(top, 0); return 0; }
