// Anark 3D PDF publishing (add-in Automation.Publish) of a holed box, and
// printing: drawing of it through DrawingPrintManager.PrintToFile on the
// default printer. Wine has no printer unless CUPS has one: tests/addprinter.exe
// adds a wineps printer on FILE: (Windows ships Microsoft Print to PDF).
using System;
using System.Linq;
using Inventor;

static class Scenario
{
    public static void Run()
    {
        var app = H.App;
        var tg = app.TransientGeometry;
        PartDocument part = null;

        H.Step("model", () =>
        {
            part = H.NewPart();
            H.Box(part, 0, 0, 4, 3, 2);
            var def = part.ComponentDefinition;
            var sk = def.Sketches.Add(def.WorkPlanes[3]);
            sk.SketchCircles.AddByCenterRadius(tg.CreatePoint2d(2, 1.5), 0.5);
            def.Features.ExtrudeFeatures.AddByThroughAllExtent(sk.Profiles.AddForSolid(),
                PartFeatureExtentDirectionEnum.kPositiveExtentDirection, PartFeatureOperationEnum.kCutOperation);
            return H.Save(part, "holed.ipt");
        });
        H.Step("3D PDF", () =>
        {
            var addin = app.ApplicationAddIns.get_ItemById("{3EE52B28-D6E0-4EA4-8AA6-C2A266DEBB88}");
            if (!addin.Activated) addin.Activate();
            object pub = addin.Automation;
            var o = app.TransientObjects.CreateNameValueMap();
            string pdf = H.Out + "\\holed3d.pdf";
            if (System.IO.File.Exists(pdf)) System.IO.File.Delete(pdf);
            o.Add("FileOutputLocation", pdf);
            o.Add("ExportAnnotations", 1);
            o.Add("ExportWfsAll", 1);
            o.Add("GenerateAndAttachSTEPFile", false);
            o.Add("LimitToEntitiesInDVRs", true);
            o.Add("VisualizationQuality", AccuracyEnum.kHigh);
            o.Add("ExportTemplate", app.FileManager.GetTemplateFile(DocumentTypeEnum.kPartDocumentObject)
                .Replace("Standard.ipt", "Sample Part Template.pdf"));
            o.Add("ExportDesignViewRepresentations",
                new[] { part.ComponentDefinition.RepresentationsManager.DesignViewRepresentations[1].Name });
            // InvokeMember: C# dynamic fails here on Windows too (0x80131165, no registered typelib)
            pub.GetType().InvokeMember("Publish", System.Reflection.BindingFlags.InvokeMethod, null, pub, new object[] { part, o });
            var b = System.IO.File.ReadAllBytes(pdf);
            string t = System.Text.Encoding.ASCII.GetString(b);
            H.Check(t.StartsWith("%PDF-"), "PDF header");
            // the model goes in as a /3D annotation with a PRC or U3D stream
            H.Check(t.Contains("/3D"), "/3D annotation");
            string fmt = t.Contains("/Subtype/PRC") || t.Contains("/Subtype /PRC") ? "PRC"
                : t.Contains("U3D") ? "U3D" : "?";
            return "holed3d.pdf " + b.Length + " bytes, " + t.Substring(0, 8).Trim() + ", 3D stream " + fmt;
        }, 300);
        DrawingDocument dwg = null;
        H.Reset();
        H.Step("drawing", () =>
        {
            dwg = (DrawingDocument)app.Documents.Add(DocumentTypeEnum.kDrawingDocumentObject,
                app.FileManager.GetTemplateFile(DocumentTypeEnum.kDrawingDocumentObject), true);
            dwg.ActiveSheet.DrawingViews.AddBaseView((_Document)part, tg.CreatePoint2d(10, 15), 1,
                ViewOrientationTypeEnum.kFrontViewOrientation, DrawingViewStyleEnum.kHiddenLineDrawingViewStyle);
            return "sheet " + dwg.ActiveSheet.Width + " x " + dwg.ActiveSheet.Height + " cm";
        });
        H.Step("printers", () =>
        {
            var l = System.Drawing.Printing.PrinterSettings.InstalledPrinters.Cast<string>().ToList();
            var pm = (DrawingPrintManager)dwg.PrintManager;
            return "installed: [" + string.Join(", ", l) + "], Inventor default: '" + pm.Printer + "'";
        });
        H.Step("print to file", () =>
        {
            var pm = (DrawingPrintManager)dwg.PrintManager;
            string f = H.Out + "\\print.out";
            if (System.IO.File.Exists(f)) System.IO.File.Delete(f);
            pm.ScaleMode = PrintScaleModeEnum.kPrintBestFitScale;
            pm.PrintToFile(f);
            var b = System.IO.File.ReadAllBytes(f);
            string head = System.Text.Encoding.ASCII.GetString(b, 0, Math.Min(b.Length, 16)).Split('\n', '\r')[0];
            // Wine: wineps PostScript (A4, landscape by rotation); VM: Microsoft Print to PDF
            H.Check((head.StartsWith("%!PS") || head.StartsWith("%PDF")) && b.Length > 20000, "print output");
            return pm.Printer + " -> print.out " + b.Length + " bytes, starts '" + head + "'";
        }, 300);
        H.Reset();
        H.Step("close", () =>
        {
            if (dwg != null) dwg.Close(true);
            part.Close(true);
            return "docs open: " + app.Documents.Count;
        });
    }
}
