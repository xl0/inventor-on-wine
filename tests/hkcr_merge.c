/* HKCR merged view probe: which side (HKCU/HKLM Software\Classes) backs HKCR handles for
 * open / create / query / enum, and whether HKCR handles carry the tag bit (handle & 3 == 2).
 * Needs admin (writes HKLM). Build: x86_64-w64-mingw32-gcc -O2 -o hkcr_merge.exe hkcr_merge.c */
#define _WIN32_WINNT 0x0600
#include <windows.h>
#include <stdio.h>

static LONG (WINAPI *pNtQueryKey)(HANDLE, int, void *, ULONG, ULONG *);

static const char *side(HKEY k)
{
    static char out[600];
    struct { ULONG len; WCHAR name[512]; } info;
    ULONG len;
    memset(&info, 0, sizeof(info));
    if (pNtQueryKey((HANDLE)((ULONG_PTR)k & ~(ULONG_PTR)3), 3 /* KeyNameInformation */, &info, sizeof(info), &len))
        return "?";
    snprintf(out, sizeof(out), "%s%.*ls", ((ULONG_PTR)k & 3) == 2 ? "[tag] " : "", (int)(info.len / 2), info.name);
    return out;
}

static void setv(HKEY k, const char *n, const char *v) { RegSetValueExA(k, n, 0, REG_SZ, (BYTE *)v, strlen(v) + 1); }

static void mk(HKEY root, const char *path, const char **vals)
{
    HKEY k;
    RegCreateKeyExA(root, path, 0, NULL, 0, KEY_ALL_ACCESS, NULL, &k, NULL);
    for (; vals && *vals; vals += 2) setv(k, vals[0], vals[1]);
    RegCloseKey(k);
}

static void q(HKEY k, const char *n)
{
    char buf[64]; DWORD sz = sizeof(buf); LONG r = RegQueryValueExA(k, n, NULL, NULL, (BYTE *)buf, &sz);
    printf("  query '%s': %ld %s\n", n, r, r ? "" : buf);
}

static void dump(const char *what, HKEY k)
{
    char buf[64]; DWORD i, sz, nk = 0, nv = 0; LONG r;
    printf("%s -> %s\n", what, side(k));
    q(k, ""); q(k, "v1"); q(k, "v2"); q(k, "both");
    printf("  keys:");
    for (i = 0; !(r = RegEnumKeyA(k, i, buf, sizeof(buf))); i++) printf(" %s", buf);
    printf(" (%ld)\n  values:", r);
    for (i = 0; sz = sizeof(buf), !(r = RegEnumValueA(k, i, buf, &sz, NULL, NULL, NULL, NULL)); i++) printf(" '%s'", buf);
    r = RegQueryInfoKeyA(k, NULL, NULL, NULL, &nk, NULL, NULL, &nv, NULL, NULL, NULL, NULL);
    printf(" (%ld)\n  QueryInfoKey %ld: subkeys %lu values %lu\n", r, r, nk, nv);
}

static void where(const char *what, HKEY k, LONG r, DWORD disp)
{
    printf("%s: %ld disp %lu -> %s\n", what, r, disp, r ? "" : side(k));
    if (!r) RegCloseKey(k);
}

