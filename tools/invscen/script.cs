// Scripting: iLogic rule created and run through the iLogic add-in's
// automation interface (drives a user parameter + an iProperty), and a VBA
// macro added to a document project and executed (VBA is a separate
// download for Inventor 2027, not installed on Wine or the VM: N/A on both). Apprentice is not registered in
// either install either (no Inventor.ApprenticeServer), so it isn't tested.
// Box 4x3xL, cm (the template's document units are inches).
using System;
using Inventor;

static class Scenario
{
    // Late-bound call through IDispatch::GetIDsOfNames/Invoke only: the
    // iLogic automation object is a .NET CCW without a registered typelib,
    // so C# dynamic (which asks for ITypeInfo first) fails, on Windows too.
    static object Call(object o, string name, params object[] args)
    {
        return o.GetType().InvokeMember(name, System.Reflection.BindingFlags.InvokeMethod, null, o, args);
    }
    static object Get(object o, string name)
    {
        return o.GetType().InvokeMember(name, System.Reflection.BindingFlags.GetProperty, null, o, null);
    }

    public static void Run()
    {
        var app = H.App;
        PartDocument doc = null;
        object il = null;

        H.Step("part with parameter L", () =>
        {
            doc = H.NewPart();
            var ext = H.Box(doc, 0, 0, 4, 3, 2);
            doc.ComponentDefinition.Parameters.UserParameters.AddByExpression("L", "30 mm", UnitsTypeEnum.kMillimeterLengthUnits);
            ((Parameter)((DistanceExtent)ext.Extent).Distance).Expression = "L";
            return H.Vol(doc, 36);
        });
        H.Step("iLogic add rule", () =>
        {
            var ai = app.ApplicationAddIns.get_ItemById("{3BDD8D79-2179-4B11-8A5A-257B1C0263AC}");
            if (!ai.Activated) ai.Activate();
            il = ai.Automation;
            Call(il, "AddRule", doc, "SetL", "Parameter(\"L\") = \"40 mm\"\r\niProperties.Value(\"Summary\", \"Title\") = \"iLogic \" & (6 * 7)\r\n");
            var r = Call(il, "GetRule", doc, "SetL");
            H.Check(r != null, "GetRule");
            return "rule " + Get(r, "Name") + ", " + ai.DisplayName;
        });
        H.Step("iLogic run rule", () =>
        {
            Call(il, "RunRule", doc, "SetL");
            doc.Update();
            string title = (string)doc.PropertySets["Inventor Summary Information"]["Title"].Value;
            H.Check(title == "iLogic 42", "title " + title);
            return "title '" + title + "', " + H.Vol(doc, 48);
        });
        H.Step("iLogic rule survives save/reopen", () =>
        {
            string s = H.Save(doc, "ilogic.ipt");
            doc.Close(true);
            doc = (PartDocument)app.Documents.Open(H.Out + "\\ilogic.ipt", true);
            var r = Call(il, "GetRule", doc, "SetL");
            H.Check(r != null && ((string)Get(r, "Text")).Contains("Parameter(\"L\") = \"40 mm\""), "rule text");
            return s + ", rule text ok";
        });
        H.Reset();
        H.Step("VBA macro", () =>
        {
            dynamic projs = app.VBAProjects;
            try { var n = projs.Count; }
            catch (System.Runtime.InteropServices.COMException e)
            {
                H.Check(e.HResult == unchecked((int)0x80040154), "VBAProjects.Count " + e.Message);
                return "N/A: VBA not installed (VBAProjects.Count: REGDB_E_CLASSNOTREG, same on the VM)";
            }
            dynamic vp = null;
            for (int i = 1; i <= projs.Count; i++)  // the part's own project
                if ((int)projs[i].ProjectType == 2 /* kDocumentVBAProject */) vp = projs[i];
            H.Check(vp != null, "document VBA project among " + projs.Count);
            dynamic comp = vp.VBProject.VBComponents.Add(1);  // vbext_ct_StdModule
            comp.Name = "InvScen";
            comp.CodeModule.AddFromString("Sub SetDesc()\r\n  ThisApplication.ActiveDocument.PropertySets(\"Design Tracking Properties\")(\"Description\").Value = \"VBA \" & (2 + 3)\r\nEnd Sub\r\n");
            vp.InventorVBAComponents["InvScen"].InventorVBAMembers["SetDesc"].Execute();
            string d = (string)doc.PropertySets["Design Tracking Properties"]["Description"].Value;
            H.Check(d == "VBA 5", "description " + d);
            return "projects " + projs.Count + ", description '" + d + "'";
        });
        H.Reset();
        H.Step("close", () => { doc.Close(true); return "docs open: " + app.Documents.Count; });
    }
}
