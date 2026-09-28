// Assembly: place box.ipt (from the part scenario) twice, ground the first,
// mate top face of #1 to bottom face of #2, check #2 lands at z = 2 cm and
// the assembly volume is 2 x 24 cm3; save, close, reopen, recheck.
using System;
using Inventor;

static class Scenario
{
    // Planar face of occ with |normal.z| = 1 and extreme z (top: max, else min).
    static Face ZFace(ComponentOccurrence occ, bool top)
    {
        Face best = null; double bz = 0;
        foreach (SurfaceBody b in occ.SurfaceBodies)
            foreach (Face f in b.Faces)
            {
                if (f.SurfaceType != SurfaceTypeEnum.kPlaneSurface) continue;
                var n = ((Plane)f.Geometry).Normal;
                if (Math.Abs(Math.Abs(n.Z) - 1) > 1e-9) continue;
                double z = f.PointOnFace.Z;
                if (best == null || (top ? z > bz : z < bz)) { best = f; bz = z; }
            }
        H.Check(best != null, "z face of " + occ.Name);
        return best;
    }

    static string Verify(AssemblyDocument doc)
    {
        var def = doc.ComponentDefinition;
        H.Check(def.Occurrences.Count == 2 && def.Constraints.Count == 1,
            "occurrences " + def.Occurrences.Count + " constraints " + def.Constraints.Count);
        var t = def.Occurrences[2].Transformation.Translation;
        var mp = def.MassProperties;
        mp.Accuracy = MassPropertiesAccuracyEnum.k_VeryHigh;
        return string.Join(", ", new[] {
            H.Near("occ2.z", t.Z + 1, 2 + 1),  // +1: tolerance relative to a nonzero value
            H.Near("volume", mp.Volume, 48), H.Near("cz", mp.CenterOfMass.Z, 2) });
    }

    public static void Run()
    {
        var app = H.App;
        var tg = app.TransientGeometry;
        string part = H.Out + "\\..\\part\\box.ipt", path = H.Out + "\\pair.iam";
        AssemblyDocument doc = null;

        H.Step("new assembly", () =>
        {
            H.Check(System.IO.File.Exists(part), part + " exists (run the part scenario first)");
            doc = (AssemblyDocument)app.Documents.Add(DocumentTypeEnum.kAssemblyDocumentObject,
                app.FileManager.GetTemplateFile(DocumentTypeEnum.kAssemblyDocumentObject), true);
            return doc.DisplayName;
        });
        H.Step("place parts", () =>
        {
            var occs = doc.ComponentDefinition.Occurrences;
            var m = tg.CreateMatrix();
            var o1 = occs.Add(part, m);
            o1.Grounded = true;
            m.SetTranslation(tg.CreateVector(10, 0, 5));
            var o2 = occs.Add(part, m);
            return o1.Name + ", " + o2.Name + " at z=" + o2.Transformation.Translation.Z;
        });
        H.Step("mate constraint", () =>
        {
            var occs = doc.ComponentDefinition.Occurrences;
            var c = doc.ComponentDefinition.Constraints.AddMateConstraint(ZFace(occs[1], true), ZFace(occs[2], false), 0);
            doc.Update();
            return c.Name + " health " + c.HealthStatus;
        });
        H.Step("verify", () => Verify(doc));
        H.Step("save", () =>
        {
            if (System.IO.File.Exists(path)) System.IO.File.Delete(path);
            doc.SaveAs(path, false);
            return path + " " + new System.IO.FileInfo(path).Length + " bytes";
        });
        H.Step("close", () => { doc.Close(true); return "docs open: " + app.Documents.Count; });
        H.Step("reopen", () => { doc = (AssemblyDocument)app.Documents.Open(path, true); return doc.FullFileName; });
        H.Step("verify after reopen", () => Verify(doc));
        H.Step("close reopened", () => { doc.Close(true); return "docs open: " + app.Documents.Count; });
    }
}
