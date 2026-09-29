// Inventor COM scenario harness: compiled together with one scenario file
// (class Scenario { public static void Run() }) by tools/invscen/run.sh.
using System;
using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Linq;
using System.Threading;
using Inventor;

static class H
{
    public static Application App;
    public static string Out;   // Windows path of this run's artifact dir
    static int fails, failed;  // failures since the last Reset / in total
    // Starts an independent section: steps run again after an earlier failure.
    public static void Reset() { fails = 0; }

    // Runs body on a worker thread (MTA, like Main); prints PASS/FAIL with
    // body's returned detail string and timing. A timeout aborts the run
    // (a hung COM call can't be cancelled). A failed step skips the rest.
    public static void Step(string name, Func<string> body, int timeoutSec = 120)
    {
        if (fails > 0) { Console.WriteLine("SKIP " + name); return; }
        string detail = null; Exception err = null;
        var sw = Stopwatch.StartNew();
        var t = new Thread(() => { try { detail = body(); } catch (Exception e) { err = e; } });
        t.IsBackground = true;
        t.Start();
        // Poll for Inventor's modal "Licensing error" (the licensing service went away,
        // Inventor quits on OK): the step can't finish, abort with its text.
        while (!t.Join(Math.Min(5000, Math.Max(0, timeoutSec * 1000 - (int)sw.ElapsedMilliseconds))))
        {
            Welcome();
            string dlg = Dialogs();
            bool lic = dlg.Contains("'Licensing error'");
            if (!lic && sw.ElapsedMilliseconds < timeoutSec * 1000L) continue;
            Console.WriteLine("FAIL {0} ({1:F1}s): {2}; Inventor dialogs: {3}", name, sw.Elapsed.TotalSeconds,
                lic ? "LICENSING ERROR" : "TIMEOUT after " + timeoutSec + "s", dlg == "" ? "none" : dlg);
            System.Environment.Exit(2);
        }
        if (err != null)
        {
            fails++; failed++;
            Console.WriteLine("FAIL {0} ({1:F1}s): HRESULT 0x{2:X8} {3}", name, sw.Elapsed.TotalSeconds, err.HResult, err);
            // RPC_S_SERVER_UNAVAILABLE / RPC_E_DISCONNECTED: Inventor is gone, nothing else can pass
            if ((uint)err.HResult == 0x800706BA || (uint)err.HResult == 0x80010108)
            {
                Console.WriteLine("ABORT: Inventor exited (crash, or quit after a licensing error)");
                System.Environment.Exit(2);
            }
        }
        else
            Console.WriteLine("PASS {0} ({1:F1}s){2}", name, sw.Elapsed.TotalSeconds,
                string.IsNullOrEmpty(detail) ? "" : ": " + detail);
    }

    [DllImport("user32.dll")] static extern bool EnumWindows(EnumProc f, IntPtr p);
    [DllImport("user32.dll")] static extern bool EnumChildWindows(IntPtr w, EnumProc f, IntPtr p);
    [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr w, out uint pid);
    [DllImport("user32.dll")] static extern bool IsWindowVisible(IntPtr w);
    [DllImport("user32.dll")] static extern bool GetWindowRect(IntPtr w, out RECT r);
    [DllImport("user32.dll")] static extern bool PostMessage(IntPtr w, uint m, IntPtr a, IntPtr b);
    struct RECT { public int L, T, R, B; }
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern int GetClassName(IntPtr w, System.Text.StringBuilder s, int n);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern int GetWindowText(IntPtr w, System.Text.StringBuilder s, int n);
    delegate bool EnumProc(IntPtr w, IntPtr p);
    static string Text(IntPtr w, bool cls = false)
    {
        var b = new System.Text.StringBuilder(1024);
        if (cls) GetClassName(w, b, b.Capacity); else GetWindowText(w, b, b.Capacity);
        return b.ToString();
    }

    // AdskLicensingAgent's "Welcome to your trial" popup at Inventor start (WebView2 in an
    // untitled 860x500 'webview' WS_POPUP; not shown while a killed Inventor's agent lingers):
    // WM_CLOSE it. Clicking its X (cursor + mouse_event) instead made the next
    // Documents.Add's ActiveView null on Wine; WM_CLOSE doesn't, and Inventor keeps running.
    static bool welcomed;
    public static void Welcome()
    {
        lock (typeof(H))
        {
            if (welcomed) return;
            var pids = new System.Collections.Generic.HashSet<uint>();
            foreach (var p in Process.GetProcessesByName("AdskLicensingAgent")) pids.Add((uint)p.Id);
            EnumWindows((w, _) =>
            {
                uint pid; GetWindowThreadProcessId(w, out pid); RECT r;
                if (!pids.Contains(pid) || !IsWindowVisible(w) || Text(w, true) != "webview"
                    || !GetWindowRect(w, out r) || r.R - r.L != 860 || r.B - r.T != 500) return true;
                PostMessage(w, 0x10, IntPtr.Zero, IntPtr.Zero);  // WM_CLOSE
                for (int i = 0; i < 50 && IsWindowVisible(w); i++) Thread.Sleep(100);
                if (IsWindowVisible(w)) return true;  // retried on the next poll
                Console.WriteLine("dismissed trial welcome");
                welcomed = true;
                return false;
            }, IntPtr.Zero);
        }
    }

