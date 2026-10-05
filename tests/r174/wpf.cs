// wpf.cs: the layout of frame.c in WPF (.NET 4.8), to see what WPF leaves stale after the window
// manager resizes the window (174). Same colours and sizes (in WPF units), so check.py applies
// (SCALE=1.5 at 144 DPI). Logs the size-move messages of the top-level window and a STATE line 600 ms
// after the last one (outside = WM_SIZE outside the size-move pair).
// Usage: wpf.exe [SECS] [X Y W H] [OPTION..]
//   hosted   a WinForms frame with four child HwndSources laid out on its Resize (Inventor: MFC frame +
//            WPF ribbon / browser); default: one WPF Window with a DockPanel
//   sw       software rendering
//   swlater=MS  software rendering forced MS after the windows were created with hardware rendering (Inventor's
//            ribbon host sets RenderOptions.ProcessRenderMode = SoftwareOnly at some point after startup)
//   defer    hosted: no layout between WM_ENTERSIZEMOVE and WM_EXITSIZEMOVE
//   slow=MS  hosted: the layout takes MS
//   partial  hosted: a square in the ribbon is black in the frame WPF presents after a resize of the pane
//            and gets the pane's colour 300 ms later, which WPF presents as a dirty rectangle; a black
//            square on the screen afterwards is an old frame shown again
// Build (prefix with .NET 4.8): see build-wpf.sh
using System;
using System.Runtime.InteropServices;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Interop;
using System.Windows.Media;
using System.Windows.Threading;

static class Probe
{
    const int RibbonH = 120, BrowserW = 240, StatusH = 24;
    static int start = Environment.TickCount, enter, leave, depth, late, slow;
    static bool defer, partial;
    static DispatcherTimer timer;
    static Func<string> state;
    static double scale = 1;

    [DllImport("user32.dll")] static extern bool GetClientRect(IntPtr hwnd, out RECT rc);
    [DllImport("user32.dll")] static extern bool GetWindowRect(IntPtr hwnd, out RECT rc);
    [DllImport("user32.dll")] static extern bool MoveWindow(IntPtr hwnd, int x, int y, int w, int h, bool repaint);
    [StructLayout(LayoutKind.Sequential)] struct RECT { public int l, t, r, b; public override string ToString() { return l + "," + t + "-" + r + "," + b; } }
    [StructLayout(LayoutKind.Sequential)] struct WINDOWPOS { public IntPtr hwnd, after; public int x, y, cx, cy, flags; }

    static void Out(string s) { Console.WriteLine("{0,6} {1}", Environment.TickCount - start, s); Console.Out.Flush(); }

    static Border Pane(Color c, string text)
    {
        var b = new Border { BorderBrush = Brushes.White, BorderThickness = new Thickness(4), Background = new SolidColorBrush(c),
                             SnapsToDevicePixels = true, UseLayoutRounding = true };
        return b;
    }

    // size-move messages of the top-level window, as frame.c logs them
    static IntPtr Hook(IntPtr hwnd, int msg, IntPtr wp, IntPtr lp, ref bool handled) { Message(hwnd, msg, lp); return IntPtr.Zero; }
    static void Message(IntPtr hwnd, int msg, IntPtr lp)
    {
        switch (msg)
        {
        case 0x231: enter++; depth++; Out("WM_ENTERSIZEMOVE"); break;
        case 0x232: leave++; depth--; Out("WM_EXITSIZEMOVE"); Arm(); break;
        case 0x47:
            var pos = (WINDOWPOS)Marshal.PtrToStructure(lp, typeof(WINDOWPOS));
            if ((pos.flags & 3) != 3) Out(String.Format("WM_WINDOWPOSCHANGED {0},{1} {2}x{3} flags {4:x} depth {5}", pos.x, pos.y, pos.cx, pos.cy, pos.flags, depth));
            Arm();
            break;
        case 0x5:
            Out(String.Format("WM_SIZE {0}x{1} depth {2}", (short)(lp.ToInt64() & 0xffff), (short)((lp.ToInt64() >> 16) & 0xffff), depth));
            if (depth == 0 && enter > 0) late++;
            break;
        }
    }
    static void Arm() { timer.Stop(); timer.Start(); }

