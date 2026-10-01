// Document creation benchmark (098): INVSCEN_N rounds (default 3) of Documents.Add (visible,
// default template) + Close for each type of INVSCEN_TYPES (default "part asm drw").
// Round 1 after an Inventor start holds the one-time costs, later rounds the per-document ones.
// INVSCEN_SYNC=DIR (Windows path): around each add, create DIR\walk.start and wait for
// DIR\walk.go, then create DIR\walk.end and wait for DIR\walk.done (attach a tracer).
using System;
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
        int n = int.Parse(System.Environment.GetEnvironmentVariable("INVSCEN_N") ?? "3");
        string types = System.Environment.GetEnvironmentVariable("INVSCEN_TYPES") ?? "part asm drw";
        for (int i = 1; i <= n; i++)
            foreach (string t in types.Split(' '))
            {
                var e = t == "asm" ? DocumentTypeEnum.kAssemblyDocumentObject
                    : t == "drw" ? DocumentTypeEnum.kDrawingDocumentObject : DocumentTypeEnum.kPartDocumentObject;
                Document doc = null;
                H.Reset();
                H.Step("add " + t + " " + i, () =>
                {
                    Sync("start", "go");
                    doc = (Document)app.Documents.Add(e, app.FileManager.GetTemplateFile(e), true);
                    var v = app.ActiveView;
                    Sync("end", "done");
                    return v == null ? "no ActiveView" : "view " + v.Width + " x " + v.Height;
                });
                H.Step("close " + t + " " + i, () => { doc.Close(true); return "docs open " + app.Documents.Count; });
            }
    }
}
