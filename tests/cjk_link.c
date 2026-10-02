/* 119: GDI font linking of CJK characters per base font: GetGlyphIndices (never linked),
 * GetGlyphOutline GGO_NATIVE size + metrics and GetTextExtentPoint32 of U+6E2C U+65E5 U+4E2D U+AC00 'A'.
 * A linked glyph shows a big outline and a full-width advance; .notdef a small box.
 * First prints diagnostics (123): code page, the registry font configuration (SystemLink, FontSubstitutes,
 * Wine Replacements) with whether the named families are installed, installed East Asian families, and which
 * installed font Tahoma's linked glyphs come from (outline comparison). The link list Wine built is in
 * `WINEDEBUG=+font wine cjk_link.exe 2>&1 | grep -a "SystemLink\|linked Tahoma"`.
 * cjk_link.exe [HEIGHT] (lfHeight, default -32)
 * x86_64-w64-mingw32-gcc -O2 -o cjk_link.exe cjk_link.c -lgdi32 -ladvapi32 */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#ifndef LOCALE_SNAME
#define LOCALE_SNAME 0x5c
#endif

static int height = -32;

static void probe( HDC hdc, const WCHAR *face, int charset )
{
    static const WCHAR chars[] = { 0x6e2c, 0x65e5, 0x4e2d, 0xac00, 'A' };
    static const MAT2 mat = { {0,1}, {0,0}, {0,0}, {0,1} };
    LOGFONTW lf = { height };
    WCHAR name[LF_FACESIZE];
    HFONT font, old;
    unsigned int i;

    lf.lfCharSet = charset;
    lstrcpyW( lf.lfFaceName, face );
    font = CreateFontIndirectW( &lf );
    old = SelectObject( hdc, font );
    GetTextFaceW( hdc, LF_FACESIZE, name );
    printf( "%-22ls cs %3d -> %-22ls", face, charset, name );
    for (i = 0; i < ARRAY_SIZE(chars); i++)
    {
        WORD idx = 0;
        GLYPHMETRICS gm = {0};
        SIZE sz = {0};
        DWORD native;

        GetGlyphIndicesW( hdc, &chars[i], 1, &idx, GGI_MARK_NONEXISTING_GLYPHS );
        native = GetGlyphOutlineW( hdc, chars[i], GGO_NATIVE, &gm, 0, NULL, &mat );
        GetTextExtentPoint32W( hdc, &chars[i], 1, &sz );
        printf( " | %04x gi %04x nat %5ld inc %2d bb %2ux%2u ext %2ld", chars[i], idx, (long)native,
                gm.gmCellIncX, gm.gmBlackBoxX, gm.gmBlackBoxY, sz.cx );
    }
    printf( "\n" );
    SelectObject( hdc, old );
    DeleteObject( font );
}

static const MAT2 ident = { {0,1}, {0,0}, {0,0}, {0,1} };

static int CALLBACK any_proc( const LOGFONTW *lf, const TEXTMETRICW *tm, DWORD type, LPARAM lp )
{
    return 0;
}

static BOOL installed( HDC hdc, const WCHAR *family )
{
    LOGFONTW lf = {0};
    lf.lfCharSet = DEFAULT_CHARSET;
    lstrcpynW( lf.lfFaceName, family, LF_FACESIZE );
    return !EnumFontFamiliesExW( hdc, &lf, any_proc, 0, 0 );
}

/* print the values of a registry key; filter: only value names / strings containing one of the words */
static void dump_key( HDC hdc, HKEY root, const WCHAR *path, const WCHAR **filter, BOOL check_families )
{
    WCHAR name[256], data[4096], *p, *family;
    DWORD i, j, name_len, size, type;
    HKEY key;

    printf( "[%ls]\n", path );
    if (RegOpenKeyExW( root, path, 0, KEY_READ, &key )) { printf( "  (no key)\n" ); return; }
    for (i = 0;; i++)
    {
        name_len = ARRAY_SIZE(name);
        size = sizeof(data) - 2 * sizeof(WCHAR);
        memset( data, 0, sizeof(data) );
        if (RegEnumValueW( key, i, name, &name_len, NULL, &type, (BYTE *)data, &size )) break;
        if (type != REG_SZ && type != REG_MULTI_SZ) continue;
        if (filter)
        {
            for (j = 0; filter[j]; j++)
                if (!lstrcmpiW( name, filter[j] ) || (type == REG_SZ && wcsstr( data, filter[j] ))) break;
            if (!filter[j]) continue;
        }
        printf( "  %ls =\n", name );
        for (p = data; *p; p += lstrlenW( p ) + 1)
        {
            printf( "      %ls", p );
            if (check_families)
            {
                /* "FILE,Family[,scale]" (links) or "Family[,charset]" (substitutes) */
                WCHAR tmp[256], *comma;
                lstrcpynW( tmp, p, ARRAY_SIZE(tmp) );
                family = tmp;
                if ((comma = wcschr( tmp, ',' )))
                {
                    if (comma[1] >= '0' && comma[1] <= '9') *comma = 0;
                    else
                    {
                        family = comma + 1;
                        while (*family == ' ') family++;
                        if ((comma = wcschr( family, ',' ))) *comma = 0;
                    }
                }
                printf( "  [%ls: %s]", family, installed( hdc, family ) ? "installed" : "missing" );
            }
            printf( "\n" );
            if (type == REG_SZ) break;
        }
    }
    RegCloseKey( key );
}

