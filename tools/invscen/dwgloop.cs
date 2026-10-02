// DWG export loop (109): drawing of a holed box on two sheets (like drawing2),
// then INVSCEN_N (default 10) DWG exports through the translator add-in. Each
// export runs a DBXBridge.exe translator server and releases its proxies from
// several threads at the end (109: the server deadlocked in RemRelease).
using System;
using Inventor;

static class Scenario
{
    public static void Run()
    {
        var app = H.App;
        var tg = app.TransientGeometry;
        DrawingDocument doc = null; PartDocument model = null;
        int n = int.Parse(System.Environment.GetEnvironmentVariable("INVSCEN_N") ?? "10");
        string ini = null;

        H.Step("model + drawing", () =>
        {
            model = H.NewPart();
            H.Box(model, 0, 0, 4, 3, 2);
            H.Save(model, "holed.ipt");
            string tpl = System.IO.Path.GetDirectoryName(app.FileManager.GetTemplateFile(DocumentTypeEnum.kPartDocumentObject));
            doc = (DrawingDocument)app.Documents.Add(DocumentTypeEnum.kDrawingDocumentObject, tpl + "\\Standard.idw", true);
            var sh = doc.ActiveSheet;
            var bv = sh.DrawingViews.AddBaseView((_Document)model, tg.CreatePoint2d(10, 12), 1,
                ViewOrientationTypeEnum.kFrontViewOrientation, DrawingViewStyleEnum.kHiddenLineDrawingViewStyle);
            sh.DrawingViews.AddProjectedView(bv, tg.CreatePoint2d(10, 20), DrawingViewStyleEnum.kFromBaseDrawingViewStyle);
            doc.Sheets.AddUsingSheetFormat(doc.SheetFormats[1], (_Document)model);
            sh.Activate();
            string src = tpl + "\\..\\..\\Design Data\\DWG-DXF\\exportdwg.ini";
            ini = H.Out + "\\exportdwg.ini";
            System.IO.File.WriteAllText(ini, System.IO.File.ReadAllText(src).Replace("USE TRANSMITTAL=Yes", "USE TRANSMITTAL=No"));
            return H.Save(doc, "holed.idw");
        });
        for (int i = 1; i <= n; i++)
            H.Step("export DWG " + i, () =>
                H.Translate(doc, "{C24E3AC2-122E-11D5-8E91-0010B541CD80}", "export.dwg", "Export_Acad_IniFile", ini));
        H.Step("close", () => { app.Documents.CloseAll(false); return "docs open: " + app.Documents.Count; });
    }
}
