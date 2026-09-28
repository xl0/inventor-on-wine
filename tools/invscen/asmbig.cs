// Generated assembly: 200 occurrences of a 4x3x2 box on a 20 x 10 grid
// (pitch 5), plus one sub-assembly of 4 more; volume, BOM counts, save/
// close/reopen and a full rebuild are timed. cm.
using System;
using Inventor;

static class Scenario
{
    const int NX = 20, NY = 10, N = NX * NY;

    public static void Run()
    {
        var app = H.App;
        var tg = app.TransientGeometry;
        string box = H.Out + "\\box.ipt", sub = H.Out + "\\sub4.iam";
        AssemblyDocument doc = null;

        H.Step("make part + sub-assembly", () =>
        {
            var p = H.NewPart();
            H.Box(p, 0, 0, 4, 3, 2);
            H.Save(p, "box.ipt");
            p.Close(true);
            var a = (AssemblyDocument)app.Documents.Add(DocumentTypeEnum.kAssemblyDocumentObject,
                app.FileManager.GetTemplateFile(DocumentTypeEnum.kAssemblyDocumentObject), true);
            var m = tg.CreateMatrix();
            for (int i = 0; i < 4; i++) { m.SetTranslation(tg.CreateVector(0, 0, 2 * i)); a.ComponentDefinition.Occurrences.Add(box, m); }
            string s = H.Save(a, "sub4.iam");
            a.Close(true);
            return s;
        });
        H.Step("place " + N + " occurrences", () =>
        {
            doc = (AssemblyDocument)app.Documents.Add(DocumentTypeEnum.kAssemblyDocumentObject,
                app.FileManager.GetTemplateFile(DocumentTypeEnum.kAssemblyDocumentObject), true);
            var occs = doc.ComponentDefinition.Occurrences;
            var m = tg.CreateMatrix();
            for (int i = 0; i < NX; i++)
                for (int j = 0; j < NY; j++)
                {
                    m.SetTranslation(tg.CreateVector(5 * i, 5 * j, 0));
                    occs.Add(box, m).Grounded = true;
                }
            m.SetTranslation(tg.CreateVector(-10, 0, 0));
            occs.Add(sub, m);
            return "occurrences " + occs.Count;
        }, 600);
        H.Step("mass properties", () =>
        {
            var mp = H.Mass(doc.ComponentDefinition);
            // centroid x: boxes at 5i + 2 (i < 20) plus 4 boxes at -8
            double cx = (N * (5 * (NX - 1) / 2.0 + 2) + 4 * -8.0) / (N + 4);
            return H.Near("volume", mp.Volume, 24 * (N + 4), 1e-6) + ", " + H.Near("cx", mp.CenterOfMass.X, cx, 1e-6);
        }, 300);
        H.Step("BOM", () =>
        {
            var bom = doc.ComponentDefinition.BOM;
            bom.StructuredViewEnabled = true;
            bom.PartsOnlyViewEnabled = true;
            var st = bom.BOMViews["Structured"]; var po = bom.BOMViews["Parts Only"];
            H.Check(st.BOMRows.Count == 2 && po.BOMRows.Count == 1, "rows " + st.BOMRows.Count + "/" + po.BOMRows.Count);
            H.Check(po.BOMRows[1].ItemQuantity == N + 4, "parts-only qty " + po.BOMRows[1].ItemQuantity);
            return "structured rows 2, parts-only qty " + (N + 4);
        });
        H.Step("save", () => H.Save(doc, "big.iam"), 600);
        H.Step("close", () => { doc.Close(true); return "docs open: " + app.Documents.Count; });
        H.Step("reopen", () =>
        {
            doc = (AssemblyDocument)app.Documents.Open(H.Out + "\\big.iam", true);
            int n = doc.ComponentDefinition.Occurrences.Count;
            H.Check(n == N + 1, "occurrences " + n);
            return "occurrences " + n + ", leaf " + doc.ComponentDefinition.Occurrences.AllLeafOccurrences.Count;
        }, 600);
        H.Step("rebuild all", () => { doc.Rebuild2(true); return H.Near("volume", H.Mass(doc.ComponentDefinition).Volume, 24 * (N + 4), 1e-6); }, 600);
        H.Step("close reopened", () => { doc.Close(true); return "docs open: " + app.Documents.Count; });
    }
}