static WCHAR families[4096][LF_FACESIZE];
static unsigned int family_count;

static int CALLBACK family_proc( const LOGFONTW *lf, const TEXTMETRICW *tm, DWORD type, LPARAM lp )
{
    unsigned int i;

    if (lf->lfFaceName[0] == '@' || (type & RASTER_FONTTYPE)) return 1;
    if (lp && lf->lfCharSet != lp) return 1;
    for (i = 0; i < family_count; i++) if (!lstrcmpW( families[i], lf->lfFaceName )) return 1;
    if (family_count < ARRAY_SIZE(families)) lstrcpyW( families[family_count++], lf->lfFaceName );
    return 1;
}

static void list_families( HDC hdc, int charset )
{
    LOGFONTW lf = {0};
    lf.lfCharSet = charset;
    family_count = 0;
    EnumFontFamiliesExW( hdc, &lf, family_proc, charset == DEFAULT_CHARSET ? 0 : charset, 0 );
}

static DWORD outline( HDC hdc, const WCHAR *family, UINT ch, UINT format, BYTE *buf, DWORD size, BOOL own_glyph )
{
    LOGFONTW lf = { -32 };
    GLYPHMETRICS gm;
    HFONT font, old;
    DWORD ret = GDI_ERROR;
    WCHAR wc = ch;
    WORD idx = 0;

    lf.lfCharSet = DEFAULT_CHARSET;
    lstrcpyW( lf.lfFaceName, family );
    font = CreateFontIndirectW( &lf );
    old = SelectObject( hdc, font );
    if (own_glyph) GetGlyphIndicesW( hdc, &wc, 1, &idx, GGI_MARK_NONEXISTING_GLYPHS );
    if (idx != 0xffff) ret = GetGlyphOutlineW( hdc, ch, format, &gm, size, buf, &ident );
    SelectObject( hdc, old );
    DeleteObject( font );
    return ret;
}

