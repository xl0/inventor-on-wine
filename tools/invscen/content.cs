// Content Center (desktop libraries, issue 064): active project, CC files path, top
// categories of the CC tree. Fails if the tree is empty (Wine: libraries not attached).
using System;
using Inventor;
static class Scenario
{
    public static void Run()
    {
        var app = H.App;
        H.Step("project", () =>
        {
            var p = app.DesignProjectManager.ActiveDesignProject;
            return p.Name + " " + p.FullFileName + ", cc path '" + p.ContentCenterPath + "'";
        });
        H.Step("cc tree", () =>
        {
            var s = new System.Collections.Generic.List<string>();
            foreach (ContentTreeViewNode n in app.ContentCenter.TreeViewTopNode.ChildNodes)
                s.Add(n.DisplayName + "(" + n.ChildNodes.Count + ")");
            H.Check(s.Count > 0, "Content Center categories");
            return string.Join(", ", s);
        });
    }
}
