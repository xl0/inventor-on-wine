// Probe: list application add-ins (id, name, activated, load behaviour).
using System;
using Inventor;

static class Scenario
{
    public static void Run()
    {
        H.Step("addins", () =>
        {
            foreach (ApplicationAddIn a in H.App.ApplicationAddIns)
                Console.WriteLine("  {0} {1} act={2} {3}", a.ClassIdString, a.DisplayName, a.Activated, a.LoadBehavior);
            return H.App.ApplicationAddIns.Count + " add-ins";
        });
    }
}
