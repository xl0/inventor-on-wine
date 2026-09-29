// Helper for UI work: run the Inventor command INVSCEN_CMD (ControlDefinition internal
// name) without waiting (Execute2(false)); INVSCEN_CMD=list:PATTERN lists matching names.
// Run with INVSCEN_KEEP=1 to keep the open documents; INVSCEN_OPEN=WINPATH opens one first.
using System;
using Inventor;

static class Scenario
{
    public static void Run()
    {
        string c = System.Environment.GetEnvironmentVariable("INVSCEN_CMD") ?? "list:";
        string open = System.Environment.GetEnvironmentVariable("INVSCEN_OPEN");
        if (open != null) H.Step("open", () => H.App.Documents.Open(open, true).FullFileName);
        H.Step("cmd", () =>
        {
            var defs = H.App.CommandManager.ControlDefinitions;
            if (c.StartsWith("list:"))
            {
                string pat = c.Substring(5).ToLowerInvariant();
                var r = new System.Collections.Generic.List<string>();
                foreach (ControlDefinition d in defs)
                    if ((d.InternalName + " " + d.DisplayName).ToLowerInvariant().Contains(pat)) r.Add(d.InternalName + " '" + d.DisplayName + "'");
                return string.Join("\n  ", r);
            }
            defs[c].Execute2(false);
            return c + " started";
        });
    }
}
