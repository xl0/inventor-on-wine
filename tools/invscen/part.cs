// New part: sketch 40x30 mm rectangle on XY, extrude 20 mm, check mass
// properties, save, close, reopen, recheck. API lengths are in cm.
using System;
using System.IO;
using Inventor;

static class Scenario
{
    const double X = 4, Y = 3, Z = 2;

    public static void Run()
    {
        var app = H.App;
        string path = H.Out + "\\box.ipt";
        PartDocument doc = null;
        Profile prof = null;

        H.Step("new part", () =>
        {
            string tpl = app.FileManager.GetTemplateFile(DocumentTypeEnum.kPartDocumentObject);
            doc = (PartDocument)app.Documents.Add(DocumentTypeEnum.kPartDocumentObject, tpl, true);
            return "template " + tpl;
        });
        H.Step("sketch rectangle", () =>
        {
            var def = doc.ComponentDefinition;
            var tg = app.TransientGeometry;
            var sk = def.Sketches.Add(def.WorkPlanes[3]);  // XY plane
            sk.SketchLines.AddAsTwoPointRectangle(tg.CreatePoint2d(0, 0), tg.CreatePoint2d(X, Y));
            prof = sk.Profiles.AddForSolid();
            return "lines " + sk.SketchLines.Count + ", profile paths " + prof.Count;
        });
        H.Step("extrude", () =>
        {
            var ext = doc.ComponentDefinition.Features.ExtrudeFeatures.AddByDistanceExtent(prof, Z,
                PartFeatureExtentDirectionEnum.kPositiveExtentDirection, PartFeatureOperationEnum.kNewBodyOperation);
            H.Check(ext.HealthStatus == HealthStatusEnum.kUpToDateHealth, "health " + ext.HealthStatus);
            return ext.Name + ", bodies " + doc.ComponentDefinition.SurfaceBodies.Count;
        });
        H.Step("mass properties", () => MassProps(doc));
        H.Step("view bitmap", () =>
        {
            app.ActiveView.GoHome();
            string bmp = H.Out + "\\box.png";
            app.ActiveView.SaveAsBitmap(bmp, 800, 600);
            return bmp + " " + new FileInfo(bmp).Length + " bytes";
        });
        H.Step("save", () =>
        {
            if (System.IO.File.Exists(path)) System.IO.File.Delete(path);
            doc.SaveAs(path, false);
            return path + " " + new FileInfo(path).Length + " bytes";
        });
        H.Step("close", () => { doc.Close(true); doc = null; return "docs open: " + app.Documents.Count; });
        H.Step("reopen", () =>
        {
            doc = (PartDocument)app.Documents.Open(path, true);
            var def = doc.ComponentDefinition;
            H.Check(def.Features.ExtrudeFeatures.Count == 1 && def.Sketches.Count == 1,
                "features " + def.Features.ExtrudeFeatures.Count + " sketches " + def.Sketches.Count);
            return doc.FullFileName;
        });
        H.Step("mass properties after reopen", () => MassProps(doc));
        H.Step("close reopened", () => { doc.Close(true); return "docs open: " + app.Documents.Count; });
    }

    // Box [0,X]x[0,Y]x[0,Z]: analytic volume, area, centroid; density = mass/volume.
    static string MassProps(PartDocument doc)
    {
        var mp = doc.ComponentDefinition.MassProperties;
        mp.Accuracy = MassPropertiesAccuracyEnum.k_VeryHigh;
        var c = mp.CenterOfMass;
        double v = mp.Volume;
        return string.Join(", ", new[] {
            H.Near("volume", v, X * Y * Z),
            H.Near("area", mp.Area, 2 * (X * Y + Y * Z + X * Z)),
            H.Near("cx", c.X, X / 2), H.Near("cy", c.Y, Y / 2), H.Near("cz", c.Z, Z / 2),
            string.Format("mass={0:R} g, density={1:R} g/cm3", mp.Mass * 1000, mp.Mass * 1000 / v) });
    }
}
