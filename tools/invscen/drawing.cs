// Drawing: base view of box.ipt (from the part scenario) at scale 1 plus a
// projected view; check view extents against the 4 x 3 x 2 cm box; save, reopen.
using System;
using Inventor;

static class Scenario
{
    public static void Run()
    {
        var app = H.App;
        var tg = app.TransientGeometry;
        string part = H.Out + "\\..\\part\\box.ipt", path = null;
        DrawingDocument doc = null; Document model = null; DrawingView bv = null;

        H.Step("new drawing", () =>
        {
            H.Check(System.IO.File.Exists(part), part + " exists (run the part scenario first)");
            string tpl = app.FileManager.GetTemplateFile(DocumentTypeEnum.kDrawingDocumentObject);
            path = H.Out + "\\box" + System.IO.Path.GetExtension(tpl);  // default template may be .dwg
            doc = (DrawingDocument)app.Documents.Add(DocumentTypeEnum.kDrawingDocumentObject, tpl, true);
            model = app.Documents.Open(part, false);
            var s = doc.ActiveSheet;
            return System.IO.Path.GetFileName(tpl) + ", " + s.Name + " " + s.Width + " x " + s.Height + " cm";
        });
        H.Step("base view", () =>
        {
            bv = doc.ActiveSheet.DrawingViews.AddBaseView((_Document)model, tg.CreatePoint2d(10, 15), 1,
                ViewOrientationTypeEnum.kFrontViewOrientation, DrawingViewStyleEnum.kHiddenLineDrawingViewStyle);
            return H.Near("width", bv.Width, 4, 1e-3) + ", " + H.Near("height", bv.Height, 3, 1e-3) + ", curves " + bv.DrawingCurves.Count;
        });
        H.Step("projected view", () =>
        {
            var pv = doc.ActiveSheet.DrawingViews.AddProjectedView(bv, tg.CreatePoint2d(10, 25),
                DrawingViewStyleEnum.kFromBaseDrawingViewStyle);
            return H.Near("width", pv.Width, 4, 1e-3) + ", " + H.Near("height", pv.Height, 2, 1e-3);
        });
        H.Step("save", () =>
        {
            if (System.IO.File.Exists(path)) System.IO.File.Delete(path);
            doc.SaveAs(path, false);
            return path + " " + new System.IO.FileInfo(path).Length + " bytes";
        });
        H.Step("close", () => { doc.Close(true); model.Close(true); return "docs open: " + app.Documents.Count; });
        H.Step("reopen", () =>
        {
            doc = (DrawingDocument)app.Documents.Open(path, true);
            int n = doc.ActiveSheet.DrawingViews.Count;
            H.Check(n == 2, "views " + n);
            return doc.FullFileName + ", views " + n;
        });
        H.Step("close reopened", () => { doc.Close(true); return "docs open: " + app.Documents.Count; });
    }
}
