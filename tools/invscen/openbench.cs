// Open-time benchmark (057): activates C:\t\samples\2022\samples.ipj (as samples
// does; run samples once first so the tree exists), then opens and closes each
// document of INVSCEN_OPEN (';'-separated, relative to ...\Models or absolute)
// INVSCEN_N times (default 3). Leaves samples.ipj active.
// INVSCEN_SYNC=DIR (Windows path): around each walk, create DIR\walk.start and wait for
// DIR\walk.go, then create DIR\walk.end and wait for DIR\walk.done (attach a tracer, 081).
using System;
using Inventor;

static class Scenario
{
    static int calls;
    // samples' Missing(): walk every file reference once (per-call COM cost).
    static int Walk(File f, System.Collections.Generic.HashSet<string> seen)
    {
        calls++;
        if (!seen.Add(f.FullFileName)) return 0;
        int n = 0;
        foreach (FileDescriptor fd in f.ReferencedFileDescriptors)
        {
            calls += 3;
            if (fd.ReferenceMissing) n++;
            else if (fd.ReferencedFile != null) n += Walk(fd.ReferencedFile, seen);
        }
        return n;
    }

    static void Sync(string mark, string wait)
    {
        string d = System.Environment.GetEnvironmentVariable("INVSCEN_SYNC");
        if (d == null) return;
        System.IO.File.Delete(d + "\\walk." + wait);
        System.IO.File.WriteAllText(d + "\\walk." + mark, "");
        for (int i = 0; i < 600 && !System.IO.File.Exists(d + "\\walk." + wait); i++) System.Threading.Thread.Sleep(100);
        System.IO.File.Delete(d + "\\walk." + mark);
    }

    public static void Run()
    {
        const string Root = @"C:\t\samples\2022";
        var dpm = H.App.DesignProjectManager;
        H.Step("activate samples.ipj", () =>
        {
            DesignProject p = null;
            foreach (DesignProject q in dpm.DesignProjects)
                if (string.Equals(q.FullFileName, Root + @"\samples.ipj", StringComparison.OrdinalIgnoreCase)) p = q;
            if (p == null) p = dpm.DesignProjects.AddExisting(Root + @"\samples.ipj");
            p.Activate(true);
            return dpm.ActiveDesignProject.Name;
        });
        int n = int.Parse(System.Environment.GetEnvironmentVariable("INVSCEN_N") ?? "3");
        string list = System.Environment.GetEnvironmentVariable("INVSCEN_OPEN") ?? @"Assemblies\Test Station\Test Station.iam";
        foreach (string rel in list.Split(';'))
        {
            string path = System.IO.Path.IsPathRooted(rel) ? rel : Root + @"\Models\" + rel;
            for (int i = 1; i <= n; i++)
            {
                H.Reset();
                H.Step("open " + i + " " + System.IO.Path.GetFileName(path), () =>
                {
                    var d = (Document)H.App.Documents.Open(path, true);
                    return "refdocs " + d.AllReferencedDocuments.Count;
                }, 900);
                H.Step("walk refs " + i, () =>
                {
                    Sync("start", "go");
                    calls = 0;
                    var sw = System.Diagnostics.Stopwatch.StartNew();
                    int m = Walk(H.App.ActiveDocument.File, new System.Collections.Generic.HashSet<string>());
                    var t = sw.Elapsed;
                    Sync("end", "done");
                    return "missing " + m + ", ~" + calls + " calls, " + (t.TotalMilliseconds * 1000 / calls).ToString("F0") + " us/call";
                }, 900);
                H.Step("close " + i, () => { H.App.Documents.CloseAll(false); return "docs open " + H.App.Documents.Count; }, 600);
            }
        }
    }
}