    [STAThread]
    static int Main(string[] args)
    {
        int secs = args.Length > 0 ? int.Parse(args[0]) : 60, x = 200, y = 200, w = 800, h = 600;
        bool hosted = false;
        if (args.Length > 4) { x = int.Parse(args[1]); y = int.Parse(args[2]); w = int.Parse(args[3]); h = int.Parse(args[4]); }
        foreach (var a in args)
        {
            if (a == "hosted") hosted = true;
            else if (a == "sw") RenderOptions.ProcessRenderMode = RenderMode.SoftwareOnly;
            else if (a == "defer") defer = true;
            else if (a == "partial") partial = true;
            else if (a.StartsWith("slow=")) slow = int.Parse(a.Substring(5));
            else if (a.StartsWith("swlater="))
            {
                var sw = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(int.Parse(a.Substring(8))) };
                sw.Tick += (s, e) => { sw.Stop(); RenderOptions.ProcessRenderMode = RenderMode.SoftwareOnly; Out("software rendering forced"); };
                sw.Start();
            }
        }
        timer = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(600) };
        timer.Tick += (s, e) => { timer.Stop(); Out("STATE " + state() + " depth " + depth + " outside " + late); late = 0; };
        var quit = new DispatcherTimer { Interval = TimeSpan.FromSeconds(secs) };
        Out("tier " + (RenderCapability.Tier >> 16) + " mode " + RenderOptions.ProcessRenderMode);

        if (!hosted)
        {
            var dock = new DockPanel { LastChildFill = true, UseLayoutRounding = true };
            var ribbon = Pane(Color.FromRgb(255, 0, 0), "ribbon"); ribbon.Height = RibbonH; DockPanel.SetDock(ribbon, Dock.Top);
            var status = Pane(Color.FromRgb(0, 0, 255), "status"); status.Height = StatusH; DockPanel.SetDock(status, Dock.Bottom);
            var browser = Pane(Color.FromRgb(0, 255, 0), "browser"); browser.Width = BrowserW; DockPanel.SetDock(browser, Dock.Left);
            var view = Pane(Color.FromRgb(255, 0, 255), "view");
            dock.Children.Add(ribbon); dock.Children.Add(status); dock.Children.Add(browser); dock.Children.Add(view);
            var win = new Window { Title = "r174_frame", Left = x, Top = y, Width = w, Height = h, Content = dock,
                                   WindowStartupLocation = WindowStartupLocation.Manual };
            win.SourceInitialized += (s, e) => {
                var src = (HwndSource)PresentationSource.FromVisual(win);
                src.AddHook(Hook);
                scale = src.CompositionTarget.TransformToDevice.M11;
                Out("hwnd " + src.Handle.ToString("x") + " scale " + scale + " render " + src.CompositionTarget.RenderMode);
            };
            state = () => {
                var hwnd = new WindowInteropHelper(win).Handle; RECT rc, wr;
                GetClientRect(hwnd, out rc); GetWindowRect(hwnd, out wr);
                bool ok = Math.Abs(ribbon.ActualWidth * scale - rc.r) < 1.5 && Math.Abs(dock.ActualHeight * scale - rc.b) < 1.5;
                return String.Format("{0} win {1} client {2}x{3} dock {4}x{5} ribbon {6}x{7} browser {8}x{9}", ok ? "ok" : "BAD", wr, rc.r, rc.b,
                                     dock.ActualWidth * scale, dock.ActualHeight * scale, ribbon.ActualWidth * scale, ribbon.ActualHeight * scale,
                                     browser.ActualWidth * scale, browser.ActualHeight * scale);
            };
            quit.Tick += (s, e) => win.Close();
            quit.Start();
            new Application().Run(win);
        }
        else
        {
            var form = new System.Windows.Forms.Form { Text = "r174_frame", StartPosition = System.Windows.Forms.FormStartPosition.Manual,
                                                       Left = x, Top = y, Width = w, Height = h, BackColor = System.Drawing.Color.Gray };
            var frame = new FrameHook(form.Handle);
            var colors = new[] { Color.FromRgb(255, 0, 0), Color.FromRgb(0, 255, 0), Color.FromRgb(0, 0, 255), Color.FromRgb(255, 0, 255) };
            var srcs = new HwndSource[4];
            for (int i = 0; i < 4; i++)
            {
                var p = new HwndSourceParameters("r174_pane" + i) { ParentWindow = form.Handle, WindowStyle = 0x52000000 /* WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN */ };
                p.SetPosition(0, 0); p.SetSize(10, 10);
                srcs[i] = new HwndSource(p) { RootVisual = Pane(colors[i], "") };
            }
            if (partial)
            {
                var pane = (Border)srcs[0].RootVisual;
                var square = new System.Windows.Shapes.Rectangle { Width = 60, Height = 60, Fill = Brushes.Black };
                var canvas = new Canvas();
                Canvas.SetLeft(square, 20); Canvas.SetTop(square, 20);
                canvas.Children.Add(square);
                pane.Child = canvas;
                var later = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(300) };
                later.Tick += (s, e) => { later.Stop(); square.Fill = pane.Background; Out("square = pane colour"); };
                pane.SizeChanged += (s, e) => { square.Fill = Brushes.Black; later.Stop(); later.Start(); };
                later.Start();
            }
            scale = srcs[0].CompositionTarget.TransformToDevice.M11;
            Out("hwnd " + form.Handle.ToString("x") + " scale " + scale + " render " + srcs[0].CompositionTarget.RenderMode);
            Action layout = () => {
                RECT rc;
                if (slow > 0) System.Threading.Thread.Sleep(slow);
                GetClientRect(form.Handle, out rc);
                int rh = (int)(RibbonH * scale), bw = (int)(BrowserW * scale), sh = (int)(StatusH * scale);
                Out("layout " + rc.r + "x" + rc.b + (depth > 0 ? " (in size-move)" : ""));
                MoveWindow(srcs[0].Handle, 0, 0, rc.r, rh, true);
                MoveWindow(srcs[1].Handle, 0, rh, bw, rc.b - rh - sh, true);
                MoveWindow(srcs[2].Handle, 0, rc.b - sh, rc.r, sh, true);
                MoveWindow(srcs[3].Handle, bw, rh, rc.r - bw, rc.b - rh - sh, true);
            };
            frame.Size += () => { if (!defer || depth == 0) layout(); };
            frame.Exit += () => { if (defer) layout(); };
            state = () => {
                RECT rc, wr, r0; GetClientRect(form.Handle, out rc); GetWindowRect(form.Handle, out wr); GetClientRect(srcs[0].Handle, out r0);
                var root = (FrameworkElement)srcs[0].RootVisual;
                bool ok = r0.r == rc.r && Math.Abs(root.ActualWidth * scale - rc.r) < 1.5;
                return String.Format("{0} win {1} client {2}x{3} ribbon hwnd {4}x{5} visual {6}x{7}", ok ? "ok" : "BAD", wr, rc.r, rc.b, r0.r, r0.b,
                                     root.ActualWidth * scale, root.ActualHeight * scale);
            };
            layout();
            quit.Tick += (s, e) => form.Close();
            quit.Start();
            System.Windows.Forms.Application.Run(form);
        }
        Out("summary enter " + enter + " exit " + leave);
        return enter != leave || depth != 0 ? 1 : 0;
    }

    class FrameHook : System.Windows.Forms.NativeWindow
    {
        public event Action Size, Exit;
        public FrameHook(IntPtr hwnd) { AssignHandle(hwnd); }
        protected override void WndProc(ref System.Windows.Forms.Message m)
        {
            Message(m.HWnd, m.Msg, m.LParam);
            base.WndProc(ref m);
            if (m.Msg == 0x5 && Size != null) Size();
            if (m.Msg == 0x232 && Exit != null) Exit();
        }
    }
}
