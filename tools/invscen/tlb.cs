// Typelib check (issue 031): read return types from Inventor's typelibs in
// this process. Windows: PartDocument.ComponentDefinition (RxInventor.tlb) ->
// VT_PTR -> PartComponentDefinition, ComponentDefinition.Occurrences
// (RxInventorImpl.impl) -> VT_PTR -> ComponentOccurrences.
using System;
using System.Runtime.InteropServices;
using System.Runtime.InteropServices.ComTypes;
using FUNCDESC = System.Runtime.InteropServices.ComTypes.FUNCDESC;
using TYPEATTR = System.Runtime.InteropServices.ComTypes.TYPEATTR;
using TYPEDESC = System.Runtime.InteropServices.ComTypes.TYPEDESC;

static class Scenario
{
    [DllImport("oleaut32.dll", CharSet = CharSet.Unicode, PreserveSig = false)]
    static extern ITypeLib LoadTypeLibEx(string file, int kind);

    static string Td(ITypeInfo ti, TYPEDESC td)
    {
        if (td.vt == 26) return "VT_PTR -> " + Td(ti, (TYPEDESC)Marshal.PtrToStructure(td.lpValue, typeof(TYPEDESC)));
        if (td.vt != 29) return "vt " + td.vt;
        ITypeInfo rt; string n, d, f; int c;
        ti.GetRefTypeInfo((int)(long)td.lpValue, out rt);
        rt.GetDocumentation(-1, out n, out d, out c, out f);
        return n;
    }

    // Return type of member `member` of type `type` in Inventor's typelib `file`.
    static string Ret(string file, string type, string member)
    {
        var tl = LoadTypeLibEx(@"C:\Program Files\Autodesk\Inventor 2027\Bin\" + file, 2 /* REGKIND_NONE */);
        for (int t = 0; t < tl.GetTypeInfoCount(); t++)
        {
            string n, d, f; int c;
            tl.GetDocumentation(t, out n, out d, out c, out f);
            if (n != type) continue;
            ITypeInfo ti; IntPtr pa;
            tl.GetTypeInfo(t, out ti);
            ti.GetTypeAttr(out pa);
            var ta = (TYPEATTR)Marshal.PtrToStructure(pa, typeof(TYPEATTR));
            for (int i = 0; i < ta.cFuncs; i++)
            {
                IntPtr pf; ti.GetFuncDesc(i, out pf);
                var fd = (FUNCDESC)Marshal.PtrToStructure(pf, typeof(FUNCDESC));
                ti.GetDocumentation(fd.memid, out n, out d, out c, out f);
                if (n == member) return Td(ti, fd.elemdescFunc.tdesc);
            }
        }
        throw new Exception(type + "." + member + " not found");
    }

    static void Check(string file, string type, string member, string expect)
    {
        H.Step(file + " " + type + "." + member, () =>
        {
            string r = Ret(file, type, member);
            H.Check(r == expect, r + " (expected " + expect + ")");
            return r;
        });
    }

    public static void Run()
    {
        Check("RxInventor.tlb", "PartDocument", "ComponentDefinition", "VT_PTR -> PartComponentDefinition");
        Check("RxInventorImpl.impl", "ComponentDefinition", "Occurrences", "VT_PTR -> ComponentOccurrences");
    }
}
