// Autodesk's official "Inventor 2022 Sample Files" (inst/samples/, see CODE.md),
// copied to C:\t\samples\2022 on both sides (samples2016, a symlink to this
// file: the 2016 set in ...\2016), restored from a pristine ...\<set>.orig at
// the start of each run. Activates samples.ipj, then per top-level
// document: open (counts, missing refs, NeedsMigrating), mass properties
// (part/asm), BOM rows (asm), rebuild all (drawings: Update2), save-as copy
// into the run dir (migrates to 2027), close, reopen the copy and compare.
// No analytic expectations: details are compared with the VM run
// (inst/invscen/ref/samples.txt). Restores the previous active project.
// Wine only: INVSCEN_ONLY=substring limits the document list.
using System;
using System.Collections.Generic;
using System.Linq;
using Inventor;

static class Scenario
{
    static readonly string Root = @"C:\t\samples\" + (H.Out.EndsWith("samples2016") ? "2016" : "2022");
    static readonly string[] Docs = {
        // parts
        @"Parts\Flower\Flower.ipt", @"Parts\Hairdryer\Hairdryer.ipt", @"Parts\OilPan\Oil Pan.ipt",
        @"Parts\Plate\Vertical Plate.ipt", @"Parts\Rim\Rim.ipt", @"Parts\Speedometer\Speedometer.ipt",
        @"Parts\Curves Samples\Archimedes Spiral.ipt", @"Parts\Curves Samples\Catenary.ipt",
        @"Parts\Curves Samples\Involute.ipt", @"Parts\Curves Samples\Trochoid&Cycloid.ipt",
        @"Sheet Metal\Mounting Bracket\Mounting Bracket.ipt", @"Translation\Arm Rest\Arm_Rest.ipt",
        @"3D Annotation\CHUCK JAW BASE RH\CHUCK JAW BASE RH.ipt",
        @"3D Annotation\Progressive Profile Example - 3DA\Progressive Profile Example - 3DA.ipt",
        // top-level assemblies
        @"Assemblies\Scissors\scissors.iam", @"Assemblies\Metal Container\Metal Container.iam",
        @"Assemblies\Tuner\Tuner.iam", @"Assemblies\Stapler\Stapler.iam",
        @"Assemblies\Suspension\Suspension.iam", @"Assemblies\Suspension Fork\Suspension-Fork_Complete.iam",
        @"Assemblies\Blower\Blower.iam", @"Assemblies\Engine MKII\Engine MKII.iam",
        @"Assemblies\Test Station\Test Station.iam", @"Assemblies\Personal Computer\Personal Computer.iam",
        @"AEC Exchange\Water Heater\Water Heater.iam", @"Weldments\Carriage\Carriage.iam",
        @"Weldments\Cosmetic\weld - cosmetic.iam", @"Weldments\Cosmetic\weld1 - solid fillet.iam",
        @"Fabrication\Shelf_Target\Shelf_Target.iam", @"Tube & Pipe\Buffer Prep Skid\Buffer Prep Skid.iam",
        @"Mold Design\Fan Cover\Fan Cover\Fan Cover Mold.iam", @"Mold Design\Switch\Switch Mold\Switch Mold.iam",
        // presentations + drawings (.idw, and the older-format Inventor .dwg of a few)
        @"Assemblies\Engine MKII\Engine MKII (Exploded View).ipn",
        @"Assemblies\Engine MKII\Engine MKII (Assembly Instructions).ipn",
        @"Parts\Flower\Flower.idw", @"Parts\Hairdryer\Hairdryer.idw", @"Parts\OilPan\Oil Pan.idw",
        @"Parts\Plate\Vertical Plate.idw", @"Parts\Rim\Rim.idw", @"Parts\Speedometer\Speedometer.idw",
        @"Sheet Metal\Mounting Bracket\Mounting Bracket.idw", @"Translation\Arm Rest\Arm_Rest.idw",
        @"Assemblies\Scissors\scissors.idw", @"Assemblies\Metal Container\Metal Container.idw",
        @"Assemblies\Tuner\Tuner.idw", @"Assemblies\Stapler\Stapler.idw", @"Assemblies\Suspension\Suspension.idw",
        @"Assemblies\Suspension Fork\Suspension-Fork_Complete.idw", @"Assemblies\Engine MKII\Engine MKII.idw",
        @"Assemblies\Engine MKII\ANSI (in)test.idw", @"Assemblies\Engine MKII\Components\Engine Case.idw",
        @"Assemblies\Test Station\Test Station Master Cut List.idw",
        @"Assemblies\Personal Computer\Inventor PC Case.idw", @"Assemblies\Personal Computer\CDROM Ribbon Cable.idw",
        @"Assemblies\Personal Computer\Power Supply Harness Nailboard.idw",
        @"Assemblies\Personal Computer\PC Chassis\Back Bracket.idw",
        @"Assemblies\Personal Computer\PC Chassis\Hard Drive Bracket.idw",
        @"AEC Exchange\Water Heater\Water Heater.idw", @"Weldments\Carriage\Carriage.idw",
        @"Weldments\Cosmetic\weld - cosmetic.idw", @"Weldments\Cosmetic\weld1 - solid fillet.idw",
        @"Fabrication\Shelf_Target\Shelf_Target.idw", @"Tube & Pipe\Buffer Prep Skid\40-P-P011500-018.idw",
        @"Mold Design\Fan Cover\Fan Cover Mold.idw", @"Mold Design\Switch\Switch Mold.idw",
        @"Assemblies\Engine MKII\Engine MKII.dwg", @"Assemblies\Scissors\scissors.dwg",
        @"Parts\Rim\Rim.dwg", @"Weldments\Carriage\Carriage.dwg",
    };

