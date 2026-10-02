/* 119: GDI font linking of CJK characters per base font: GetGlyphIndices (never linked),
 * GetGlyphOutline GGO_NATIVE size + metrics and GetTextExtentPoint32 of U+6E2C U+65E5 U+4E2D U+AC00 'A'.
 * A linked glyph shows a big outline and a full-width advance; .notdef a small box.
 * cjk_link.exe [HEIGHT] (lfHeight, default -32)
 * x86_64-w64-mingw32-gcc -O2 -o cjk_link.exe cjk_link.c -lgdi32 */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

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

int main( int argc, char **argv )
{
    static const WCHAR *faces[] = { L"Tahoma", L"Segoe UI", L"Microsoft Sans Serif", L"MS Sans Serif",
        L"MS Shell Dlg", L"MS Shell Dlg 2", L"Arial", L"Times New Roman", L"Courier New", L"Verdana",
        L"MS Gothic", L"MS UI Gothic", L"SimSun", L"Meiryo UI", L"Yu Gothic UI", L"Microsoft YaHei UI",
        L"Lucida Sans Unicode", L"Calibri", L"System" };
    HDC hdc = CreateCompatibleDC( 0 );
    unsigned int i;

    if (argc > 1) height = atoi( argv[1] );
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
