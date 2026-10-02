// 118: CJK/Cyrillic glyph rendering in a drawing note (Wine font fallback), left open for a screenshot. Not in `all`. INVSCEN_FONTS: ;-list of fonts, one note each.
using System;
using Inventor;

static class Scenario
{
    public static void Run()
    {
        H.Step("note", () =>
        {
            var app = H.App; var tg = app.TransientGeometry;
            var dd = (DrawingDocument)app.Documents.Add(DocumentTypeEnum.kDrawingDocumentObject, app.FileManager.GetTemplateFile(DocumentTypeEnum.kDrawingDocumentObject), true);
            string[] fonts = (System.Environment.GetEnvironmentVariable("INVSCEN_FONTS") ??
                "Arial;Tahoma;Segoe UI;MS Gothic;Yu Gothic;Microsoft YaHei;SimSun;Noto Sans CJK JP").Split(';');
            for (int i = 0; i < fonts.Length; i++)
                dd.ActiveSheet.DrawingNotes.GeneralNotes.AddFitted(tg.CreatePoint2d(3, 40 - 4 * i),
                    "<StyleOverride Font='" + fonts[i] + "' FontSize='1.5'>" + fonts[i] + ": Привет 測試 日本語 äöü Ω</StyleOverride>");
            app.ActiveView.Fit();
            return "ok";
        });
    }
}
