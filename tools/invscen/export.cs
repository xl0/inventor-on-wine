// File I/O: holed box (4x3x2 cm, d=1 through hole along Z) exported to STEP,
// IGES, SAT, Parasolid, STL (binary + ASCII, mm), JPG/BMP/PNG views; STEP,
// IGES, SAT and Parasolid imported back (volume must match); STL checked
// structurally (triangle count consistency, bounding box, mesh volume).
using System;
using System.Linq;
using System.Text.RegularExpressions;
using Inventor;

static class Scenario
{
    const double V = 24 - Math.PI * 0.25 * 2;
    static PartDocument doc;

    static string Text(string name) { return System.IO.File.ReadAllText(H.Out + "\\" + name); }
    static int Count(string text, string pat) { return Regex.Matches(text, pat).Count; }

    // Reopen an exported file with Inventor (import) and compare the volume
    // (SAT imports as an assembly).
    static string Import(string name)
    {
        var d = H.App.Documents.Open(H.Out + "\\" + name, true);
        try
        {
            object def = d.DocumentType == DocumentTypeEnum.kPartDocumentObject ? (object)((PartDocument)d).ComponentDefinition
                : ((AssemblyDocument)d).ComponentDefinition;
            return name + " imported as " + d.DocumentType + ", " + H.Near("volume", H.Mass(def).Volume, V, 1e-5);
        }
        finally { d.Close(true); }
    }

    // Binary STL: header, count, 50-byte triangles; bbox and signed volume (mm).
    static string Stl(string name, out int n)
    {
        var b = System.IO.File.ReadAllBytes(H.Out + "\\" + name);
        n = BitConverter.ToInt32(b, 80);
        H.Check(b.Length == 84 + 50 * n, "size " + b.Length + " for " + n + " triangles");
        double[] lo = { 1e9, 1e9, 1e9 }, hi = { -1e9, -1e9, -1e9 };
        double vol = 0;
        for (int i = 0; i < n; i++)
        {
            var p = new double[3, 3];
            for (int k = 0; k < 3; k++)
                for (int c = 0; c < 3; c++)
                {
                    p[k, c] = BitConverter.ToSingle(b, 84 + 50 * i + 12 + 12 * k + 4 * c);
                    lo[c] = Math.Min(lo[c], p[k, c]); hi[c] = Math.Max(hi[c], p[k, c]);
                }
            vol += (p[0, 0] * (p[1, 1] * p[2, 2] - p[1, 2] * p[2, 1]) - p[0, 1] * (p[1, 0] * p[2, 2] - p[1, 2] * p[2, 0])
                + p[0, 2] * (p[1, 0] * p[2, 1] - p[1, 1] * p[2, 0])) / 6;
        }
        for (int c = 0; c < 3; c++)
            H.Check(Math.Abs(lo[c]) < 1e-3 && Math.Abs(hi[c] - new[] { 40.0, 30, 20 }[c]) < 1e-3, "bbox axis " + c + ": " + lo[c] + ".." + hi[c]);
        // inscribed polygon for the hole: mesh volume slightly above the exact one
        H.Check(vol >= V * 1000 - 1e-2 && vol < V * 1000 * 1.01, "mesh volume " + vol);
        H.Check(n == 140, "triangles " + n);  // default resolution, VM reference
        return name + ": " + n + " triangles, bbox 40x30x20 mm, mesh volume " + vol.ToString("F3") + " mm3";
    }

    // View image: format, size and how much of it is non-background / light.
    static string Image(string name)
    {
        string p = H.Out + "\\" + name;
        if (System.IO.File.Exists(p)) System.IO.File.Delete(p);
        doc.Views[1].SaveAsBitmap(p, 640, 480);
        using (var bmp = new System.Drawing.Bitmap(p))
        {
            H.Check(bmp.Width == 640 && bmp.Height == 480, "size " + bmp.Width + "x" + bmp.Height);
            int light = 0, total = 0;
            for (int y = 0; y < bmp.Height; y += 4)
                for (int x = 0; x < bmp.Width; x += 4, total++)
                    if (bmp.GetPixel(x, y).GetBrightness() > 0.5) light++;
            double f = (double)light / total;
            // Windows: shaded light-grey box on a dark gradient, 39.1% light (VM reference)
            H.Check(Math.Abs(f - 0.391) < 0.05, "light (shaded) pixels " + f.ToString("P1"));
            return name + " " + new System.IO.FileInfo(p).Length + " bytes, " + bmp.RawFormat.Guid.ToString().Substring(0, 8) + ", light " + f.ToString("P1");
        }
    }

    static void S(string name, Func<string> body) { H.Reset(); H.Step(name, body); }

