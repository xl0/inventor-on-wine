// Lists Inventor's open documents (use INVSCEN_KEEP=1 so connect doesn't close them). Not in `all`.
using System;
using Inventor;

static class Scenario
{
    public static void Run()
    {
        H.Step("open documents", () =>
        {
            var s = H.App.Documents.Count + " open:";
            foreach (Document d in H.App.Documents) s += " [" + d.FullFileName + "]";
            return s;
        });
    }
}
