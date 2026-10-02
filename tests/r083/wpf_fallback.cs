// 083: WPF font fallback probe. Measures text in font families that may be missing; WPF fails fast
// ("Unrecoverable system error", exit 0x80131623) when neither the family, its composite-font
// fallback targets (Latin on Win10 1809+: Segoe UI, Segoe UI Symbol, Ebrima) nor Arial exist.
// Build: csc /nologo /platform:x64 /r:WPF\PresentationCore.dll /r:WPF\WindowsBase.dll /r:System.Xaml.dll wpf_fallback.cs
// Run: wpf_fallback.exe [png=OUT.png] [FAMILY...]; exit 0 = every family measured (png: one line of text per family).
using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Windows;
using System.Windows.Media;
using System.Windows.Media.Imaging;

class Probe
{
    [STAThread]
    static int Main(string[] args)
    {
        foreach (string name in new[] { "Arial", "Segoe UI", "Segoe UI Symbol", "Ebrima", "Tahoma", "Times New Roman", "Courier New" })
        {
            bool found = false;
            foreach (FontFamily f in Fonts.SystemFontFamilies)
                if (string.Equals(f.Source, name, StringComparison.OrdinalIgnoreCase)) found = true;
            Console.WriteLine("installed {0}: {1}", name, found);
        }
        string png = null;
        if (args.Length > 0 && args[0].StartsWith("png="))
        {
            png = args[0].Substring(4);
            args = new List<string>(args).GetRange(1, args.Length - 1).ToArray();
        }
        DrawingVisual visual = new DrawingVisual();
        DrawingContext dc = visual.RenderOpen();
        dc.DrawRectangle(Brushes.White, null, new Rect(0, 0, 400, 400));
        double y = 4;
        if (args.Length == 0)
            args = new[] { "Tahoma", "Arial", "Segoe UI", "Global User Interface", "No Such Font 083" };
        foreach (string name in args)
        {
            Console.Write("measure '{0}': ", name);
            Console.Out.Flush();
            Typeface face = new Typeface(name);
            FormattedText text = new FormattedText("A1 \u0416 " + name, CultureInfo.InvariantCulture,
                    FlowDirection.LeftToRight, face, 12.0, Brushes.Black, 1.0);
            GlyphTypeface glyphs;
            face.TryGetGlyphTypeface(out glyphs);
            Console.WriteLine("width {0:F2} height {1:F2} glyph typeface {2}", text.Width, text.Height,
                    glyphs == null ? "none" : glyphs.FamilyNames[CultureInfo.GetCultureInfo("en-US")]);
            dc.DrawText(text, new Point(4, y));
            y += text.Height + 4;
        }
        dc.Close();
        if (png != null)
        {
            RenderTargetBitmap bitmap = new RenderTargetBitmap(400, (int)y, 96, 96, PixelFormats.Pbgra32);
            bitmap.Render(visual);
            PngBitmapEncoder encoder = new PngBitmapEncoder();
            encoder.Frames.Add(BitmapFrame.Create(bitmap));
            using (FileStream stream = File.Create(png)) encoder.Save(stream);
        }
        return 0;
    }
}