    public static void Run()
    {
        var app = H.App;
        var tg = app.TransientGeometry;

        H.Step("model", () =>
        {
            doc = H.NewPart();
            H.Box(doc, 0, 0, 4, 3, 2);
            var def = doc.ComponentDefinition;
            var sk = def.Sketches.Add(def.WorkPlanes[3]);
            sk.SketchCircles.AddByCenterRadius(tg.CreatePoint2d(2, 1.5), 0.5);
            def.Features.ExtrudeFeatures.AddByThroughAllExtent(sk.Profiles.AddForSolid(),
                PartFeatureExtentDirectionEnum.kPositiveExtentDirection, PartFeatureOperationEnum.kCutOperation);
            return H.Vol(doc, V, 1e-5) + ", " + H.Save(doc, "src.ipt")  /* not holed.ipt: importing holed.stp would clash with it */;
        });
        S("STEP export", () =>
        {
            string s = H.Translate(doc, "{90AF7F40-0C01-11D5-8E83-0010B541CD80}", "holed.stp", "ApplicationProtocolType", 3);
            string t = Text("holed.stp");
            H.Check(t.StartsWith("ISO-10303-21;"), "header");
            int breps = Count(t, @"=\s*MANIFOLD_SOLID_BREP\("), faces = Count(t, @"=\s*ADVANCED_FACE\("), cyl = Count(t, @"=\s*CYLINDRICAL_SURFACE\(");
            // structure as written by Inventor 2027.1 on Windows (VM reference)
            H.Check(breps == 1 && faces == 7 && cyl == 1 && Count(t, @"\n#\d+\s*=") == 249, "breps " + breps + " faces " + faces + " cylinders " + cyl);
            return s + ", " + Regex.Match(t, @"FILE_SCHEMA\s*\(\s*\(\s*'([^' ]+)").Groups[1].Value + ", entities " + Count(t, @"\n#\d+\s*=")
                + ", brep 1, faces " + faces + ", cylinders " + cyl;
        });
        S("STEP import", () => Import("holed.stp"));
        S("IGES export", () =>
        {
            string s = H.Translate(doc, "{90AF7F44-0C01-11D5-8E83-0010B541CD80}", "holed.igs");
            var lines = System.IO.File.ReadAllLines(H.Out + "\\holed.igs");
            H.Check(lines.All(l => l.Length == 80), "80-column records");
            var sec = lines.GroupBy(l => l[72]).ToDictionary(g => g.Key, g => g.Count());
            H.Check(sec.ContainsKey('S') && sec.ContainsKey('G') && sec.ContainsKey('D') && sec.ContainsKey('P') && sec['T'] == 1, "sections");
            var types = lines.Where(l => l[72] == 'D').Where((l, i) => i % 2 == 0).GroupBy(l => int.Parse(l.Substring(0, 8)))
                .OrderBy(g => g.Key).Select(g => g.Key + ":" + g.Count());
            string hist = string.Join(" ", types);
            H.Check(hist == "126:15 128:7 186:1 314:1 406:1 502:1 504:1 508:9 510:7 514:1", "entity types " + hist);  // VM reference
            return s + ", D entries " + sec['D'] / 2 + " (" + hist + ")";
        });
        S("IGES import", () => Import("holed.igs"));
        S("SAT export", () =>
        {
            string s = H.Translate(doc, "{89162634-02B6-11D5-8E80-0010B541CD80}", "holed.sat");
            string t = Text("holed.sat");
            H.Check(Count(t, @"\bface\s") == 7 && Count(t, @"cone-surface") == 1, "faces");  // VM reference
            return s + ", header " + t.Split('\n')[0].Trim() + ", faces 7, cone-surface 1";
        });
        S("SAT import", () => Import("holed.sat"));
        S("Parasolid export", () =>
        {
            string s = H.Translate(doc, "{8F9D3571-3CB8-42F7-8AFF-2DB2779C8465}", "holed.x_t");
            H.Check(Text("holed.x_t").Contains("PARASOLID"), "Parasolid header");
            return s;
        });
        S("Parasolid import", () => Import("holed.x_t"));
        int nb = 0;
        S("STL binary", () =>
        {
            string s = H.Translate(doc, "{533E9A98-FC3B-11D4-8E7E-0010B541CD80}", "holed.stl", "OutputFileType", 0, "ExportUnits", 5);
            return s + ", " + Stl("holed.stl", out nb);
        });
        S("STL ASCII", () =>
        {
            string s = H.Translate(doc, "{533E9A98-FC3B-11D4-8E7E-0010B541CD80}", "holed_a.stl", "OutputFileType", 1, "ExportUnits", 5);
            string t = Text("holed_a.stl");
            H.Check(t.TrimStart().StartsWith("solid"), "solid header");
            int n = Count(t, @"facet normal"), v = Count(t, @"\bvertex\s");
            H.Check(v == 3 * n && (nb == 0 || n == nb), "facets " + n + " vertices " + v + " binary " + nb);
            return s + ", facets " + n;
        });
        S("view BMP", () => { doc.Views[1].GoHome(); return Image("view.bmp"); });
        S("view JPG", () => Image("view.jpg"));
        S("view PNG", () => Image("view.png"));
        S("close", () => { doc.Close(true); return "docs open: " + app.Documents.Count; });
    }
}