    // Missing references anywhere below f (visited by path).
    static int Missing(File f, HashSet<string> seen)
    {
        if (!seen.Add(f.FullFileName)) return 0;
        int n = 0;
        foreach (FileDescriptor fd in f.ReferencedFileDescriptors)
            if (fd.ReferenceMissing) n++;
            else if (fd.ReferencedFile != null) n += Missing(fd.ReferencedFile, seen);  // null: non-Inventor file
        return n;
    }

    // Structural summary; equal before save and after reopen.
    static string Counts(Document d)
    {
        string s;
        if (d is PartDocument)
        {
            var def = ((PartDocument)d).ComponentDefinition;
            s = "features " + def.Features.Count + ", bodies " + def.SurfaceBodies.Count + ", sketches " + def.Sketches.Count;
        }
        else if (d is AssemblyDocument)
        {
            var def = ((AssemblyDocument)d).ComponentDefinition;
            s = "occ " + def.Occurrences.Count + ", leaf " + def.Occurrences.AllLeafOccurrences.Count
                + ", constraints " + def.Constraints.Count;
        }
        else if (d is DrawingDocument)
        {
            int views = 0, pl = 0;
            foreach (Sheet sh in ((DrawingDocument)d).Sheets) { views += sh.DrawingViews.Count; pl += sh.PartsLists.Count; }
            s = "sheets " + ((DrawingDocument)d).Sheets.Count + ", views " + views + ", partslists " + pl;
        }
        else s = "type " + d.DocumentType;
        return s + ", refdocs " + d.AllReferencedDocuments.Count + ", missing " + Missing(d.File, new HashSet<string>());
    }

    // Volume must agree within 1e-4 relative (a rebuild of a migrated file moves it slightly).
    static void SameVol(string m, string was, string what)
    {
        if (was == null || m == was) return;  // no earlier value (mass step failed)
        Func<string, double> v = t => double.Parse(t.Split(' ')[1], System.Globalization.CultureInfo.InvariantCulture);
        double a = v(m), b = v(was);
        H.Check(Math.Abs(a - b) <= 1e-4 * Math.Abs(b), what + " " + m + " (was " + was + ")");
    }

    static string MassText(Document d)
    {
        object def = d is PartDocument ? (object)((PartDocument)d).ComponentDefinition
            : d is AssemblyDocument ? ((AssemblyDocument)d).ComponentDefinition : null;
        if (def == null) return null;
        var mp = H.Mass(def);
        string s = string.Format("vol {0:G7} cm3, mass {1:G7} kg, area {2:G7} cm2", mp.Volume, mp.Mass, mp.Area);
        if (mp.Volume == 0) return s;  // surface-only part: CenterOfMass is E_FAIL
        var c = mp.CenterOfMass;
        return s + string.Format(", com ({0:G6}, {1:G6}, {2:G6})", c.X, c.Y, c.Z);
    }

    // Saving a rebuilt top document also saves its dirty (migrated) dependents in
    // place, so every run first restores the work tree from the pristine Root.orig.
    static void Mirror(string src, string dst)
    {
        // In place: removing a directory Inventor watches fails on Wine (issue 054).
        System.IO.Directory.CreateDirectory(dst);
        Func<string, string, string> to = (p, d) => d + "\\" + System.IO.Path.GetFileName(p);
        foreach (string f in System.IO.Directory.GetFiles(dst))
            if (!System.IO.File.Exists(to(f, src))) System.IO.File.Delete(f);
        foreach (string d in System.IO.Directory.GetDirectories(dst))
            if (!System.IO.Directory.Exists(to(d, src))) System.IO.Directory.Delete(d, true);
        foreach (string f in System.IO.Directory.GetFiles(src)) System.IO.File.Copy(f, to(f, dst), true);
        foreach (string d in System.IO.Directory.GetDirectories(src)) Mirror(d, to(d, dst));
    }

    // Copies go to C: on both sides: on Wine H.Out is on Z:, and a copy on another
    // drive than the models loses its relative references on reopen.
    static readonly string CopyDir = @"C:\t\scen\" + System.IO.Path.GetFileName(H.Out);

