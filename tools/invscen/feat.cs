// Part features, one fresh part each (independent sections): fillet, chamfer,
// simple + threaded hole, rectangular + circular pattern, shell, revolve,
// sweep, loft, work plane/axis. Each checks the body volume analytically
// (threaded hole: bounds, exact value compared to the VM reference). cm, rad.
using System;
using Inventor;

static class Scenario
{
    static Application app;
    static TransientGeometry tg;

    // One independent section: new part, build() returns expected volume
    // (or NaN: build checked itself), close.
    static void Feature(string name, Func<PartDocument, object> build, double tol = 1e-5)
    {
        H.Reset();
        H.Step(name, () =>
        {
            var doc = H.NewPart();
            try
            {
                object r = build(doc);
                if (r is string) return (string)r;
                return H.Vol(doc, (double)r, tol);
            }
            finally { doc.Close(true); }
        });
    }

    static SketchPoint HoleCenter(PartDocument doc, object plane, double x, double y)
    {
        var sk = doc.ComponentDefinition.Sketches.Add(plane);
        return sk.SketchPoints.Add(tg.CreatePoint2d(x, y), true);
    }

    static HoleFeature ThroughHole(PartDocument doc, object plane, double x, double y, object dia,
        PartFeatureExtentDirectionEnum dir = PartFeatureExtentDirectionEnum.kSymmetricExtentDirection)
    {
        var pts = app.TransientObjects.CreateObjectCollection();
        pts.Add(HoleCenter(doc, plane, x, y));
        var hf = doc.ComponentDefinition.Features.HoleFeatures;
        return hf.AddDrilledByThroughAllExtent(hf.CreateSketchPlacementDefinition(pts), dia, dir);
    }

