// Inventor COM scenario harness: compiled together with one scenario file
// (class Scenario { public static void Run() }) by tools/invscen/run.sh.
using System;
using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Threading;
using Inventor;

static class H
{
    public static Application App;
    public static string Out;   // Windows path of this run's artifact dir
    static int fails;
    public static void Reset() { fails = 0; }  // probes: keep going after a failure

    // Runs body on a worker thread (MTA, like Main); prints PASS/FAIL with
    // body's returned detail string and timing. A timeout aborts the run
    // (a hung COM call can't be cancelled). A failed step skips the rest.
    public static void Step(string name, Func<string> body, int timeoutSec = 120)
    {
        if (fails > 0) { Console.WriteLine("SKIP " + name); return; }
        string detail = null; Exception err = null;
        var sw = Stopwatch.StartNew();
        var t = new Thread(() => { try { detail = body(); } catch (Exception e) { err = e; } });
        t.IsBackground = true;
        t.Start();
        if (!t.Join(timeoutSec * 1000))
        {
            Console.WriteLine("FAIL {0} ({1:F1}s): TIMEOUT after {2}s", name, sw.Elapsed.TotalSeconds, timeoutSec);
            System.Environment.Exit(2);
        }
        if (err != null)
        {
            fails++;
            Console.WriteLine("FAIL {0} ({1:F1}s): HRESULT 0x{2:X8} {3}", name, sw.Elapsed.TotalSeconds, err.HResult, err);
        }
        else
            Console.WriteLine("PASS {0} ({1:F1}s){2}", name, sw.Elapsed.TotalSeconds,
                string.IsNullOrEmpty(detail) ? "" : ": " + detail);
    }

    // Throws unless |actual - expected| <= tol * |expected|.
    public static string Near(string what, double actual, double expected, double tol = 1e-6)
    {
        string s = string.Format("{0}={1:R} (expected {2:R})", what, actual, expected);
        if (Math.Abs(actual - expected) > tol * Math.Abs(expected)) throw new Exception("mismatch: " + s);
        return s;
    }

    public static void Check(bool ok, string what) { if (!ok) throw new Exception("check failed: " + what); }

    [MTAThread]
    static int Main(string[] args)
    {
        Out = args[0];
        System.IO.Directory.CreateDirectory(Out);
        Step("connect", () =>
        {
            // Attach only (run.sh starts Inventor): never spawn a second instance.
            while (App == null)
                try { App = (Application)Marshal.GetActiveObject("Inventor.Application"); }
                catch (COMException) { Thread.Sleep(2000); }
            App.SilentOperation = true;
            // Deterministic start: discard whatever is open (dedicated test Inventor).
            int n = App.Documents.Count;
            App.Documents.CloseAll(false);
            return App.SoftwareVersion.DisplayName + ", closed " + n + " open docs";
        }, 300);
        Scenario.Run();
        if (App != null) try { App.SilentOperation = false; } catch (Exception) { }
        Console.WriteLine(fails == 0 ? "RESULT PASS" : "RESULT FAIL");
        return fails == 0 ? 0 : 1;
    }
}
