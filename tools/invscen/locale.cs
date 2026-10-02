// Environment variation (118): number/date handling under a non-English C library locale
// (run with LOCPATH/LC_ALL set). Prints what Inventor does; checks values, not formats. Not in `all`.
using System;
using Inventor;

static class Scenario
{
    public static void Run()
    {
        var app = H.App; var tg = app.TransientGeometry;
        PartDocument doc = null; Sheet sh = null; DrawingDocument dd = null;
        H.Step("locale info", () => "LocaleId 0x" + app.Locale.ToString("X") + ", language " + app.LanguageName + ", culture " + System.Globalization.CultureInfo.CurrentCulture.Name
);
        H.Step("expressions", () =>
        {
            doc = H.NewPart(); H.Box(doc, 0, 0, 4, 3, 2);
            var ps = doc.ComponentDefinition.Parameters.UserParameters;
            string r = "";
            foreach (var e in new[] { "12,5 mm", "12.5 mm", "1,5 in", "2 * 3,5 mm", "sin(30 deg)", "1.234,5 mm", "0,5 ul", "pow(2 ul; 3 ul)", "pow(2 ul, 3 ul)", "max(1 mm; 2 mm)" })
            {
                try { var p = ps.AddByExpression("Par_" + (ps.Count + 1), e, UnitsTypeEnum.kMillimeterLengthUnits); r += "[" + e + " => " + p.Value + " cm, '" + p.Expression + "'] "; }
                catch (Exception x) { r += "[" + e + " => 0x" + x.HResult.ToString("X8") + "] "; }
            }
            return r;
        });
        H.Step("units + iProperties", () =>
        {
            var ps = doc.PropertySets["Design Tracking Properties"];
            string cost = ""; try { ps["Cost"].Value = 12.5; cost = ps["Cost"].Value.ToString(); } catch (Exception) { cost = "n/a"; }
            object ct; try { ct = doc.PropertySets["Inventor Summary Information"]["Creation Time"].Value; } catch (Exception) { ct = "n/a"; }
            doc.PropertySets["Inventor User Defined Properties"].Add(new DateTime(2026, 3, 4, 5, 6, 7), "when");
            doc.PropertySets["Inventor User Defined Properties"].Add(1234.5, "num");
            return "length unit " + doc.UnitsOfMeasure.LengthUnits + " cost " + cost + " created " + ct + " user date '" + doc.PropertySets["Inventor User Defined Properties"]["when"].Value + "'";
        });
        H.Step("drawing + dimension text", () =>
        {
            doc.SaveAs(H.Out + "\\loc.ipt", false);
            dd = (DrawingDocument)app.Documents.Add(DocumentTypeEnum.kDrawingDocumentObject, app.FileManager.GetTemplateFile(DocumentTypeEnum.kDrawingDocumentObject), true);
            sh = dd.ActiveSheet;
            var bv = sh.DrawingViews.AddBaseView((_Document)doc, tg.CreatePoint2d(15, 12), 1, ViewOrientationTypeEnum.kFrontViewOrientation, DrawingViewStyleEnum.kHiddenLineDrawingViewStyle);
            DrawingCurve l = null, r = null;
            foreach (DrawingCurve dc in bv.get_DrawingCurves())
            {
                if (dc.CurveType != CurveTypeEnum.kLineSegmentCurve || Math.Abs(dc.StartPoint.X - dc.EndPoint.X) > 1e-6) continue;
                if (l == null || dc.StartPoint.X < l.StartPoint.X) l = dc;
                if (r == null || dc.StartPoint.X > r.StartPoint.X) r = dc;
            }
            var d = sh.DrawingDimensions.GeneralDimensions.AddLinear(tg.CreatePoint2d(15, bv.Top + 1), sh.CreateGeometryIntent(l), sh.CreateGeometryIntent(r), DimensionTypeEnum.kHorizontalDimensionType);
            var t = sh.DrawingNotes.GeneralNotes.AddFitted(tg.CreatePoint2d(2, 4), "Wert 12,5 und 12.5; Datum <DrawingPropertyValue/>");
            return "dim " + d.ModelValue + " text '" + d.Text.Text + "' formatted '" + d.Text.FormattedText + "'";
        });
        H.Step("save + export dxf/pdf", () =>
        {
            app.ActiveView.Fit(); app.ActiveView.SaveAsBitmap(H.Out + "\\loc.png", 1000, 700);
            H.Translate(dd, "{0AC6FD96-2F4D-42CE-8BE0-8AEA580399E4}", "loc.pdf");
            dd.SaveAs(H.Out + "\\loc" + System.IO.Path.GetExtension(app.FileManager.GetTemplateFile(DocumentTypeEnum.kDrawingDocumentObject)), false);
            return "ok";
        });
        H.Step("close", () => { app.Documents.CloseAll(false); return ""; });
    }
}
