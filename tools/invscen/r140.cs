// 140 repro setup: a new visible part with a box (something to undo), then the iLogic Browser
// command (pane opens next to Model). Left open for the manual steps: click the part node in the
// iLogic pane, close the pane, press Ctrl+Z.
using System;
using Inventor;

static class Scenario
{
    public static void Run()
    {
        H.Step("part + box", () => { H.Box(H.NewPart(), 0, 0, 4, 3, 2); return "ok"; });
        H.Step("iLogic browser", () =>
        {
            H.App.CommandManager.ControlDefinitions["iLogic.RuleBrowser"].Execute2(false);
            return "started";
        });
    }
}
