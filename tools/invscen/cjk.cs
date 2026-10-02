// 118: CJK/Cyrillic glyph rendering in a drawing note (Wine font fallback), left open for a screenshot. Not in `all`.
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
            string[] fonts = { "Noto Sans CJK JP", "Noto Serif CJK JP", "Arial" };
            for (int i = 0; i < fonts.Length; i++)
                dd.ActiveSheet.DrawingNotes.GeneralNotes.AddFitted(tg.CreatePoint2d(3, 40 - 6 * i),
                    "<StyleOverride Font='" + fonts[i] + "' FontSize='1.5'>" + fonts[i] + ": Привет 測試 日本語 äöü Ω</StyleOverride>");
            app.ActiveView.Fit();
            return "ok";
        });
    }
}
