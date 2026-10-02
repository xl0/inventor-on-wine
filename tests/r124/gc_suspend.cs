// 124 side finding: time GC.Collect() while another thread runs a tight managed loop without calls.
// CoreCLR must interrupt that thread (special user APC via QueueUserAPC2 when the OS accepts it,
// else SuspendThread + SetThreadContext redirection).
// Build (VM): csc /o+ /platform:x64 gc_suspend.cs ; run: dotnet gc_suspend.exe [LOOP_MS]
using System;
using System.Diagnostics;
using System.Threading;

static class P
{
    static volatile bool started;
    static long Spin(long ms)
    {
        long sum = 0, i = 0;
        Stopwatch sw = Stopwatch.StartNew();
        started = true;
        for (;;)
        {
            for (int k = 0; k < 100000000; k++) { sum += i ^ (sum >> 3); i++; }
            if (sw.ElapsedMilliseconds > ms) break;
        }
        return sum;
    }

    static int Main(string[] args)
    {
        long ms = args.Length > 0 ? long.Parse(args[0]) : 8000;
        Thread t = new Thread(() => Spin(ms));
        t.Start();
        while (!started) Thread.Sleep(1);
        Thread.Sleep(500);
        long max = 0;
        for (int n = 0; n < 5; n++)
        {
            Stopwatch sw = Stopwatch.StartNew();
            GC.Collect();
            long e = sw.ElapsedMilliseconds;
            if (e > max) max = e;
            Console.WriteLine("GC.Collect " + n + ": " + e + " ms");
            Thread.Sleep(200);
        }
        t.Join();
        Console.WriteLine("max " + max + " ms");
        return max < 1000 ? 0 : 1;
    }
}
