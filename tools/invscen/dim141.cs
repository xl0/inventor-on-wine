// 141: drawing with a base view and one API-created linear dimension, left open and
// maximized for UI work on dimensions (the Edit Dimension dialog has a rich edit).
// INVSCEN_CMD: command (ControlDefinition internal name) to start on the selected
// dimension, without waiting. Run with INVSCEN_DIALOGS=off so the dialog stays.
using System;
using Inventor;

static class Scenario
{
    public static void Run()
    {
        var app = H.App;
        var tg = app.TransientGeometry;
        string part = System.Environment.GetEnvironmentVariable("INVSCEN_OPEN") ?? @"C:\t\samples\2022\Models\Parts\Plate\Vertical Plate.ipt";
        string cmd = System.Environment.GetEnvironmentVariable("INVSCEN_CMD");
        DrawingDocument doc = null; DrawingView bv = null; LinearGeneralDimension dim = null;

        H.Step("drawing + base view", () =>
        {
            string tpl = app.FileManager.GetTemplateFile(DocumentTypeEnum.kDrawingDocumentObject);
            doc = (DrawingDocument)app.Documents.Add(DocumentTypeEnum.kDrawingDocumentObject, tpl, true);
            var model = app.Documents.Open(part, false);
            var s = doc.ActiveSheet;
            bv = s.DrawingViews.AddBaseView((_Document)model, tg.CreatePoint2d(s.Width / 2, s.Height / 2), 1,
                ViewOrientationTypeEnum.kFrontViewOrientation, DrawingViewStyleEnum.kHiddenLineDrawingViewStyle);
            app.ActiveView.Fit();
            return "view " + bv.Width.ToString("G4") + " x " + bv.Height.ToString("G4") + " at " + bv.Center.X + "," + bv.Center.Y
                + " sheet " + s.Width + " x " + s.Height;
        });
        H.Step("dimension", () =>
        {
            var sh = doc.ActiveSheet;
            DrawingCurve l = null, r = null;
            foreach (DrawingCurve dc in bv.get_DrawingCurves())
            {
                if (dc.CurveType != CurveTypeEnum.kLineSegmentCurve) continue;
                if (Math.Abs(dc.StartPoint.X - dc.EndPoint.X) > 1e-6) continue;  // vertical only
                if (l == null || dc.StartPoint.X < l.StartPoint.X) l = dc;
                if (r == null || dc.StartPoint.X > r.StartPoint.X) r = dc;
            }
            dim = sh.DrawingDimensions.GeneralDimensions.AddLinear(tg.CreatePoint2d(bv.Center.X, bv.Top + 2),
                sh.CreateGeometryIntent(l), sh.CreateGeometryIntent(r), DimensionTypeEnum.kHorizontalDimensionType);
            return "value " + dim.ModelValue + " text '" + dim.Text.Text + "' at " + dim.Text.Origin.X + "," + dim.Text.Origin.Y;
        });
        if (cmd != null) H.Step("command on the dimension", () =>
        {
            doc.SelectSet.Clear();
            doc.SelectSet.Select(dim);
            app.CommandManager.ControlDefinitions[cmd].Execute2(false);
            return cmd + " started";
        });
    }
}
