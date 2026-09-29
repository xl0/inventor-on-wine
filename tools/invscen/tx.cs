// Probe (INVSCEN_KEEP=1): undo/redo transaction names and the active document's leaf occurrences.
using System;
using Inventor;
static class Scenario
{
    public static void Run()
    {
        H.Step("tx", () =>
        {
            var tm = H.App.TransactionManager;
            var u = new System.Collections.Generic.List<string>(); var r = new System.Collections.Generic.List<string>();
            foreach (Transaction t in tm.CommittedTransactions) u.Add(t.DisplayName);
            foreach (Transaction t in tm.UndoneTransactions) r.Add(t.DisplayName);
            return "current " + (tm.CurrentTransaction == null ? "none" : tm.CurrentTransaction.DisplayName)
                + "; committed [" + string.Join(", ", u) + "]; undone [" + string.Join(", ", r) + "]";
        });
        H.Step("occurrences", () =>
        {
            var a = H.App.ActiveDocument as AssemblyDocument;
            if (a == null) return "active doc is not an assembly";
            var s = new System.Collections.Generic.List<string>();
            foreach (ComponentOccurrence o in a.ComponentDefinition.Occurrences) s.Add(o.Name);
            var d = new System.Collections.Generic.List<string>();
            foreach (Document x in H.App.Documents) d.Add(System.IO.Path.GetFileName(x.FullFileName) + (x.Dirty ? "*" : ""));
            return string.Join(", ", s) + "; documents: " + string.Join(", ", d);
        });
    }
}
