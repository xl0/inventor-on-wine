// Viewport probe (037): open part/box.ipt (run `part` first) visible, fit, save
// a bitmap, and leave the part open for a screenshot of the graphics window.
using System;
using System.IO;
using Inventor;

static class Scenario
{
    public static void Run()
    {
        var app = H.App;
        H.Step("open", () =>
        {
            var doc = app.Documents.Open(H.Out + "\\..\\part\\box.ipt", true);
            return doc.FullFileName;
        });
        H.Step("view bitmap", () =>
        {
            var v = app.ActiveView;
            v.GoHome();
            string bmp = H.Out + "\\box.png";
            v.SaveAsBitmap(bmp, 800, 600);
            return bmp + " " + new FileInfo(bmp).Length + " bytes, view " + v.Width + "x" + v.Height
                + ", style " + v.DisplayMode;
        });
    }
}
