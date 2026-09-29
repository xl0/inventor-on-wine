// Frame Generator setup: skeleton part (sketch rectangle 100 x 50 cm on XY) placed in
// frame.iam, both saved and left open for the Insert Frame UI. INVSCEN_FRAME=check
// (with INVSCEN_KEEP=1) lists the generated leaf parts (frame members, Design
// Accelerator shaft/gears) with their longest extent and volume.
using System;
using System.Linq;
using Inventor;

static class Scenario
{
    public static void Run()
    {
        var app = H.App;
        var tg = app.TransientGeometry;
        if (System.Environment.GetEnvironmentVariable("INVSCEN_FRAME") == "check")
        {
            H.Step("members", () =>
            {
                var asm = (AssemblyDocument)app.Documents.Open(H.Out + "\\frame.iam", true);
                var names = new System.Collections.Generic.List<string>();
                foreach (ComponentOccurrence o in asm.ComponentDefinition.Occurrences.AllLeafOccurrences)
                {
                    var d = (Document)o.Definition.Document;
                    if (d.FullFileName.EndsWith("skeleton.ipt")) continue;
                    var p = (PartDocument)d;
                    var r = p.ComponentDefinition.RangeBox;
                    double len = new[] { r.MaxPoint.X - r.MinPoint.X, r.MaxPoint.Y - r.MinPoint.Y, r.MaxPoint.Z - r.MinPoint.Z }.Max();
                    names.Add(System.IO.Path.GetFileName(d.FullFileName) + " " + len.ToString("F2") + " cm, "
                        + H.Mass(p.ComponentDefinition).Volume.ToString("F2") + " cm3");
                }
                return names.Count + " members: " + string.Join("; ", names);
            });
            return;
        }
        H.Step("skeleton", () =>
        {
            var p = H.NewPart();
            var def = p.ComponentDefinition;
            var sk = def.Sketches.Add(def.WorkPlanes[3]);
            sk.SketchLines.AddAsTwoPointRectangle(tg.CreatePoint2d(0, 0), tg.CreatePoint2d(100, 50));
            string s = H.Save(p, "skeleton.ipt");
            p.Close(true);
            return s;
        });
        H.Step("assembly", () =>
        {
            var a = (AssemblyDocument)app.Documents.Add(DocumentTypeEnum.kAssemblyDocumentObject,
                app.FileManager.GetTemplateFile(DocumentTypeEnum.kAssemblyDocumentObject), true);
            var occ = a.ComponentDefinition.Occurrences.Add(H.Out + "\\skeleton.ipt", tg.CreateMatrix());
            occ.Grounded = true;
            return H.Save(a, "frame.iam");
        });
    }
}
