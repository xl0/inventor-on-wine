// Cantilever beam for the Stress Analysis / Studio UI checks: steel bar 200 x 10 x 10 mm
// along +X (end faces x=0 and x=20 cm), saved as beam.ipt and left open (visible).
using System;
using Inventor;

static class Scenario
{
    public static void Run()
    {
        var app = H.App;
        H.Step("beam", () =>
        {
            var doc = H.NewPart();
            H.Box(doc, 0, 0, 1, 1, 20, plane: doc.ComponentDefinition.WorkPlanes[1]);
            Asset steel = null;
            foreach (Asset a in app.ActiveMaterialLibrary.MaterialAssets)
                if (a.DisplayName == "Steel") steel = a;
            H.Check(steel != null, "Steel");
            doc.ActiveMaterial = steel.CopyTo(doc);
            var r = doc.ComponentDefinition.RangeBox;
            app.ActiveView.GoHome();
            return H.Vol(doc, 20) + string.Format(", box {0},{1},{2}..{3},{4},{5}, ", r.MinPoint.X, r.MinPoint.Y,
                r.MinPoint.Z, r.MaxPoint.X, r.MaxPoint.Y, r.MaxPoint.Z) + H.Save(doc, "beam.ipt");
        });
    }
}
