// UI-visible opens (084): with SilentOperation off, opens a sample part and a sample assembly
// (INVSCEN_OPEN=';'-separated, relative to C:\t\samples\2022\Models or absolute) so Inventor shows
// its real prompts (unavailable project locations, resolve-link, migration...). The harness's
// dialog watcher records and dismisses them: read the "DIALOG" lines (INVSCEN_DIALOGS=fail
// fails the open step). Run samples once first so C:\t\samples\2022 exists.
using System;
using Inventor;

static class Scenario
{
    public static void Run()
    {
        const string Root = @"C:\t\samples\2022";
        H.App.SilentOperation = false;
        // INVSCEN_PROJECT=0 keeps the active project (its workspace would resolve moved files by name).
        if (System.Environment.GetEnvironmentVariable("INVSCEN_PROJECT") != "0")
        H.Step("activate samples.ipj", () =>
        {
            var dpm = H.App.DesignProjectManager;
            DesignProject p = null;
            foreach (DesignProject q in dpm.DesignProjects)
                if (string.Equals(q.FullFileName, Root + @"\samples.ipj", StringComparison.OrdinalIgnoreCase)) p = q;
            if (p == null) p = dpm.DesignProjects.AddExisting(Root + @"\samples.ipj");
            p.Activate(true);
            return dpm.ActiveDesignProject.Name;
        });
        // INVSCEN_PROBE=<command internal name> (e.g. AppOptionsCmd): runs a command that opens a
        // dialog, to test the watcher (which dismisses it, so Execute returns).
        string probe = System.Environment.GetEnvironmentVariable("INVSCEN_PROBE");
        if (probe != null)
            H.Step("probe " + probe, () => { H.App.CommandManager.ControlDefinitions[probe].Execute(); System.Threading.Thread.Sleep(4000); return ""; });
        string list = System.Environment.GetEnvironmentVariable("INVSCEN_OPEN")
            ?? @"Parts\Flower\Flower.ipt;Assemblies\Scissors\scissors.iam";
        foreach (string rel in list.Split(';'))
        {
            string path = System.IO.Path.IsPathRooted(rel) ? rel : Root + @"\Models\" + rel;
            H.Reset();
            H.Step("open " + System.IO.Path.GetFileName(path), () =>
            {
                var d = (Document)H.App.Documents.Open(path, true);
                System.Threading.Thread.Sleep(2500);  // prompts that appear after the open returns
                int missing = 0;
                foreach (FileDescriptor fd in d.File.ReferencedFileDescriptors) if (fd.ReferenceMissing) missing++;
                return "refdocs " + d.AllReferencedDocuments.Count + ", missing refs " + missing;
            }, 300);
            H.Step("close", () => { H.App.Documents.CloseAll(false); return ""; });
        }
    }
}
