// Drawing (Standard.idw): part = 4x3x2 box with a d=1 hole along Z at the
// centre. Base (front = XY) view at scale 1, projected, section through the
// hole, detail, retrieved + general dimensions, parts list, a second sheet
// from a sheet format; save .idw, reopen; PDF (2 pages), DWG and DXF
// export. View extents are analytic (cm on the sheet).
using System;
using Inventor;

static class Scenario
{
    // Shipped DWG/DXF export settings (the translators need one: E_INVALIDARG
    // otherwise), minus the transmittal zip.
    static string Ini(string name)
    {
        string t = H.App.FileManager.GetTemplateFile(DocumentTypeEnum.kPartDocumentObject);  // ...\Templates\en-US\x.ipt
        string src = System.IO.Path.GetDirectoryName(t) + "\\..\\..\\Design Data\\DWG-DXF\\" + name, dst = H.Out + "\\" + name;
        System.IO.File.WriteAllText(dst, System.IO.File.ReadAllText(src).Replace("USE TRANSMITTAL=Yes", "USE TRANSMITTAL=No"));
        return dst;
    }

    public static void Run()
    {
        var app = H.App;
        var tg = app.TransientGeometry;
        DrawingDocument doc = null; PartDocument model = null;
        DrawingView bv = null; Sheet sh = null;

        H.Step("model + new drawing", () =>
        {
            model = H.NewPart();
            H.Box(model, 0, 0, 4, 3, 2);
            var def = model.ComponentDefinition;
            var sk = def.Sketches.Add(def.WorkPlanes[3]);
            sk.SketchCircles.AddByCenterRadius(tg.CreatePoint2d(2, 1.5), 0.5);
            def.Features.ExtrudeFeatures.AddByThroughAllExtent(sk.Profiles.AddForSolid(),
                PartFeatureExtentDirectionEnum.kPositiveExtentDirection, PartFeatureOperationEnum.kCutOperation);
            H.Vol(model, 24 - Math.PI * 0.25 * 2, 1e-5);
            H.Save(model, "holed.ipt");
            string tpl = System.IO.Path.GetDirectoryName(app.FileManager.GetTemplateFile(DocumentTypeEnum.kPartDocumentObject))
                + "\\Standard.idw";
            doc = (DrawingDocument)app.Documents.Add(DocumentTypeEnum.kDrawingDocumentObject, tpl, true);
            sh = doc.ActiveSheet;
            return System.IO.Path.GetFileName(tpl) + ", " + sh.Name + " " + sh.Width + " x " + sh.Height + " cm, formats " + doc.SheetFormats.Count;
        });
        H.Step("base + projected views", () =>
        {
            bv = sh.DrawingViews.AddBaseView((_Document)model, tg.CreatePoint2d(10, 12), 1,
                ViewOrientationTypeEnum.kFrontViewOrientation, DrawingViewStyleEnum.kHiddenLineDrawingViewStyle);
            var top = sh.DrawingViews.AddProjectedView(bv, tg.CreatePoint2d(10, 20), DrawingViewStyleEnum.kFromBaseDrawingViewStyle);
            var iso = sh.DrawingViews.AddProjectedView(bv, tg.CreatePoint2d(20, 20), DrawingViewStyleEnum.kShadedDrawingViewStyle);
            int circles = 0;
            foreach (DrawingCurve c in bv.get_DrawingCurves()) if (c.CurveType == CurveTypeEnum.kCircleCurve) circles++;
            H.Check(circles == 1, "hole circles in base view " + circles);
            return string.Join(", ", new[] { H.Near("base w", bv.Width, 4, 1e-3), H.Near("base h", bv.Height, 3, 1e-3),
                H.Near("top w", top.Width, 4, 1e-3), H.Near("top h", top.Height, 2, 1e-3),
                "iso " + iso.Width.ToString("G4") + " x " + iso.Height.ToString("G4") });
        });
        H.Step("section view", () =>
        {
            var sk = bv.Sketches.Add();
            sk.Edit();
            var c = bv.Center;
            sk.SketchLines.AddByTwoPoints(sk.SheetToSketchSpace(tg.CreatePoint2d(c.X, c.Y + 2.5)),
                sk.SheetToSketchSpace(tg.CreatePoint2d(c.X, c.Y - 2.5)));
            sk.ExitEdit();
            var sv = sh.DrawingViews.AddSectionView(bv, sk, tg.CreatePoint2d(18, 12), DrawingViewStyleEnum.kFromBaseDrawingViewStyle);
            // cut along YZ through the hole: 3 x 2 (orientation depends on the line direction)
            H.Check(Math.Abs(sv.Width * sv.Height - 6) < 1e-2, "section " + sv.Width + " x " + sv.Height);
            int lines = 0;
            foreach (DrawingCurve dc in sv.get_DrawingCurves()) if (dc.CurveType == CurveTypeEnum.kLineSegmentCurve) lines++;
            return sv.Name + " " + sv.Width.ToString("G6") + " x " + sv.Height.ToString("G6") + ", lines " + lines;
        });
        H.Step("detail view", () =>
        {
            var c = bv.Center;
            var dv = sh.DrawingViews.AddDetailView(bv, tg.CreatePoint2d(26, 12), DrawingViewStyleEnum.kFromBaseDrawingViewStyle,
                true, c, 0.8, Type.Missing, 2.0);
            // extents include more than the 2 x 0.8 x 2 fence circle: compared with the VM
            H.Check(dv.Scale == 2 && dv.Width >= 3.2 - 1e-6 && dv.Height >= 3.2 - 1e-6, "scale " + dv.Scale);
            return dv.Name + " scale 2, " + dv.Width.ToString("G6") + " x " + dv.Height.ToString("G6");
        });
        H.Step("dimensions", () =>
        {
            var gd = sh.DrawingDimensions.GeneralDimensions;
            DrawingCurve l = null, r = null;
            foreach (DrawingCurve dc in bv.get_DrawingCurves())
            {
                if (dc.CurveType != CurveTypeEnum.kLineSegmentCurve) continue;
                if (Math.Abs(dc.StartPoint.X - dc.EndPoint.X) > 1e-6) continue;  // vertical only
                if (l == null || dc.StartPoint.X < l.StartPoint.X) l = dc;
                if (r == null || dc.StartPoint.X > r.StartPoint.X) r = dc;
            }
            var d = gd.AddLinear(tg.CreatePoint2d(bv.Center.X, bv.Top + 1), sh.CreateGeometryIntent(l), sh.CreateGeometryIntent(r),
                DimensionTypeEnum.kHorizontalDimensionType);
            var ret = gd.GetRetrievableDimensions(bv);
            int nret = 0;
            if (ret.Count > 0) nret = gd.Retrieve(bv, ret).Count;
            return H.Near("width dim", d.ModelValue, 4, 1e-9) + ", retrievable " + ret.Count + ", retrieved " + nret + ", total " + gd.Count;
        });
        H.Step("parts list", () =>
        {
            var pl = sh.PartsLists.Add(bv, tg.CreatePoint2d(sh.Width - 2, sh.Height - 2));
            H.Check(pl.PartsListRows.Count == 1, "rows " + pl.PartsListRows.Count);
            return "rows 1, columns " + pl.PartsListColumns.Count;
        });
        H.Step("sheet from sheet format", () =>
        {
            var fmt = doc.SheetFormats[1];
            var s2 = doc.Sheets.AddUsingSheetFormat(fmt, (_Document)model);
            H.Check(s2.DrawingViews.Count > 0, "views on " + fmt.Name);
            string r = fmt.Name + ": " + s2.Name + ", views " + s2.DrawingViews.Count + ", sheets " + doc.Sheets.Count;
            sh.Activate();
            return r;
        });
        H.Step("save .idw", () => H.Save(doc, "holed.idw"));
        // (SaveAs .dwg from an .idw opens the DWG export options wizard, on Windows
        // too, despite SilentOperation: use the translator; drawing.cs covers
        // Inventor .dwg save/reopen.)
        H.Step("export PDF", () =>
        {
            string s = H.Translate(doc, "{0AC6FD96-2F4D-42CE-8BE0-8AEA580399E4}", "holed.pdf", "Sheet_Range", PrintRangeEnum.kPrintAllSheets);
            string t = System.IO.File.ReadAllText(H.Out + "\\holed.pdf", System.Text.Encoding.GetEncoding(28591));
            H.Check(t.StartsWith("%PDF-"), "pdf header");
            int pages = System.Text.RegularExpressions.Regex.Matches(t, @"/Type\s*/Page\b").Count;
            H.Check(pages == 2, "pages " + pages);
            return s + ", pages " + pages;
        });
        H.Step("export DWG", () =>
        {
            string s = H.Translate(doc, "{C24E3AC2-122E-11D5-8E91-0010B541CD80}", "export.dwg", "Export_Acad_IniFile", Ini("exportdwg.ini"));
            foreach (var f in System.IO.Directory.GetFiles(H.Out, "export*.dwg"))
                H.Check(System.Text.Encoding.ASCII.GetString(System.IO.File.ReadAllBytes(f), 0, 4) == "AC10", f + " magic");
            return s;
        });
        H.Step("export DXF", () =>
        {
            string s = H.Translate(doc, "{C24E3AC4-122E-11D5-8E91-0010B541CD80}", "export.dxf", "Export_Acad_IniFile", Ini("exportdxf.ini"));
            var files = System.IO.Directory.GetFiles(H.Out, "export*.dxf");
            string t = System.IO.File.ReadAllText(files[0]);
            H.Check(t.Contains("ENTITIES"), "ENTITIES section");
            return s + ", CIRCLE in sheet 1: " + (t.Split(new[] { "\nCIRCLE" }, StringSplitOptions.None).Length - 1);
        });
        H.Step("close + reopen .idw", () =>
        {
            doc.Close(true); model.Close(true);
            doc = (DrawingDocument)app.Documents.Open(H.Out + "\\holed.idw", true);
            var s = doc.Sheets[1];
            H.Check(doc.Sheets.Count == 2 && s.DrawingViews.Count == 5 && s.PartsLists.Count == 1,
                "sheets " + doc.Sheets.Count + " views " + s.DrawingViews.Count + " partslists " + s.PartsLists.Count);
            return "sheets 2, views 5, dims " + s.DrawingDimensions.GeneralDimensions.Count;
        });
        H.Step("close", () => { app.Documents.CloseAll(false); return "docs open: " + app.Documents.Count; });
    }
}
