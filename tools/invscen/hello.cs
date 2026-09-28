// Smoke: connect, then open + close one visible part (the first document
// after an Inventor start; on Wine its ActiveView was null twice, issue 037).
using Inventor;

static class Scenario
{
    public static void Run()
    {
        H.Step("first part view", () =>
        {
            var doc = H.NewPart();
            try
            {
                var v = H.App.ActiveView;
                H.Check(v != null, "ActiveView");
                return "view " + v.Width + " x " + v.Height;
            }
            finally { doc.Close(true); }
        });
    }
}
