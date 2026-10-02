// Environment variation (118): the part/asm/drawing/export round trip under non-ASCII, special-character
// and long (200..300 chars) directories. Failures list Inventor's error text. Not in `all`.
using System;
using System.Text;
using Inventor;

static class Scenario
{
    const string Cyr = "\u0414\u043e\u043a\u0443\u043c\u0435\u043d\u0442\u044b", Prj = "\u041f\u0440\u043e\u0435\u043a\u0442 \u6e2c\u8a66 \u00e4";
    const string Name = "\u0434\u0435\u0442\u0430\u043b\u044c \u6e2c\u8a66 \u00e4";  // file stem
    const string Txt = "\u041f\u0440\u0438\u0432\u0435\u0442 \u6e2c\u8a66 \u00e4\u00f6\u00fc \u03a9";

    static string Long(string root, int dirLen)
    {
        var sb = new StringBuilder(root);
        while (sb.Length < dirLen) sb.Append("\\seg_").Append(new string('x', 20)).Append(sb.Length % 10);
        return sb.ToString();
    }
    static string Fix(string p) { return p.Length > 240 ? @"\\?\" + p : p; }
    [System.Runtime.InteropServices.DllImport("kernel32.dll", CharSet = System.Runtime.InteropServices.CharSet.Unicode, SetLastError = true)]
    static extern bool CreateDirectoryW(string p, IntPtr sa);
    [System.Runtime.InteropServices.DllImport("kernel32.dll", CharSet = System.Runtime.InteropServices.CharSet.Unicode)]
    static extern uint GetFileAttributesW(string p);
    static bool Ex(string p)
    {
        uint a = GetFileAttributesW(@"\\?\" + p), b = GetFileAttributesW(p);
        Console.WriteLine("  attrs {0} chars: prefixed {1:x} plain {2:x}", p.Length, a, b);
        return a != 0xFFFFFFFF || b != 0xFFFFFFFF;
    }
    // H.Translate without .NET path APIs (they refuse > 259 chars)
    static string Tr(object doc, string clsid, string file, params object[] opts)
    {
        var t = (TranslatorAddIn)H.App.ApplicationAddIns.get_ItemById(clsid);
        if (!t.Activated) t.Activate();
        var to = H.App.TransientObjects; var ctx = to.CreateTranslationContext();
        ctx.Type = IOMechanismEnum.kFileBrowseIOMechanism;
        var o = to.CreateNameValueMap(); t.get_HasSaveCopyAsOptions(doc, ctx, o);
        for (int i = 0; i < opts.Length; i += 2) { try { o.Remove(opts[i]); } catch (System.Runtime.InteropServices.COMException) { } o.Add((string)opts[i], opts[i + 1]); }
        var dm = to.CreateDataMedium(); dm.FileName = file;
        t.SaveCopyAs(doc, ctx, o, dm);
        H.Check(Ex(file), file.Length + " chars written");
        return file.Substring(file.LastIndexOf('\\') + 1).Length + "-char name ok";
    }
    static void Mk(string dir)  // .NET's Directory.CreateDirectory refuses > 248 even with \\?\
    {
        for (int i = dir.IndexOf('\\', 3); ; i = dir.IndexOf('\\', i + 1))
        {
            string q = i < 0 ? dir : dir.Substring(0, i);
            if (!CreateDirectoryW(@"\\?\" + q, IntPtr.Zero) && System.Runtime.InteropServices.Marshal.GetLastWin32Error() != 183)
                throw new System.ComponentModel.Win32Exception(System.Runtime.InteropServices.Marshal.GetLastWin32Error(), "mkdir " + q.Length);
            if (i < 0) break;
        }
    }

    static void Case(string label, string dir)
    {
        var app = H.App; var tg = app.TransientGeometry;
        string part = dir + "\\" + Name + ".ipt", asm = dir + "\\" + Name + ".iam", drw = dir + "\\" + Name + ".idw";
        PartDocument pd = null; AssemblyDocument ad = null; DrawingDocument dd = null;
        H.Reset();
        H.Step(label + ": create+save part (dir " + dir.Length + " chars)", () =>
        {
            Mk(dir);
            pd = H.NewPart(); H.Box(pd, 0, 0, 4, 3, 2);
            var sk = pd.ComponentDefinition.Sketches.Add(pd.ComponentDefinition.WorkPlanes[3]);
            sk.TextBoxes.AddByRectangle(tg.CreatePoint2d(6, 0), tg.CreatePoint2d(12, 2), Txt);
            pd.PropertySets["Inventor Summary Information"]["Title"].Value = Txt;
            pd.SaveAs(part, false);
            return part.Length + " chars, " + (Ex(part) ? "exists" : "MISSING");
        });
        H.Step(label + ": reopen part", () =>
        {
            pd.Close(true);
            pd = (PartDocument)app.Documents.Open(part, true);
            string t = (string)pd.PropertySets["Inventor Summary Information"]["Title"].Value;
            H.Check(t == Txt, "title roundtrip '" + t + "'");
            string tb = pd.ComponentDefinition.Sketches[2].TextBoxes[1].Text;
            H.Check(tb == Txt, "sketch text '" + tb + "'");
            H.Check(pd.FullFileName == part, "FullFileName " + pd.FullFileName);
            return H.Vol(pd, 24);
        });
        H.Step(label + ": export STEP+STL+png", () =>
        {
            {
                string s = Tr(pd, "{90AF7F40-0C01-11D5-8E83-0010B541CD80}", dir + "\\" + Name + ".stp", "ApplicationProtocolType", 3);
                string l = Tr(pd, "{533E9A98-FC3B-11D4-8E7E-0010B541CD80}", dir + "\\" + Name + ".stl", "OutputFileType", 0);
                pd.Views[1].SaveAsBitmap(dir + "\\" + Name + ".png", 400, 300);
                H.Check(Ex(dir + "\\" + Name + ".png"), "png written");
                return s + "; " + l;
            }
        });
        H.Step(label + ": import STEP back", () =>
        {
            var d = app.Documents.Open(dir + "\\" + Name + ".stp", true);
            try { return d.DocumentType + " " + H.Near("volume", H.Mass(((PartDocument)d).ComponentDefinition).Volume, 24, 1e-4); }
            finally { d.Close(true); }
        });
        H.Step(label + ": assembly save/reopen", () =>
        {
            ad = (AssemblyDocument)app.Documents.Add(DocumentTypeEnum.kAssemblyDocumentObject,
                app.FileManager.GetTemplateFile(DocumentTypeEnum.kAssemblyDocumentObject), true);
            ad.ComponentDefinition.Occurrences.Add(part, tg.CreateMatrix());
            ad.SaveAs(asm, false); app.Documents.CloseAll(false);
            ad = (AssemblyDocument)app.Documents.Open(asm, true);
            var fd = ad.File.ReferencedFileDescriptors[1];
            H.Check(fd.FullFileName == part && fd.ReferenceMissing == false, "ref '" + fd.FullFileName + "' missing=" + fd.ReferenceMissing);
            return H.Near("volume", H.Mass(ad.ComponentDefinition).Volume, 24, 1e-5);
        });
        H.Step(label + ": drawing save/reopen", () =>
        {
            dd = (DrawingDocument)app.Documents.Add(DocumentTypeEnum.kDrawingDocumentObject,
                app.FileManager.GetTemplateFile(DocumentTypeEnum.kDrawingDocumentObject), true);
            drw = dir + "\\" + Name + System.IO.Path.GetExtension(app.FileManager.GetTemplateFile(DocumentTypeEnum.kDrawingDocumentObject));
            dd.ActiveSheet.DrawingViews.AddBaseView((_Document)ad, tg.CreatePoint2d(10, 15), 1,
                ViewOrientationTypeEnum.kFrontViewOrientation, DrawingViewStyleEnum.kHiddenLineDrawingViewStyle);
            dd.SaveAs(drw, false); app.Documents.CloseAll(false);
            dd = (DrawingDocument)app.Documents.Open(drw, true);
            var fd = dd.File.ReferencedFileDescriptors[1];
            H.Check(!fd.ReferenceMissing, "drawing ref missing: " + fd.FullFileName);
            return "views " + dd.ActiveSheet.DrawingViews.Count + " ref " + fd.FullFileName.Length + " chars";
        });
        H.Step(label + ": close all", () => { app.Documents.CloseAll(false); return ""; });
    }

    public static void Run()
    {
        string prof = System.Environment.GetFolderPath(System.Environment.SpecialFolder.UserProfile);
        string root = H.Out + "\\..\\paths_data";
        string only = System.Environment.GetEnvironmentVariable("INVSCEN_CASES") ?? "";
        var cases = new System.Collections.Generic.List<string[]>();
        cases.Add(new[] { "unicode", prof + "\\" + Cyr + "\\" + Prj });
        cases.Add(new[] { "special", prof + "\\a b&c#d%e[1]'f;g,h!~@$^(x)=+" });
        cases.Add(new[] { "long200", Long(prof + "\\" + Prj, 205) });
        cases.Add(new[] { "long245", Long(prof + "\\" + Prj, 235) });
        cases.Add(new[] { "long300", Long(prof + "\\" + Prj, 290) });
        foreach (var c in cases)
            if (only == "" || c[0].Contains(only)) Case(c[0], c[1]);
    }
}
