// Assembly constraints, BOM, interference. Parts built here: box 4x3x2,
// plate 6x6x1 with a d=1 through hole at (3,3), pin d=1 x 3. Box #1 grounded;
// box #2 mate/flush/flush onto #1's top (lands at (0,0,2)); box #3 on #2 with
// a 30 deg angle constraint; pin inserted into the plate hole; box #4 left
// overlapping #1 for interference analysis ((4-1)(3-1)2 = 12 cm3). cm, rad.
using System;
using Inventor;

static class Scenario
{
    static Application app;

    static string MakePart(string name, Action<PartDocument> build)
    {
        var d = H.NewPart();
        build(d);
        H.Save(d, name);
        d.Close(true);
        return H.Out + "\\" + name;
    }

    // Planar face of occurrence occ with world normal +-n containing world point c.
    static Face OFace(ComponentOccurrence occ, double nx, double ny, double nz, double[] c)
    {
        foreach (SurfaceBody b in occ.SurfaceBodies)
            foreach (Face f in b.Faces)
            {
                if (f.SurfaceType != SurfaceTypeEnum.kPlaneSurface) continue;
                var pl = (Plane)f.Geometry; var n = pl.Normal; var r = pl.RootPoint;
                if (Math.Abs(Math.Abs(n.X * nx + n.Y * ny + n.Z * nz) - 1) > 1e-9) continue;  // Plane normals need not point outwards
                if (Math.Abs((c[0] - r.X) * n.X + (c[1] - r.Y) * n.Y + (c[2] - r.Z) * n.Z) < 1e-6) return f;
            }
        throw new Exception("no face on " + occ.Name);
    }

    // Circular edge of occ at height z (world).
    static Edge CircEdge(ComponentOccurrence occ, double z)
    {
        foreach (SurfaceBody b in occ.SurfaceBodies)
            foreach (Edge e in b.Edges)
                if (e.GeometryType == CurveTypeEnum.kCircleCurve && Math.Abs(((Circle)e.Geometry).Center.Z - z) < 1e-6) return e;
        throw new Exception("no circular edge at z=" + z + " on " + occ.Name);
    }

    static string Pos(ComponentOccurrence o)
    {
        var t = o.Transformation.Translation;
        return string.Format("({0:G6},{1:G6},{2:G6})", t.X, t.Y, t.Z);
    }

