// Occurrence placement benchmark (097): asmbig's "place 200 occurrences" step alone,
// INVSCEN_N rounds (default 3) into a new assembly each, with the time split per API call
// kind (CreateVector, SetTranslation, Occurrences.Add, Grounded). INVSCEN_PLACE=hidden
// adds the documents invisible (no graphics updates), `cheap` also times 200 rounds of a
// trivial call (Occurrences.Count) per round. INVSCEN_SYNC=DIR: around each placement,
// DIR\walk.start -> wait DIR\walk.go, DIR\walk.end -> wait DIR\walk.done (as openbench).
using System;
using System.Diagnostics;
using Inventor;

static class Scenario
{
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
        var app = H.App;
        var tg = app.TransientGeometry;
        string box = H.Out + "\\box.ipt";
        string mode = System.Environment.GetEnvironmentVariable("INVSCEN_PLACE") ?? "";
        bool visible = !mode.Contains("hidden");
        int n = int.Parse(System.Environment.GetEnvironmentVariable("INVSCEN_N") ?? "3");
        H.Step("make part", () =>
        {
            var p = H.NewPart();
            H.Box(p, 0, 0, 4, 3, 2);
            H.Save(p, "box.ipt");
            p.Close(true);
            return box;
        });
        for (int r = 1; r <= n; r++)
        {
            H.Reset();
            AssemblyDocument doc = null;
            H.Step("new assembly " + r, () =>
            {
                doc = (AssemblyDocument)app.Documents.Add(DocumentTypeEnum.kAssemblyDocumentObject,
                    app.FileManager.GetTemplateFile(DocumentTypeEnum.kAssemblyDocumentObject), visible);
                return visible ? "visible" : "hidden";
            });
            H.Step("place 200 " + r, () =>
            {
                var occs = doc.ComponentDefinition.Occurrences;
                var m = tg.CreateMatrix();
                var sw = new Stopwatch[4];
                for (int k = 0; k < 4; k++) sw[k] = new Stopwatch();
                Sync("start", "go");
                var all = Stopwatch.StartNew();
                for (int i = 0; i < 20; i++)
                    for (int j = 0; j < 10; j++)
                    {
                        sw[0].Start(); var v = tg.CreateVector(5 * i, 5 * j, 0); sw[0].Stop();
                        sw[1].Start(); m.SetTranslation(v); sw[1].Stop();
                        sw[2].Start(); var o = occs.Add(box, m); sw[2].Stop();
                        sw[3].Start(); o.Grounded = true; sw[3].Stop();
                    }
                var t = all.Elapsed;
                Sync("end", "done");
                string s = string.Format("total {0:F2}s; per occurrence ms: vector {1:F2} settrans {2:F2} add {3:F2} grounded {4:F2}",
                    t.TotalSeconds, sw[0].Elapsed.TotalMilliseconds / 200, sw[1].Elapsed.TotalMilliseconds / 200,
                    sw[2].Elapsed.TotalMilliseconds / 200, sw[3].Elapsed.TotalMilliseconds / 200);
                if (mode.Contains("cheap"))
                {
                    var c = Stopwatch.StartNew();
                    int x = 0;
                    for (int i = 0; i < 200; i++) x += occs.Count;
                    s += string.Format("; Count {0:F2} ms", c.Elapsed.TotalMilliseconds / 200);
                }
                return s;
            }, 600);
            H.Step("close " + r, () => { doc.Close(true); return "docs open: " + app.Documents.Count; });
        }
    }
}