int main(void)
{
    static const char *lm[] = { "", "m", "v1", "m1", "both", "m", NULL };
    static const char *cu[] = { "v2", "u2", "both", "u", "a0", "u", NULL };
    static const char *lmsub[] = { "", "lm-sub", NULL };
    static const char *cusub[] = { "", "cu-sub", NULL };
    HKEY hkcr, sub, k; LONG r; DWORD disp;
    char buf[64]; LONG sz;

    pNtQueryKey = (void *)GetProcAddress(GetModuleHandleA("ntdll.dll"), "NtQueryKey");
    RegDeleteTreeA(HKEY_LOCAL_MACHINE, "Software\\Classes\\WineMrg");
    RegDeleteTreeA(HKEY_CURRENT_USER, "Software\\Classes\\WineMrg");
    RegDeleteTreeA(HKEY_LOCAL_MACHINE, "Software\\Classes\\WineMrgNew");
    RegDeleteTreeA(HKEY_CURRENT_USER, "Software\\Classes\\WineMrgUsr");
    RegDeleteTreeA(HKEY_LOCAL_MACHINE, "Software\\Classes\\WineMrgUsr");

    mk(HKEY_LOCAL_MACHINE, "Software\\Classes\\WineMrg", lm);
    mk(HKEY_LOCAL_MACHINE, "Software\\Classes\\WineMrg\\C", NULL);
    mk(HKEY_LOCAL_MACHINE, "Software\\Classes\\WineMrg\\A", lmsub);
    mk(HKEY_LOCAL_MACHINE, "Software\\Classes\\WineMrg\\lmonly\\deep", lmsub);
    mk(HKEY_CURRENT_USER, "Software\\Classes\\WineMrg", cu);
    mk(HKEY_CURRENT_USER, "Software\\Classes\\WineMrg\\B", NULL);
    mk(HKEY_CURRENT_USER, "Software\\Classes\\WineMrg\\Z", NULL);
    mk(HKEY_LOCAL_MACHINE, "Software\\Classes\\WineMrg\\Y", NULL);
    mk(HKEY_CURRENT_USER, "Software\\Classes\\WineMrg\\A", cusub);
    mk(HKEY_CURRENT_USER, "Software\\Classes\\WineMrgUsr", cu);

    r = RegOpenKeyExA(HKEY_CLASSES_ROOT, "WineMrg", 0, KEY_READ, &hkcr);
    printf("open HKCR\\WineMrg %ld\n", r);
    dump("HKCR\\WineMrg", hkcr);
    r = RegOpenKeyExA(hkcr, "A", 0, KEY_READ, &sub); printf("rel A %ld\n", r); if (!r) { dump("HKCR\\WineMrg\\A", sub); RegCloseKey(sub); }
    r = RegOpenKeyExA(hkcr, "C", 0, KEY_READ, &sub); printf("rel C %ld\n", r); if (!r) { dump("HKCR\\WineMrg\\C", sub); RegCloseKey(sub); }
    r = RegOpenKeyExA(hkcr, "lmonly\\deep", 0, KEY_READ, &sub); printf("rel lmonly\\deep %ld\n", r); if (!r) { dump("rel deep", sub); RegCloseKey(sub); }
    r = RegOpenKeyExA(HKEY_CLASSES_ROOT, "WineMrg\\lmonly\\deep", 0, KEY_READ, &sub); printf("abs deep %ld %s\n", r, r ? "" : side(sub)); if (!r) RegCloseKey(sub);
    sz = sizeof(buf); r = RegQueryValueA(HKEY_CLASSES_ROOT, "WineMrg\\A", buf, &sz); printf("RegQueryValue HKCR WineMrg\\A: %ld %s\n", r, r ? "" : buf);
    sz = sizeof(buf); r = RegQueryValueA(HKEY_CLASSES_ROOT, "WineMrg", buf, &sz); printf("RegQueryValue HKCR WineMrg: %ld %s\n", r, r ? "" : buf);
    sz = sizeof(buf); r = RegGetValueA(HKEY_CLASSES_ROOT, "WineMrg", "v1", RRF_RT_REG_SZ, NULL, buf, (DWORD *)&sz); printf("RegGetValue HKCR WineMrg v1: %ld %s\n", r, r ? "" : buf);

    r = RegOpenKeyExA(HKEY_CLASSES_ROOT, "WineMrgUsr", 0, KEY_READ, &k);
    printf("open HKCR\\WineMrgUsr %ld %s\n", r, r ? "" : side(k)); if (!r) RegCloseKey(k);
    r = RegOpenKeyExA(HKEY_CLASSES_ROOT, "CLSID", 0, KEY_READ, &k);
    printf("open HKCR\\CLSID %ld %s\n", r, r ? "" : side(k)); if (!r) RegCloseKey(k);
    r = RegOpenKeyExA(HKEY_CLASSES_ROOT, "", 0, KEY_READ, &k);
    printf("open HKCR \"\" %ld %p\n", r, k); if (!r) RegCloseKey(k);

    /* creates */
    r = RegCreateKeyExA(HKEY_CLASSES_ROOT, "WineMrg", 0, NULL, 0, KEY_ALL_ACCESS, NULL, &k, &disp); where("create HKCR\\WineMrg (both)", k, r, disp);
    r = RegCreateKeyExA(HKEY_CLASSES_ROOT, "WineMrgUsr", 0, NULL, 0, KEY_ALL_ACCESS, NULL, &k, &disp); where("create HKCR\\WineMrgUsr (cu only)", k, r, disp);
    r = RegCreateKeyExA(HKEY_CLASSES_ROOT, "WineMrgUsr\\new", 0, NULL, 0, KEY_ALL_ACCESS, NULL, &k, &disp); where("create HKCR\\WineMrgUsr\\new", k, r, disp);
    r = RegCreateKeyExA(HKEY_CLASSES_ROOT, "WineMrg\\lmonly\\new", 0, NULL, 0, KEY_ALL_ACCESS, NULL, &k, &disp); where("create HKCR\\WineMrg\\lmonly\\new", k, r, disp);
    r = RegCreateKeyExA(HKEY_CLASSES_ROOT, "WineMrg\\newboth", 0, NULL, 0, KEY_ALL_ACCESS, NULL, &k, &disp); where("create HKCR\\WineMrg\\newboth", k, r, disp);
    r = RegCreateKeyExA(HKEY_CLASSES_ROOT, "WineMrgNew", 0, NULL, 0, KEY_ALL_ACCESS, NULL, &k, &disp); where("create HKCR\\WineMrgNew (none)", k, r, disp);
    r = RegOpenKeyExA(HKEY_CLASSES_ROOT, "WineMrgUsr", 0, KEY_ALL_ACCESS, &k);
    r = RegCreateKeyExA(k, "rel", 0, NULL, 0, KEY_ALL_ACCESS, NULL, &sub, &disp); where("create rel HKCR\\WineMrgUsr\\rel", sub, r, disp);
    RegCloseKey(k);

    /* HKLM-side handle, then user key appears */
    r = RegOpenKeyExA(hkcr, "lmonly", 0, KEY_ALL_ACCESS, &sub);
    printf("lmonly handle %s\n", side(sub));
    mk(HKEY_CURRENT_USER, "Software\\Classes\\WineMrg\\lmonly", cusub);
    q(sub, "");
    setv(sub, "set", "x");
    r = RegOpenKeyExA(HKEY_CURRENT_USER, "Software\\Classes\\WineMrg\\lmonly", 0, KEY_READ, &k); q(k, "set"); RegCloseKey(k);
    r = RegOpenKeyExA(HKEY_LOCAL_MACHINE, "Software\\Classes\\WineMrg\\lmonly", 0, KEY_READ, &k); q(k, "set"); RegCloseKey(k);
    RegCloseKey(sub);
    RegCloseKey(hkcr);

    RegDeleteTreeA(HKEY_LOCAL_MACHINE, "Software\\Classes\\WineMrg");
    RegDeleteTreeA(HKEY_CURRENT_USER, "Software\\Classes\\WineMrg");
    RegDeleteTreeA(HKEY_LOCAL_MACHINE, "Software\\Classes\\WineMrgNew");
    RegDeleteTreeA(HKEY_CURRENT_USER, "Software\\Classes\\WineMrgUsr");
    RegDeleteTreeA(HKEY_LOCAL_MACHINE, "Software\\Classes\\WineMrgUsr");
    return 0;
}