    public static void Run()
    {
        app = H.App;
        var tg = app.TransientGeometry;
        string box = null, plate = null, pin = null;
        AssemblyDocument doc = null;
        AssemblyComponentDefinition def = null;
        ComponentOccurrence b1 = null, b2 = null, b3 = null, pl = null, pn = null, b4 = null;

        H.Step("make parts", () =>
        {
            box = MakePart("box.ipt", d => H.Box(d, 0, 0, 4, 3, 2));
            plate = MakePart("plate.ipt", d =>
            {
                H.Box(d, 0, 0, 6, 6, 1);
                var sk = d.ComponentDefinition.Sketches.Add(d.ComponentDefinition.WorkPlanes[3]);
                sk.SketchCircles.AddByCenterRadius(tg.CreatePoint2d(3, 3), 0.5);
                d.ComponentDefinition.Features.ExtrudeFeatures.AddByThroughAllExtent(sk.Profiles.AddForSolid(),
                    PartFeatureExtentDirectionEnum.kPositiveExtentDirection, PartFeatureOperationEnum.kCutOperation);
                H.Vol(d, 36 - Math.PI * 0.25, 1e-5);
            });
            pin = MakePart("pin.ipt", d =>
            {
                var sk = d.ComponentDefinition.Sketches.Add(d.ComponentDefinition.WorkPlanes[3]);
                sk.SketchCircles.AddByCenterRadius(tg.CreatePoint2d(0, 0), 0.5);
                d.ComponentDefinition.Features.ExtrudeFeatures.AddByDistanceExtent(sk.Profiles.AddForSolid(), 3,
                    PartFeatureExtentDirectionEnum.kPositiveExtentDirection, PartFeatureOperationEnum.kNewBodyOperation);
            });
            return "box, plate, pin";
        });
        H.Step("new assembly + place", () =>
        {
            doc = (AssemblyDocument)app.Documents.Add(DocumentTypeEnum.kAssemblyDocumentObject,
                app.FileManager.GetTemplateFile(DocumentTypeEnum.kAssemblyDocumentObject), true);
            def = doc.ComponentDefinition;
            var m = tg.CreateMatrix();
            b1 = def.Occurrences.Add(box, m); b1.Grounded = true;
            m.SetTranslation(tg.CreateVector(7, 5, 3)); b2 = def.Occurrences.Add(box, m);
            m.SetTranslation(tg.CreateVector(-8, 6, 9)); b3 = def.Occurrences.Add(box, m);
            m.SetTranslation(tg.CreateVector(20, 0, 0)); pl = def.Occurrences.Add(plate, m); pl.Grounded = true;
            m.SetTranslation(tg.CreateVector(30, 4, 7)); pn = def.Occurrences.Add(pin, m);
            m.SetTranslation(tg.CreateVector(1, 1, 0)); b4 = def.Occurrences.Add(box, m); b4.Grounded = true;
            return "occurrences " + def.Occurrences.Count;
        });
        H.Step("mate + 2 flush", () =>
        {
            var c = def.Constraints;  // faces looked up first: each constraint moves b2
            var f = new[] { OFace(b1, 0, 0, 1, new[] { 0.0, 0, 2 }), OFace(b2, 0, 0, -1, new[] { 0.0, 0, 3 }),
                OFace(b1, -1, 0, 0, new[] { 0.0, 0, 0 }), OFace(b2, -1, 0, 0, new[] { 7.0, 0, 0 }),
                OFace(b1, 0, -1, 0, new[] { 0.0, 0, 0 }), OFace(b2, 0, -1, 0, new[] { 0.0, 5, 0 }) };
            c.AddMateConstraint(f[0], f[1], 0);
            c.AddFlushConstraint(f[2], f[3], 0);
            c.AddFlushConstraint(f[4], f[5], 0);
            doc.Update();
            var t = b2.Transformation.Translation;
            H.Check(Math.Abs(t.X) < 1e-9 && Math.Abs(t.Y) < 1e-9 && Math.Abs(t.Z - 2) < 1e-9, "b2 at " + Pos(b2));
            return "b2 at " + Pos(b2) + ", constraints " + c.Count;
        });
        H.Step("angle 30 deg", () =>
        {
            var c = def.Constraints;
            var f = new[] { OFace(b2, 0, 0, 1, new[] { 0.0, 0, 4 }), OFace(b3, 0, 0, -1, new[] { 0.0, 0, 9 }),
                OFace(b2, 1, 0, 0, new[] { 4.0, 0, 0 }), OFace(b3, 1, 0, 0, new[] { -4.0, 0, 0 }) };
            c.AddMateConstraint(f[0], f[1], 0);
            var a = c.AddAngleConstraint(f[2], f[3], Math.PI / 6, AngleConstraintSolutionTypeEnum.kDirectedSolution, f[0]);
            doc.Update();
            var m = b3.Transformation;
            double ang = Math.Atan2(m.get_Cell(2, 1), m.get_Cell(1, 1));  // rotation about z
            H.Check(Math.Abs(m.get_Cell(3, 3) - 1) < 1e-9, "b3 still upright");
            return H.Near("|angle| deg", Math.Abs(ang) * 180 / Math.PI, 30, 1e-9) + ", " + H.Near("b3.z", m.Translation.Z, 4, 1e-9)
                + ", health " + a.HealthStatus;
        });
        H.Step("insert pin", () =>
        {
            // plate hole top edge at world z=1, pin bottom edge at z=7 (placed at 30,4,7)
            var e1 = CircEdge(pl, 1); var e2 = CircEdge(pn, 7);
            var ins = def.Constraints.AddInsertConstraint(e1, e2, true, 0);
            doc.Update();
            var t = pn.Transformation.Translation;
            var ax = pn.Transformation.get_Cell(3, 3);
            return H.Near("pin.x", t.X, 23) + ", " + H.Near("pin.y", t.Y, 3) + ", pin z-axis " + ax + ", pin at " + Pos(pn)
                + ", health " + ins.HealthStatus;
        });
        H.Step("mass properties", () =>
        {
            double v = 4 * 24 + 36 - Math.PI * 0.25 + Math.PI * 0.25 * 3;
            return H.Near("volume", H.Mass(def).Volume, v, 1e-5);
        });
        H.Step("interference", () =>
        {
            var s = app.TransientObjects.CreateObjectCollection();
            foreach (ComponentOccurrence o in def.Occurrences) s.Add(o);
            var r = def.AnalyzeInterference(s);
            H.Check(r.Count >= 1, "results " + r.Count);
            double v = 0; string pairs = "";
            for (int i = 1; i <= r.Count; i++)
            {
                v += r[i].Volume;
                pairs += r[i].OccurrenceOne.Name + "/" + r[i].OccurrenceTwo.Name + "=" + r[i].Volume.ToString("G6") + " ";
            }
            // b4 overlaps b1 only; the pin sits in the plate hole with zero clearance
            return pairs + H.Near("total", v, 12, 1e-6);
        });
        H.Step("BOM", () =>
        {
            var bom = doc.ComponentDefinition.BOM;
            bom.StructuredViewEnabled = true;
            bom.PartsOnlyViewEnabled = true;
            string r = "";
            foreach (BOMView v in bom.BOMViews)
            {
                if (v.ViewType == BOMViewTypeEnum.kModelDataBOMViewType) continue;
                var q = new System.Collections.Generic.SortedDictionary<string, int>();
                foreach (BOMRow row in v.BOMRows)
                    q[System.IO.Path.GetFileName(((Document)row.ComponentDefinitions[1].Document).FullFileName)] = row.ItemQuantity;
                string s = string.Join(" ", System.Linq.Enumerable.Select(q, kv => kv.Key + "x" + kv.Value));
                H.Check(s == "box.iptx4 pin.iptx1 plate.iptx1", v.Name + ": " + s);
                r += v.Name + ": " + s + "; ";
            }
            string csv = H.Out + "\\bom.csv";
            if (System.IO.File.Exists(csv)) System.IO.File.Delete(csv);
            bom.BOMViews["Parts Only"].Export(csv, FileFormatEnum.kTextFileCommaDelimitedFormat);
            return r + "export " + new System.IO.FileInfo(csv).Length + " bytes";
        });
        H.Step("save + reopen", () =>
        {
            string s = H.Save(doc, "constraints.iam");
            doc.Close(true);
            doc = (AssemblyDocument)app.Documents.Open(H.Out + "\\constraints.iam", true);
            def = doc.ComponentDefinition;
            H.Check(def.Occurrences.Count == 6 && def.Constraints.Count == 6,
                "occ " + def.Occurrences.Count + " constraints " + def.Constraints.Count);
            var t = def.Occurrences[2].Transformation.Translation;
            H.Check(Math.Abs(t.Z - 2) < 1e-9, "b2.z " + t.Z);
            return s + ", occ 6, constraints 6";
        });
        H.Step("close", () => { doc.Close(true); return "docs open: " + app.Documents.Count; });
    }
}