    // Visible dialog boxes (#32770) of Inventor.exe: 'title' [child texts | ...]; ...
    public static string Dialogs()
    {
        var pids = new System.Collections.Generic.HashSet<uint>();
        foreach (var p in Process.GetProcessesByName("Inventor")) pids.Add((uint)p.Id);
        var r = new System.Collections.Generic.List<string>();
        EnumWindows((w, _) =>
        {
            uint pid; GetWindowThreadProcessId(w, out pid);
            if (!pids.Contains(pid) || !IsWindowVisible(w) || Text(w, true) != "#32770") return true;
            var kids = new System.Collections.Generic.List<string>();
            EnumChildWindows(w, (c, __) => { string x = Text(c); if (x != "") kids.Add(x); return true; }, IntPtr.Zero);
            r.Add("'" + Text(w) + "' [" + string.Join(" | ", kids.Take(8)) + "]");
            return true;
        }, IntPtr.Zero);
        return string.Join("; ", r);
    }

    // Throws unless |actual - expected| <= tol * |expected|.
    public static string Near(string what, double actual, double expected, double tol = 1e-6)
    {
        string s = string.Format("{0}={1:R} (expected {2:R})", what, actual, expected);
        if (Math.Abs(actual - expected) > tol * Math.Abs(expected)) throw new Exception("mismatch: " + s);
        return s;
    }

    public static void Check(bool ok, string what) { if (!ok) throw new Exception("check failed: " + what); }

    // --- Modelling helpers shared by scenarios (lengths in cm, angles in rad) ---

    public static PartDocument NewPart(string tpl = null)
    {
        return (PartDocument)App.Documents.Add(DocumentTypeEnum.kPartDocumentObject,
            tpl ?? App.FileManager.GetTemplateFile(DocumentTypeEnum.kPartDocumentObject), true);
    }

    // Rectangle [x0,x1]x[y0,y1] on plane (default XY) extruded by h.
    public static ExtrudeFeature Box(PartDocument doc, double x0, double y0, double x1, double y1, double h,
        PartFeatureExtentDirectionEnum dir = PartFeatureExtentDirectionEnum.kPositiveExtentDirection,
        object plane = null,
        PartFeatureOperationEnum op = PartFeatureOperationEnum.kNewBodyOperation)
    {
        var def = doc.ComponentDefinition;
        var tg = App.TransientGeometry;
        var sk = def.Sketches.Add(plane ?? def.WorkPlanes[3]);
        sk.SketchLines.AddAsTwoPointRectangle(tg.CreatePoint2d(x0, y0), tg.CreatePoint2d(x1, y1));
        var ext = def.Features.ExtrudeFeatures.AddByDistanceExtent(sk.Profiles.AddForSolid(), h, dir, op);
        Check(ext.HealthStatus == HealthStatusEnum.kUpToDateHealth, "extrude health " + ext.HealthStatus);
        return ext;
    }

    public static MassProperties Mass(object def)
    {
        var mp = def is PartComponentDefinition ? ((PartComponentDefinition)def).MassProperties
            : ((AssemblyComponentDefinition)def).MassProperties;
        mp.Accuracy = MassPropertiesAccuracyEnum.k_VeryHigh;
        return mp;
    }

    // Checks the part volume (tolerance relative) and returns the detail string.
    public static string Vol(PartDocument doc, double expected, double tol = 1e-6)
    {
        doc.Update();
        return Near("volume", Mass(doc.ComponentDefinition).Volume, expected, tol);
    }

    // Linear edge of the part with endpoints a, b (either order).
    public static Edge EdgeAt(PartDocument doc, double[] a, double[] b)
    {
        foreach (SurfaceBody body in doc.ComponentDefinition.SurfaceBodies)
            foreach (Edge e in body.Edges)
            {
                if (e.StartVertex == null || e.StopVertex == null) continue;
                Point p = e.StartVertex.Point, q = e.StopVertex.Point;
                if ((At(p, a) && At(q, b)) || (At(p, b) && At(q, a))) return e;
            }
        throw new Exception("no edge " + string.Join(",", a) + " - " + string.Join(",", b));
    }
    static bool At(Point p, double[] c)
    {
        return Math.Abs(p.X - c[0]) < 1e-6 && Math.Abs(p.Y - c[1]) < 1e-6 && Math.Abs(p.Z - c[2]) < 1e-6;
    }

