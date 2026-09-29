// UI latency setup (tools/uilat): new part with a 4x3x2 cm box, a sketch on XY
// holding a few lines (hover targets), left in sketch edit mode, front view, fitted.
// Leaves the part open for tools/uilat/uilat.py.
using System;
using Inventor;

static class Scenario
{
    public static void Run()
    {
        var app = H.App;
        PartDocument doc = null;
        H.Step("part + box", () =>
        {
            doc = H.NewPart();
            H.Box(doc, 0, 0, 4, 3, 2);
            return "ok";
        });
        H.Step("sketch edit", () =>
        {
            var def = doc.ComponentDefinition;
            var tg = app.TransientGeometry;
            var sk = def.Sketches.Add(def.WorkPlanes[3]);
            sk.SketchLines.AddByTwoPoints(tg.CreatePoint2d(-6, -4), tg.CreatePoint2d(-6, 7));  // vertical
            sk.SketchLines.AddByTwoPoints(tg.CreatePoint2d(-8, 5), tg.CreatePoint2d(10, 5));   // horizontal
            sk.Edit();
            var v = doc.Views[1];  // ActiveView is null early in a session on Wine (037)
            var cam = v.Camera;
            cam.ViewOrientationType = ViewOrientationTypeEnum.kFrontViewOrientation;
            cam.Fit();
            cam.Apply();
            return sk.Name + " in edit, view " + v.Width + "x" + v.Height
                + " hwnd 0x" + v.HWND.ToString("x");
        });
    }
}