static void diag( HDC hdc )
{
    static const struct { int charset; const char *name; } charsets[] =
    {
        { SHIFTJIS_CHARSET, "SHIFTJIS" }, { GB2312_CHARSET, "GB2312" },
        { CHINESEBIG5_CHARSET, "CHINESEBIG5" }, { HANGEUL_CHARSET, "HANGEUL" },
    };
    static const WCHAR *link_names[] = { L"Tahoma", L"Microsoft Sans Serif", L"Segoe UI", L"MS Shell Dlg",
        L"MS Shell Dlg 2", L"MS UI Gothic", L"SimSun", L"Arial", NULL };
    static const WCHAR *subst_names[] = { L"Tahoma", L"MS Shell Dlg", L"MS Shell Dlg 2", L"Segoe UI",
        L"Microsoft Sans Serif", L"Arial", L"Noto", NULL };
    static const WCHAR *font_names[] = { L"Tahoma (TrueType)", L"Tahoma Bold (TrueType)", L"NotoSansCJK", NULL };
    static const WCHAR *noto[] = { L"Noto Sans CJK JP", L"Noto Sans CJK SC", L"Noto Sans CJK TC",
        L"Noto Sans CJK KR", L"Noto Sans CJK HK" };
    static const WCHAR chars[] = { 0x6e2c, 0x65e5, 0x4e2d, 0x3042, 0xac00 };
    static BYTE buf[65536], buf2[65536], def[65536];
    const char *(CDECL *p_wine_get_version)(void);
    DWORD size, size2, def_size;
    unsigned int i, j, matches;
    char locale[64] = "";

    p_wine_get_version = (void *)GetProcAddress( GetModuleHandleA( "ntdll.dll" ), "wine_get_version" );
    GetLocaleInfoA( LOCALE_USER_DEFAULT, LOCALE_SNAME, locale, sizeof(locale) );
    printf( "=== diagnostics: wine %s, ACP %u, OEMCP %u, locale %s, system langid %04x\n",
            p_wine_get_version ? p_wine_get_version() : "-", GetACP(), GetOEMCP(), locale, GetSystemDefaultLangID() );

    dump_key( hdc, HKEY_LOCAL_MACHINE, L"Software\\Microsoft\\Windows NT\\CurrentVersion\\FontLink\\SystemLink",
              link_names, TRUE );
    dump_key( hdc, HKEY_LOCAL_MACHINE, L"Software\\Microsoft\\Windows NT\\CurrentVersion\\FontSubstitutes",
              subst_names, TRUE );
    dump_key( hdc, HKEY_CURRENT_USER, L"Software\\Wine\\Fonts\\Replacements", NULL, TRUE );
    dump_key( hdc, HKEY_CURRENT_USER, L"Software\\Wine\\Uniscribe\\Fallback", NULL, TRUE );
    dump_key( hdc, HKEY_LOCAL_MACHINE, L"Software\\Microsoft\\Windows NT\\CurrentVersion\\Fonts", font_names, FALSE );

    printf( "Tahoma: %s", installed( hdc, L"Tahoma" ) ? "installed" : "MISSING" );
    for (i = 0; i < ARRAY_SIZE(noto); i++)
        printf( ", %ls: %s", noto[i], installed( hdc, noto[i] ) ? "installed" : "missing" );
    printf( "\n" );

    for (i = 0; i < ARRAY_SIZE(charsets); i++)
    {
        list_families( hdc, charsets[i].charset );
        printf( "%s fonts (%u):", charsets[i].name, family_count );
        for (j = 0; j < family_count && j < 16; j++) printf( " \"%ls\"", families[j] );
        printf( "%s\n", family_count > 16 ? " ..." : "" );
    }

    /* which installed font has the outline that Tahoma returns */
    list_families( hdc, DEFAULT_CHARSET );
    def_size = outline( hdc, L"Tahoma", 0xffff, GGO_NATIVE, def, sizeof(def), FALSE );
    for (i = 0; i < ARRAY_SIZE(chars); i++)
    {
        size = outline( hdc, L"Tahoma", chars[i], GGO_NATIVE, buf, sizeof(buf), FALSE );
        printf( "Tahoma U+%04X: outline %ld bytes", chars[i], (long)size );
        if (size == GDI_ERROR || (size == def_size && !memcmp( buf, def, size )))
        {
            printf( " = default glyph: NOT LINKED\n" );
            continue;
        }
        for (j = matches = 0; j < family_count && matches < 6; j++)
        {
            if (!lstrcmpW( families[j], L"Tahoma" )) continue;
            size2 = outline( hdc, families[j], chars[i], GGO_NATIVE, buf2, sizeof(buf2), TRUE );
            if (size2 != size || memcmp( buf, buf2, size )) continue;
            printf( "%s \"%ls\"", matches++ ? "," : " linked, same glyph as", families[j] );
        }
        printf( "%s\n", matches ? "" : " linked, no installed font has this glyph outline" );
    }
    printf( "=== per font\n" );
}

int main( int argc, char **argv )
{
    static const WCHAR *faces[] = { L"Tahoma", L"Segoe UI", L"Microsoft Sans Serif", L"MS Sans Serif",
        L"MS Shell Dlg", L"MS Shell Dlg 2", L"Arial", L"Times New Roman", L"Courier New", L"Verdana",
        L"MS Gothic", L"MS UI Gothic", L"SimSun", L"Meiryo UI", L"Yu Gothic UI", L"Microsoft YaHei UI",
        L"Lucida Sans Unicode", L"Calibri", L"System" };
    HDC hdc = CreateCompatibleDC( 0 );
    unsigned int i;

    if (argc > 1) height = atoi( argv[1] );
    diag( hdc );
    printf( "ACP %u height %d\n", GetACP(), height );
    for (i = 0; i < ARRAY_SIZE(faces); i++) probe( hdc, faces[i], DEFAULT_CHARSET );
    probe( hdc, L"Tahoma", ANSI_CHARSET );
    probe( hdc, L"Arial", ANSI_CHARSET );
    probe( hdc, L"Tahoma", SHIFTJIS_CHARSET );
    probe( hdc, L"Tahoma", HANGEUL_CHARSET );
    probe( hdc, L"SimSun", GB2312_CHARSET );
    probe( hdc, L"MS UI Gothic", SHIFTJIS_CHARSET );
    probe( hdc, L"Arial", SHIFTJIS_CHARSET );
    probe( hdc, L"Arial", GB2312_CHARSET );
    DeleteDC( hdc );
    return 0;
}
