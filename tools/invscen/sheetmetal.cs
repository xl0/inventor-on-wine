// Sheet metal (Metric template): 10 x 6 cm face, t = 2 mm; flat pattern of
// the face alone is the sketch rectangle; a 90 deg flange of height h = 3 cm
// (outer datum) on a 10 cm edge unfolds to 6 + h - 2 (R + t) + pi/2 (R + K t);
// flat pattern DXF export.
using System;
using Inventor;

static class Scenario
{
    public static void Run()
    {
        var app = H.App;
        var tg = app.TransientGeometry;
        PartDocument doc = null;
        SheetMetalComponentDefinition def = null;
        const double t = 0.2, h = 3;
        double R = 0, K = 0;

        H.Step("new sheet metal part", () =>
        {
            string tpl = System.IO.Path.GetDirectoryName(app.FileManager.GetTemplateFile(DocumentTypeEnum.kPartDocumentObject))
                + "\\Metric\\Sheet Metal (mm).ipt";
            doc = H.NewPart(tpl);
            def = (SheetMetalComponentDefinition)doc.ComponentDefinition;
            def.UseSheetMetalStyleThickness = false;
            def.Thickness.Expression = "2 mm";
            R = (double)def.BendRadius.Value;
            K = double.Parse(def.UnfoldMethod.kFactor.Split(' ')[0], System.Globalization.CultureInfo.InvariantCulture);
            return def.ActiveSheetMetalStyle.Name + ", R=" + R + " cm, K=" + K + " (" + def.UnfoldMethod.Name + ")";
        });
        H.Step("face", () =>
        {
            var sk = def.Sketches.Add(def.WorkPlanes[3]);
            sk.SketchLines.AddAsTwoPointRectangle(tg.CreatePoint2d(0, 0), tg.CreatePoint2d(10, 6));
            var ff = ((SheetMetalFeatures)def.Features).FaceFeatures;
            ff.Add(ff.CreateFaceFeatureDefinition(sk.Profiles.AddForSolid()));
            return H.Vol(doc, 10 * 6 * t);
        });
        H.Step("flat pattern (face)", () =>
        {
            def.Unfold();
            var fp = def.FlatPattern;
            string r = H.Near("length", Math.Max(fp.Length, fp.Width), 10, 1e-9) + ", "
                + H.Near("width", Math.Min(fp.Length, fp.Width), 6, 1e-9);
            fp.ExitEdit();
            fp.Delete();
            return r;
        });
        H.Step("flange", () =>
        {
            Edge e = null; double z = double.MinValue;
            foreach (Edge c in def.SurfaceBodies[1].Edges)
            {
                Point p = c.StartVertex.Point, q = c.StopVertex.Point;
                if (Math.Abs(p.Y - 6) < 1e-9 && Math.Abs(q.Y - 6) < 1e-9 && Math.Abs(Math.Abs(p.X - q.X) - 10) < 1e-9 && p.Z > z)
                { e = c; z = p.Z; }
            }
            H.Check(e != null, "10 cm edge at y=6");
            var ec = app.TransientObjects.CreateEdgeCollection();
            ec.Add(e);
            var fl = ((SheetMetalFeatures)def.Features).FlangeFeatures;
            var fd = fl.CreateFlangeDefinition(ec, "90 deg", h);
            fd.SetDistanceHeightExtent(h, PartFeatureExtentDirectionEnum.kPositiveExtentDirection, HeightDatumTypeEnum.kHeightDatumOuter);
            var f = fl.Add(fd);
            doc.Update();
            var rb = def.RangeBox;
            H.Check(Math.Abs(rb.MaxPoint.Z - h) < 1e-9 && Math.Abs(rb.MaxPoint.Y - 6) < 1e-9, "range " + rb.MaxPoint.Y + " " + rb.MaxPoint.Z);
            // straight base + straight flange (both shortened by R + t) + quarter annulus bend
            return f.Name + ", " + H.Vol(doc, 10 * t * (6 + h - 2 * (R + t)) + Math.PI / 4 * ((R + t) * (R + t) - R * R) * 10);
        });
        H.Step("flat pattern (face + flange)", () =>
        {
            def.Unfold();
            var fp = def.FlatPattern;
            double L = 6 + h - 2 * (R + t) + Math.PI / 2 * (R + K * t);
            var mp = fp.MassProperties;
            string r = H.Near("length", Math.Max(fp.Length, fp.Width), 10, 1e-9) + ", "
                + H.Near("width", Math.Min(fp.Length, fp.Width), L, 1e-6) + ", "
                + H.Near("flat volume", mp.Volume, 10 * L * t, 1e-5)
                + ", bends " + fp.FlatBendResults.Count;
            fp.ExitEdit();
            return r;
        });
        H.Step("save + reopen", () =>
        {
            string s = H.Save(doc, "sheet.ipt");
            doc.Close(true);
            doc = (PartDocument)app.Documents.Open(H.Out + "\\sheet.ipt", true);
            def = (SheetMetalComponentDefinition)doc.ComponentDefinition;
            H.Check(def.HasFlatPattern, "flat pattern kept");
            return s + ", flat " + def.FlatPattern.Length + " x " + def.FlatPattern.Width;
        });
        H.Step("export flat pattern DXF", () =>
        {
            string p = H.Out + "\\flat.dxf";
            if (System.IO.File.Exists(p)) System.IO.File.Delete(p);
            def.DataIO.WriteDataToFile("FLAT PATTERN DXF?AcadVersion=2018&OuterProfileLayer=Outer", p);
            string txt = System.IO.File.ReadAllText(p);
            H.Check(txt.Contains("Outer"), "Outer layer in dxf");
            return p + " " + txt.Length + " chars, LINE " + (txt.Split(new[] { "\nLINE" }, StringSplitOptions.None).Length - 1);
        });
        H.Step("close", () => { doc.Close(true); return "docs open: " + app.Documents.Count; });
    }
}
