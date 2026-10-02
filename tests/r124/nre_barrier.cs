// 124: NullReferenceException raised inside the JIT write barrier (reference store into a field of a
// null object), with a poisoned stack below the faulting frame. CoreCLR's vectored handler copies the
// exception CONTEXT into a plain local and calls LocateXStateFeature(copy, XSTATE_CET_U) on it.
// Build (VM): csc /unsafe /o+ /platform:x64 nre_barrier.cs ; run: dotnet nre_barrier.exe [N]
// next to nre_barrier.runtimeconfig.json:
// {"runtimeOptions":{"framework":{"name":"Microsoft.NETCore.App","version":"10.0.0"},"rollForward":"LatestMinor"}}
using System;
using System.Runtime.CompilerServices;

class C { public object f; }

static class P
{
    [MethodImpl(MethodImplOptions.NoInlining)]
    static void Store(C c, object o) { c.f = o; }

    [MethodImpl(MethodImplOptions.NoInlining)]
    static unsafe int Poison(int n, byte v)
    {
        byte* p = stackalloc byte[n];
        for (int i = 0; i < n; i++) p[i] = v;
        return p[n / 2];
    }

    static int Main(string[] args)
    {
        int n = args.Length > 0 ? int.Parse(args[0]) : 2000, caught = 0;
        object o = new object();
        Console.WriteLine("runtime " + Environment.Version);
        for (int i = 0; i < n; i++)
        {
            Poison(0x8000, (byte)(0xcc + i));
            try { Store(null, o); }
            catch (NullReferenceException) { caught++; }
        }
        Console.WriteLine("caught " + caught + " of " + n);
        return caught == n ? 0 : 1;
    }
}