    public static void Run()
    {
        app = H.App; tg = app.TransientGeometry;
        const double PI = Math.PI;

        // Box 4x3x2; round the vertical edge at (4,3): removes (r^2 - pi r^2/4) * 2.
        Feature("fillet", doc =>
        {
            H.Box(doc, 0, 0, 4, 3, 2);
            var ec = app.TransientObjects.CreateEdgeCollection();
            ec.Add(H.EdgeAt(doc, new[] { 4.0, 3, 0 }, new[] { 4.0, 3, 2 }));
            doc.ComponentDefinition.Features.FilletFeatures.AddSimple(ec, 0.5);
            return 24 - 2 * 0.25 * (1 - PI / 4);
        });
        // Equal-distance chamfer d=0.5 on the same edge: removes d^2/2 * 2.
        Feature("chamfer", doc =>
        {
            H.Box(doc, 0, 0, 4, 3, 2);
            var ec = app.TransientObjects.CreateEdgeCollection();
            ec.Add(H.EdgeAt(doc, new[] { 4.0, 3, 0 }, new[] { 4.0, 3, 2 }));
            doc.ComponentDefinition.Features.ChamferFeatures.AddUsingDistance(ec, 0.5);
            return 24 - 0.25;
        });
        // Box z in [-1,1]; drilled d=1 through all from the XY plane (both ways).
        Feature("hole simple", doc =>
        {
            H.Box(doc, 0, 0, 4, 3, 2, PartFeatureExtentDirectionEnum.kSymmetricExtentDirection);
            ThroughHole(doc, doc.ComponentDefinition.WorkPlanes[3], 2, 1.5, 1.0);
            return 24 - PI * 0.25 * 2;
        });
        // M10x1.5 tapped through all (box z in [-2,0]; symmetric extent is
        // E_INVALIDARG for tapped holes): Inventor models the bore
        // at the minor diameter D1 = D - 1.0825 P = 8.376 mm.
        Feature("hole threaded M10x1.5", doc =>
        {
            H.Box(doc, 0, 0, 4, 3, 2, PartFeatureExtentDirectionEnum.kNegativeExtentDirection);
            var hf = doc.ComponentDefinition.Features.HoleFeatures;
            var tap = hf.CreateTapInfo(true, "ISO Metric profile", "M10x1.5", "6H", true);
            var h = ThroughHole(doc, doc.ComponentDefinition.WorkPlanes[3], 2, 1.5, tap,
                PartFeatureExtentDirectionEnum.kPositiveExtentDirection);
            H.Check(h.Tapped, "tapped");
            var ti = (HoleTapInfo)h.TapInfo;
            return H.Vol(doc, 24 - PI * 0.4188 * 0.4188 * 2, 1e-5) + ", " + ti.ThreadDesignation + " " + ti.Class;
        });
        // Hole d=0.5 at (0.5,0.5), pattern 3 x 2 at spacing 1.2 (X) and 1.5 (Y).
        Feature("rectangular pattern", doc =>
        {
            H.Box(doc, 0, 0, 4, 3, 2, PartFeatureExtentDirectionEnum.kSymmetricExtentDirection);
            var def = doc.ComponentDefinition;
            var parents = app.TransientObjects.CreateObjectCollection();
            parents.Add(ThroughHole(doc, def.WorkPlanes[3], 0.5, 0.5, 0.5));
            var p = def.Features.RectangularPatternFeatures.Add(parents, def.WorkAxes[1], true, 3, 1.2,
                PatternSpacingTypeEnum.kDefault, Type.Missing, def.WorkAxes[2], true, 2, 1.5);
            H.Check(p.PatternElements.Count == 6, "elements " + p.PatternElements.Count);
            return 24 - 6 * PI * 0.0625 * 2;
        });
        // Plate [-2,2]^2 x 1, hole d=0.5 at (1.2,0), 6 around the Z axis.
        Feature("circular pattern", doc =>
        {
            H.Box(doc, -2, -2, 2, 2, 1, PartFeatureExtentDirectionEnum.kSymmetricExtentDirection);
            var def = doc.ComponentDefinition;
            var parents = app.TransientObjects.CreateObjectCollection();
            parents.Add(ThroughHole(doc, def.WorkPlanes[3], 1.2, 0, 0.5));
            var p = def.Features.CircularPatternFeatures.Add(parents, def.WorkAxes[3], true, 6, "360 deg", true);
            H.Check(p.PatternElements.Count == 6, "elements " + p.PatternElements.Count);
            return 16 - 6 * PI * 0.0625;
        });
        // Box 4x3x2 shelled 0.2 inwards with the top face removed.
        Feature("shell", doc =>
        {
            H.Box(doc, 0, 0, 4, 3, 2);
            var fc = app.TransientObjects.CreateFaceCollection();
            fc.Add(H.FaceAt(doc, 0, 0, 1, new[] { 0.0, 0, 2 }));
            var sf = doc.ComponentDefinition.Features.ShellFeatures;
            sf.Add(sf.CreateShellDefinition(fc, 0.2, ShellDirectionEnum.kInsideShellDirection));
            return 24 - 3.6 * 2.6 * 1.8;
        });
        // Rectangle x in [1,2], y in [0,3] revolved fully about Y: pi (2^2-1^2) 3.
        Feature("revolve full", doc =>
        {
            var def = doc.ComponentDefinition;
            var sk = def.Sketches.Add(def.WorkPlanes[3]);
            sk.SketchLines.AddAsTwoPointRectangle(tg.CreatePoint2d(1, 0), tg.CreatePoint2d(2, 3));
            def.Features.RevolveFeatures.AddFull(sk.Profiles.AddForSolid(), def.WorkAxes[2],
                PartFeatureOperationEnum.kNewBodyOperation);
            return 9 * PI;
        });
        // Same profile, 90 degrees: a quarter.
        Feature("revolve 90deg", doc =>
        {
            var def = doc.ComponentDefinition;
            var sk = def.Sketches.Add(def.WorkPlanes[3]);
            sk.SketchLines.AddAsTwoPointRectangle(tg.CreatePoint2d(1, 0), tg.CreatePoint2d(2, 3));
            def.Features.RevolveFeatures.AddByAngle(sk.Profiles.AddForSolid(), def.WorkAxes[2], PI / 2,
                PartFeatureExtentDirectionEnum.kPositiveExtentDirection, PartFeatureOperationEnum.kNewBodyOperation);
            return 9 * PI / 4;
        });
        // Circle r=0.5 on XZ swept along line (0,0)-(0,5) + quarter arc R=2:
        // Pappus: pi r^2 * (5 + pi).
        Feature("sweep", doc =>
        {
            var def = doc.ComponentDefinition;
            var ps = def.Sketches.Add(def.WorkPlanes[3]);
            var l = ps.SketchLines.AddByTwoPoints(tg.CreatePoint2d(0, 0), tg.CreatePoint2d(0, 5));
            ps.SketchArcs.AddByCenterStartEndPoint(tg.CreatePoint2d(2, 5), l.EndSketchPoint, tg.CreatePoint2d(2, 7), false);
            var cs = def.Sketches.Add(def.WorkPlanes[2]);  // XZ, normal Y = path start tangent
            cs.SketchCircles.AddByCenterRadius(tg.CreatePoint2d(0, 0), 0.5);
            var sw = def.Features.SweepFeatures;
            sw.AddUsingPath(cs.Profiles.AddForSolid(), sw.CreatePath(l), PartFeatureOperationEnum.kNewBodyOperation);
            return PI * 0.25 * (5 + PI);
        });
        // Square 2x2 at z=0 lofted to centred 1x1 at z=3: frustum 3/3 (4+1+2).
        Feature("loft", doc =>
        {
            var def = doc.ComponentDefinition;
            var s1 = def.Sketches.Add(def.WorkPlanes[3]);
            s1.SketchLines.AddAsTwoPointRectangle(tg.CreatePoint2d(-1, -1), tg.CreatePoint2d(1, 1));
            var wp = def.WorkPlanes.AddByPlaneAndOffset(def.WorkPlanes[3], 3);
            var s2 = def.Sketches.Add(wp);
            s2.SketchLines.AddAsTwoPointRectangle(tg.CreatePoint2d(-0.5, -0.5), tg.CreatePoint2d(0.5, 0.5));
            var secs = app.TransientObjects.CreateObjectCollection();
            secs.Add(s1.Profiles.AddForSolid()); secs.Add(s2.Profiles.AddForSolid());
            var lf = def.Features.LoftFeatures;
            lf.Add(lf.CreateLoftDefinition(secs, PartFeatureOperationEnum.kNewBodyOperation));
            return 7.0;
        }, 1e-5);
        // Work plane offset 1.5 from XY carries a 2x2x1 box: centroid z = 2;
        // work axis from XZ x YZ is the Z axis; work point at 3 planes' corner.
        Feature("work plane/axis/point", doc =>
        {
            var def = doc.ComponentDefinition;
            var wp = def.WorkPlanes.AddByPlaneAndOffset(def.WorkPlanes[3], 1.5);
            H.Box(doc, 0, 0, 2, 2, 1, PartFeatureExtentDirectionEnum.kPositiveExtentDirection, wp);
            var ax = def.WorkAxes.AddByTwoPlanes(def.WorkPlanes[2], def.WorkPlanes[1]);
            var d = ax.Line.Direction;
            H.Check(Math.Abs(Math.Abs(d.Z) - 1) < 1e-12, "axis dir " + d.X + "," + d.Y + "," + d.Z);
            var wpt = def.WorkPoints.AddByThreePlanes(def.WorkPlanes[1], def.WorkPlanes[2], wp);
            var mp = H.Mass(def);
            return string.Join(", ", new[] { H.Vol(doc, 4), H.Near("cz", mp.CenterOfMass.Z, 2),
                H.Near("workpoint.z", wpt.Point.Z, 1.5), "planes " + def.WorkPlanes.Count });
        });
    }
}