    public static void Run()
    {
        var app = H.App;
        System.IO.Directory.CreateDirectory(CopyDir);
        var dpm = app.DesignProjectManager;
        string prev = dpm.ActiveDesignProject.FullFileName;
        bool restored = false;
        H.Step("restore pristine samples", () =>
        {
            // An aborted run leaves a samples.ipj active, which Inventor keeps open
            // (sharing violation on restore): switch to Default first.
            if (prev.EndsWith(@"\samples.ipj", StringComparison.OrdinalIgnoreCase))
            {
                foreach (DesignProject q in dpm.DesignProjects) if (q.Name == "Default") q.Activate(true);
                prev = dpm.ActiveDesignProject.FullFileName;
            }
            Mirror(Root + ".orig", Root);
            restored = true;
            return System.IO.Directory.GetFiles(Root, "*", System.IO.SearchOption.AllDirectories).Length + " files";
        }, 600);
        if (!restored) return;
        H.Step("activate samples.ipj", () =>
        {
            DesignProject p = null;
            foreach (DesignProject q in dpm.DesignProjects)
                if (string.Equals(q.FullFileName, Root + @"\samples.ipj", StringComparison.OrdinalIgnoreCase)) p = q;
            if (p == null) p = dpm.DesignProjects.AddExisting(Root + @"\samples.ipj");
            p.Activate(true);
            return "was " + prev + ", now " + dpm.ActiveDesignProject.Name;
        });

        string only = System.Environment.GetEnvironmentVariable("INVSCEN_ONLY");
        int idx = 0;
        foreach (string rel in Docs)
        {
            idx++;
            if (!string.IsNullOrEmpty(only) && rel.IndexOf(only, StringComparison.OrdinalIgnoreCase) < 0) continue;
            string path = Root + @"\Models\" + rel, name = System.IO.Path.GetFileName(rel);
            if (!System.IO.File.Exists(path)) { Console.WriteLine("SKIP open " + rel + " (not in this set)"); continue; }
            string copy = CopyDir + "\\" + idx.ToString("D2") + "_" + name;
            string counts = null, mass = null;
            Document d = null;
            H.Reset();
            H.Step("open " + rel, () =>
            {
                d = (Document)app.Documents.Open(path, true);
                counts = Counts(d);
                return counts + ", migrate " + d.NeedsMigrating + ", docs open " + app.Documents.Count;
            }, 900);
            if (d == null) continue;
            if (d is PartDocument || d is AssemblyDocument)
            {
                H.Reset();
                H.Step("mass " + name, () => mass = MassText(d), 900);
            }
            if (d is AssemblyDocument)
            {
                H.Reset();
                H.Step("BOM " + name, () =>
                {
                    var bom = ((AssemblyDocument)d).ComponentDefinition.BOM;
                    bom.StructuredViewEnabled = true;
                    bom.PartsOnlyViewEnabled = true;
                    return "structured rows " + bom.BOMViews["Structured"].BOMRows.Count
                        + ", parts-only rows " + bom.BOMViews["Parts Only"].BOMRows.Count;
                }, 600);
            }
            H.Reset();
            if (d.DocumentType != DocumentTypeEnum.kPresentationDocumentObject)  // .ipn: E_NOTIMPL
            H.Step("rebuild " + name, () =>
            {
                // drawings/presentations: Rebuild2 is E_NOTIMPL
                bool ok = d is PartDocument || d is AssemblyDocument ? d.Rebuild2(true) : d.Update2(true);
                string m = MassText(d), was = mass;
                if (m != null) { SameVol(m, was, "volume after rebuild"); mass = m; }
                return (m != null ? "Rebuild2 " : "Update2 ") + ok + (m == null ? "" : m == was ? ", mass unchanged" : ", " + m);
            }, 900);
            H.Reset();
            // SaveAs copy of an Inventor .dwg goes through the DWG export translator and
            // shows its options dialog despite SilentOperation (VM): save it natively.
            H.Step("save as " + name, () =>
            {
                if (System.IO.File.Exists(copy)) System.IO.File.Delete(copy);
                d.SaveAs(copy, !rel.EndsWith(".dwg"));
                return System.IO.Path.GetFileName(copy) + " " + new System.IO.FileInfo(copy).Length + " bytes";
            }, 900);
            H.Reset();
            H.Step("close " + name, () => { d.Close(true); d = null; return "docs open " + app.Documents.Count; }, 600);
            if (d != null) { try { app.Documents.CloseAll(false); } catch (Exception) { } d = null; }
            if (!System.IO.File.Exists(copy)) continue;
            H.Reset();
            H.Step("reopen " + name, () =>
            {
                d = (Document)app.Documents.Open(copy, true);
                string c = Counts(d), m = MassText(d);
                H.Check(c == counts, "counts after reopen " + c);
                if (m != null) SameVol(m, mass, "volume after reopen");
                return c + ", migrate " + d.NeedsMigrating;
            }, 900);
            H.Reset();
            H.Step("close copy " + name, () => { app.Documents.CloseAll(false); d = null; return "docs open " + app.Documents.Count; }, 600);
        }

        H.Reset();
        H.Step("restore project", () =>
        {
            foreach (DesignProject q in dpm.DesignProjects)
                if (q.FullFileName == prev) q.Activate(true);
            return dpm.ActiveDesignProject.FullFileName;
        });
    }
}
