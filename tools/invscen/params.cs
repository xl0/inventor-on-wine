// Parameters/equations, iProperties, material + mass on one 4x3xh part:
// user parameter L drives the extrusion (h = L), changes propagate to the
// volume; iProperties and material survive save/reopen. cm.
using System;
using Inventor;

static class Scenario
{
    public static void Run()
    {
        var app = H.App;
        PartDocument doc = null;
        UserParameter L = null;
        ExtrudeFeature ext = null;
        const string name = "params.ipt";

        H.Step("box driven by user parameter", () =>
        {
            doc = H.NewPart();
            ext = H.Box(doc, 0, 0, 4, 3, 2);
            var ps = doc.ComponentDefinition.Parameters;
            L = ps.UserParameters.AddByExpression("L", "30 mm", UnitsTypeEnum.kMillimeterLengthUnits);
            var d = (Parameter)((DistanceExtent)ext.Extent).Distance;
            d.Expression = "L";
            return d.Name + " = " + d.Expression + ", " + H.Vol(doc, 36);
        });
        H.Step("change parameter", () =>
        {
            L.Expression = "45 mm";
            return H.Vol(doc, 54);
        });
        H.Step("equations", () =>
        {
            var ps = doc.ComponentDefinition.Parameters;
            var Wd = ps.UserParameters.AddByExpression("Wd", "L / 3 + 5 mm", UnitsTypeEnum.kMillimeterLengthUnits);
            L.Expression = "2 * 12.5 mm + 5 mm";  // 30 mm
            string cyc;
            try { L.Expression = "Wd * 2"; cyc = "cycle accepted!"; }
            catch (System.Runtime.InteropServices.COMException e) { cyc = "cycle rejected 0x" + e.HResult.ToString("X8"); }
            H.Check((double)L.Value == 3.0, "L after cycle: " + L.Expression);
            H.Check(ps.IsExpressionValid("L * 2", "mm") && !ps.IsExpressionValid("L *", "mm"), "IsExpressionValid");
            return H.Near("Wd", (double)Wd.Value, 1.5, 1e-12) + ", " + cyc + ", " + H.Vol(doc, 36);
        });
        H.Step("iProperties write", () =>
        {
            var p = doc.PropertySets;
            p["Design Tracking Properties"]["Part Number"].Value = "INVSCEN-001";
            p["Inventor Summary Information"]["Title"].Value = "Wine test part";
            p["Inventor User Defined Properties"].Add("hello Wine", "TextProp");
            p["Inventor User Defined Properties"].Add(42.5, "NumProp");
            return "custom " + p["Inventor User Defined Properties"].Count;
        });
        H.Step("material Steel", () =>
        {
            Asset steel = null;
            foreach (Asset a in app.ActiveMaterialLibrary.MaterialAssets)
                if (a.DisplayName == "Steel") steel = a;
            H.Check(steel != null, "Steel in " + app.ActiveMaterialLibrary.DisplayName);
            doc.ActiveMaterial = steel.CopyTo(doc);
            doc.Update();
            var mp = H.Mass(doc.ComponentDefinition);
            return H.Near("density g/cm3", mp.Mass * 1000 / mp.Volume, 7.85, 1e-6) + ", mass " + mp.Mass + " kg";
        });
        H.Step("save", () => H.Save(doc, name));
        H.Step("close", () => { doc.Close(true); return "docs open: " + app.Documents.Count; });
        H.Step("reopen + verify", () =>
        {
            doc = (PartDocument)app.Documents.Open(H.Out + "\\" + name, true);
            var p = doc.PropertySets;
            var u = p["Inventor User Defined Properties"];
            H.Check((string)p["Design Tracking Properties"]["Part Number"].Value == "INVSCEN-001", "part number");
            H.Check((string)p["Inventor Summary Information"]["Title"].Value == "Wine test part", "title");
            H.Check((string)u["TextProp"].Value == "hello Wine", "TextProp");
            H.Check(Convert.ToDouble(u["NumProp"].Value) == 42.5, "NumProp " + u["NumProp"].Value);
            H.Check(doc.ActiveMaterial.DisplayName == "Steel", "material " + doc.ActiveMaterial.DisplayName);
            var ps = doc.ComponentDefinition.Parameters;
            return "L=" + ps.UserParameters["L"].Expression + ", " + H.Vol(doc, 36) + ", mass "
                + H.Near("kg", H.Mass(doc.ComponentDefinition).Mass, 36 * 7.85 / 1000, 1e-6);
        });
        H.Step("export parameters XML", () =>
        {
            string x = H.Out + "\\params.xml";
            if (System.IO.File.Exists(x)) System.IO.File.Delete(x);
            doc.ComponentDefinition.Parameters.ExportToXML(x);
            string t = System.IO.File.ReadAllText(x);
            H.Check(t.Contains("Wd"), "Wd in xml");
            return x + " " + t.Length + " chars";
        });
        H.Step("close", () => { doc.Close(true); return "docs open: " + app.Documents.Count; });
    }
}