    // Planar face of the part whose normal is (nx,ny,nz) and which contains point c.
    public static Face FaceAt(PartDocument doc, double nx, double ny, double nz, double[] c)
    {
        foreach (SurfaceBody body in doc.ComponentDefinition.SurfaceBodies)
            foreach (Face f in body.Faces)
            {
                if (f.SurfaceType != SurfaceTypeEnum.kPlaneSurface) continue;
                var pl = (Plane)f.Geometry;
                var n = pl.Normal; var r = pl.RootPoint;
                if (Math.Abs(Math.Abs(n.X * nx + n.Y * ny + n.Z * nz) - 1) > 1e-9) continue;
                if (Math.Abs((c[0] - r.X) * n.X + (c[1] - r.Y) * n.Y + (c[2] - r.Z) * n.Z) < 1e-6) return f;
            }
        throw new Exception("no face normal " + nx + "," + ny + "," + nz + " through " + string.Join(",", c));
    }

    // Exports doc through translator add-in clsid (e.g. STEP) to Out\name with
    // the translator's default options overridden by opts (name, value pairs).
    public static string Translate(object doc, string clsid, string name, params object[] opts)
    {
        var t = (TranslatorAddIn)App.ApplicationAddIns.get_ItemById(clsid);
        if (!t.Activated) t.Activate();
        var to = App.TransientObjects;
        var ctx = to.CreateTranslationContext();
        ctx.Type = IOMechanismEnum.kFileBrowseIOMechanism;
        var o = to.CreateNameValueMap();
        t.get_HasSaveCopyAsOptions(doc, ctx, o);
        for (int i = 0; i < opts.Length; i += 2)
        {
            try { o.Remove(opts[i]); } catch (COMException) { }
            o.Add((string)opts[i], opts[i + 1]);
        }
        var dm = to.CreateDataMedium();
        dm.FileName = Out + "\\" + name;
        string stem = System.IO.Path.GetFileNameWithoutExtension(name), ext = System.IO.Path.GetExtension(name);
        foreach (var f in System.IO.Directory.GetFiles(Out, stem + "*" + ext)) System.IO.File.Delete(f);
        t.SaveCopyAs(doc, ctx, o, dm);
        // multi-sheet DWG/DXF go to stem_Sheet_N.ext
        var files = System.IO.Directory.GetFiles(Out, stem + "*" + ext);
        Check(files.Length > 0, name + " written");
        return string.Join(", ", System.Linq.Enumerable.Select(files,
            f => System.IO.Path.GetFileName(f) + " " + new System.IO.FileInfo(f).Length + " bytes"));
    }

    // SaveAs into the run dir (replacing), returns "name size".
    public static string Save(object doc, string name, bool copy = false)
    {
        string p = Out + "\\" + name;
        if (System.IO.File.Exists(p)) System.IO.File.Delete(p);
        ((Document)doc).SaveAs(p, copy);
        return name + " " + new System.IO.FileInfo(p).Length + " bytes";
    }

    [MTAThread]
    static int Main(string[] args)
    {
        Out = args[0];
        System.IO.Directory.CreateDirectory(Out);
        Step("connect", () =>
        {
            // Attach only (run.sh starts Inventor): never spawn a second instance.
            while (App == null)
                try { App = (Application)Marshal.GetActiveObject("Inventor.Application"); }
                catch (COMException) { Thread.Sleep(2000); }
                // Registered in the ROT before it implements Application during startup.
                catch (InvalidCastException) { Thread.Sleep(2000); }
            App.SilentOperation = true;
            Welcome();
            // Deterministic start: discard whatever is open (dedicated test Inventor),
            // unless INVSCEN_KEEP is set (helpers for UI work on open documents).
            int n = App.Documents.Count;
            if (System.Environment.GetEnvironmentVariable("INVSCEN_KEEP") != null) n = 0;
            else App.Documents.CloseAll(false);
            return App.SoftwareVersion.DisplayName + ", closed " + n + " open docs";
        }, 300);
        Scenario.Run();
        if (App != null) try { App.SilentOperation = false; } catch (Exception) { }
        Console.WriteLine(failed == 0 ? "RESULT PASS" : "RESULT FAIL");
        return failed == 0 ? 0 : 1;
    }
}
