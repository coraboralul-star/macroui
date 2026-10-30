using System.Diagnostics;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Drawing.Imaging;
using System.Drawing.Text;
using System.Runtime.InteropServices;

namespace MacroShell;

/* One radial overlay timing, for the log and the tests. Milliseconds. Total
   and Pipe are NaN when the engine's stamp is missing. */
readonly record struct RadialShown(string Id, double Total, double Pipe, double Draw, double Screen, PixelRect Rect, uint Dpi);

/* The radial ring over the game (#97). Its own thread and window, so a busy
   page on the WPF thread cannot delay it, and per-monitor DPI aware, so the
   ring is sharp on every monitor while the rest of the shell is not.
   The window never activates: layered, click-through, no-activate, a tool
   window, topmost. It shows what the engine last sent and nothing else. It
   hides on the engine's close, a missed heartbeat (750 ms), Reset (the pipe
   dropped or the engine changed), and a display change. */
sealed class RadialOverlay : IDisposable
{
    public const int BeatTimeoutMs = 750;
    const int StatsEvery = 20;

    const uint WM_DESTROY = 0x0002;
    const uint WM_TIMER = 0x0113;
    const uint WM_DISPLAYCHANGE = 0x007E;
    const uint WM_NCHITTEST = 0x0084;
    const uint WM_MOUSEACTIVATE = 0x0021;
    const uint WM_APP_VIEW = 0x8001;
    const uint WM_APP_HIDE = 0x8002;
    const uint WM_APP_QUIT = 0x8003;

    static int s_classes;

    readonly Action<string> _log;
    readonly object _gate = new();
    readonly ManualResetEventSlim _ready = new();
    readonly string _className;
    Thread? _thread;
    IntPtr _hwnd;
    WndProc? _proc;

    RadialView? _pending;
    long _pendingAt;
    string _hideReason = "";

    /* Overlay thread only. */
    RadialView? _shown;
    long _beatAt;
    uint _lastSeq;
    PixelRect _rect;
    readonly List<double> _totals = new();

    public RadialOverlay(Action<string> log)
    {
        _log = log;
        _className = "MacroUIRadial" + Interlocked.Increment(ref s_classes);
    }

    /* Raised on the overlay thread. Tests wait on these. */
    public event Action<RadialShown>? Shown;
    public event Action<string>? Hidden;

    public int StoleFocus { get; private set; }
    public IntPtr Handle => _hwnd;
    public bool Visible => _hwnd != IntPtr.Zero && IsWindowVisible(_hwnd);
    public PixelRect Rect => _rect;

    public void Start()
    {
        if (_thread != null)
            return;
        _thread = new Thread(Run) { IsBackground = true, Name = "radial overlay" };
        _thread.Start();
        _ready.Wait(2000);
    }

    /* Any thread. received is Stopwatch.GetTimestamp() when the message arrived. */
    public void Post(RadialView view, long received)
    {
        lock (_gate)
        {
            _pending = view;
            _pendingAt = received;
        }
        if (_hwnd != IntPtr.Zero)
            PostMessageW(_hwnd, WM_APP_VIEW, IntPtr.Zero, IntPtr.Zero);
    }

    /* Any thread. Hides now and forgets the engine's sequence: a new engine starts at 1. */
    public void Reset(string reason)
    {
        lock (_gate)
        {
            _pending = null;
            _hideReason = reason;
        }
        if (_hwnd != IntPtr.Zero)
            PostMessageW(_hwnd, WM_APP_HIDE, IntPtr.Zero, IntPtr.Zero);
    }

    public void Dispose()
    {
        if (_hwnd != IntPtr.Zero)
            PostMessageW(_hwnd, WM_APP_QUIT, IntPtr.Zero, IntPtr.Zero);
        _thread?.Join(1000);
        _ready.Dispose();
    }

    void Run()
    {
        try
        {
            SetThreadDpiAwarenessContext(new IntPtr(-4));
            _proc = WindowProc;
            var module = GetModuleHandleW(null);
            var wc = new WNDCLASSEX
            {
                cbSize = (uint)Marshal.SizeOf<WNDCLASSEX>(),
                lpfnWndProc = Marshal.GetFunctionPointerForDelegate(_proc),
                hInstance = module,
                lpszClassName = _className
            };
            if (RegisterClassExW(ref wc) == 0)
            {
                _log("radial overlay class failed " + Marshal.GetLastWin32Error());
                return;
            }
            const uint exStyle = 0x00080000 /* LAYERED */ | 0x00000020 /* TRANSPARENT */ | 0x08000000 /* NOACTIVATE */
                                 | 0x00000080 /* TOOLWINDOW */ | 0x00000008 /* TOPMOST */;
            _hwnd = CreateWindowExW(exStyle, _className, "", 0x80000000 /* POPUP */, 0, 0, 1, 1,
                IntPtr.Zero, IntPtr.Zero, module, IntPtr.Zero);
            if (_hwnd == IntPtr.Zero)
            {
                _log("radial overlay window failed " + Marshal.GetLastWin32Error());
                return;
            }
            SetTimer(_hwnd, 1, 100, IntPtr.Zero);
        }
        finally
        {
            _ready.Set();
        }
        while (GetMessageW(out var msg, IntPtr.Zero, 0, 0) > 0)
        {
            TranslateMessage(ref msg);
            DispatchMessageW(ref msg);
        }
    }

    IntPtr WindowProc(IntPtr hwnd, uint msg, IntPtr wParam, IntPtr lParam)
    {
        try
        {
            switch (msg)
            {
                case WM_APP_VIEW:
                    OnView();
                    return IntPtr.Zero;
                case WM_APP_HIDE:
                {
                    string reason;
                    lock (_gate)
                        reason = _hideReason;
                    _lastSeq = 0;
                    HideNow(reason);
                    return IntPtr.Zero;
                }
                case WM_TIMER:
                    if (_shown != null && Ms(Stopwatch.GetTimestamp() - _beatAt) > BeatTimeoutMs)
                        HideNow("heartbeat");
                    return IntPtr.Zero;
                case WM_DISPLAYCHANGE:
                    HideNow("display");
                    break;
                case WM_NCHITTEST:
                    return new IntPtr(-1); /* HTTRANSPARENT */
                case WM_MOUSEACTIVATE:
                    return new IntPtr(3); /* MA_NOACTIVATE */
                case WM_APP_QUIT:
                    KillTimer(hwnd, 1);
                    DestroyWindow(hwnd);
                    return IntPtr.Zero;
                case WM_DESTROY:
                    _hwnd = IntPtr.Zero;
                    PostQuitMessage(0);
                    return IntPtr.Zero;
            }
        }
        catch (Exception ex)
        {
            _log("radial overlay " + ex.Message);
        }
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    static double Ms(long ticks) => ticks * 1000.0 / Stopwatch.Frequency;

    void OnView()
    {
        RadialView? view;
        long received;
        lock (_gate)
        {
            view = _pending;
            received = _pendingAt;
            _pending = null;
        }
        if (view == null)
            return;
        /* An older view that arrived late. A new engine resets through Reset. */
        if (view.Seq != 0 && _lastSeq != 0 && view.Seq <= _lastSeq)
            return;
        _lastSeq = view.Seq;
        _beatAt = Stopwatch.GetTimestamp();
        if (!view.Open)
        {
            HideNow("closed " + (view.Why == "" ? "none" : view.Why));
            return;
        }
        if (_shown == null || _shown.Id != view.Id)
        {
            ShowNew(view, received);
            return;
        }
        if (_shown.Lit != view.Lit || _shown.SlotCount != view.SlotCount || _shown.Size != view.Size
            || _shown.Opacity != view.Opacity || !_shown.Labels.SequenceEqual(view.Labels))
        {
            if (_shown.Size != view.Size)
                _rect = new PixelRect(_rect.Left, _rect.Top, _rect.Left + RadialPlacement.Pixels(view.Size, DpiAt(_rect)),
                    _rect.Top + RadialPlacement.Pixels(view.Size, DpiAt(_rect)));
            Draw(view, _rect);
        }
        _shown = view;
    }

    void ShowNew(RadialView view, long received)
    {
        var before = GetForegroundWindow();
        IntPtr monitor;
        GetCursorPos(out var cursor);
        if (view.At == "cursor")
            monitor = MonitorFromPoint(cursor, 2 /* NEAREST */);
        else
            monitor = before != IntPtr.Zero ? MonitorFromWindow(before, 2) : MonitorFromPoint(new POINT(), 1 /* PRIMARY */);
        var info = new MONITORINFO { cbSize = (uint)Marshal.SizeOf<MONITORINFO>() };
        if (!GetMonitorInfoW(monitor, ref info))
            info.rcMonitor = new RECT { Right = 1920, Bottom = 1080 };
        uint dpi = 96;
        if (GetDpiForMonitor(monitor, 0, out var dx, out _) == 0 && dx > 0)
            dpi = dx;
        var mon = new PixelRect(info.rcMonitor.Left, info.rcMonitor.Top, info.rcMonitor.Right, info.rcMonitor.Bottom);
        _rect = RadialPlacement.Place(mon, dpi, view.At, cursor.X, cursor.Y, view.Size);
        Draw(view, _rect);
        ShowWindow(_hwnd, 4 /* SW_SHOWNOACTIVATE */);
        SetWindowPos(_hwnd, new IntPtr(-1) /* TOPMOST */, 0, 0, 0, 0,
            0x0001 | 0x0002 | 0x0010 | 0x0040 /* NOSIZE NOMOVE NOACTIVATE SHOWWINDOW */);
        var drawn = Stopwatch.GetTimestamp();
        DwmFlush();
        var shown = Stopwatch.GetTimestamp();
        var after = GetForegroundWindow();
        if (after == _hwnd)
        {
            StoleFocus++;
            _log("radial overlay stole focus");
        }
        _shown = view;
        var stamped = view.Stamp > 0 && view.Stamp <= shown;
        var timing = new RadialShown(view.Id,
            stamped ? Ms(shown - view.Stamp) : double.NaN,
            stamped ? Ms(received - view.Stamp) : double.NaN,
            Ms(drawn - received), Ms(shown - drawn), _rect, dpi);
        _log(string.Format(System.Globalization.CultureInfo.InvariantCulture,
            "radial shown menu=\"{0}\" id={1} mode={2} at={3} px={4} dpi={5} latency={6} (pipe {7} draw {8:0.0} screen {9:0.0})",
            view.Name, view.Id, view.Mode, view.At, _rect.Width, dpi,
            stamped ? timing.Total.ToString("0.0", System.Globalization.CultureInfo.InvariantCulture) + "ms" : "unknown",
            stamped ? timing.Pipe.ToString("0.0", System.Globalization.CultureInfo.InvariantCulture) : "?",
            timing.Draw, timing.Screen));
        if (stamped)
            NoteTotal(timing.Total);
        Shown?.Invoke(timing);
    }

    void NoteTotal(double total)
    {
        _totals.Add(total);
        if (_totals.Count < StatsEvery)
            return;
        _totals.Sort();
        double At(double q) => _totals[Math.Min(_totals.Count - 1, (int)Math.Ceiling(q * _totals.Count) - 1)];
        _log(string.Format(System.Globalization.CultureInfo.InvariantCulture,
            "radial latency n={0} p50={1:0.0}ms p95={2:0.0}ms max={3:0.0}ms", _totals.Count, At(0.5), At(0.95), _totals[^1]));
        _totals.Clear();
    }

    void HideNow(string reason)
    {
        if (_shown == null)
            return;
        _shown = null;
        ShowWindow(_hwnd, 0 /* SW_HIDE */);
        _log("radial overlay hidden reason=" + reason);
        Hidden?.Invoke(reason);
    }

    static uint DpiAt(PixelRect rect)
    {
        var monitor = MonitorFromPoint(new POINT { X = rect.Left + rect.Width / 2, Y = rect.Top + rect.Height / 2 }, 2);
        return GetDpiForMonitor(monitor, 0, out var dx, out _) == 0 && dx > 0 ? dx : 96;
    }

    void Draw(RadialView view, PixelRect rect)
    {
        var px = Math.Max(1, rect.Width);
        using var bmp = new Bitmap(px, px, PixelFormat.Format32bppArgb);
        using (var g = Graphics.FromImage(bmp))
            RadialArt.Paint(g, view, px);
        var screen = GetDC(IntPtr.Zero);
        var mem = CreateCompatibleDC(screen);
        var hbmp = bmp.GetHbitmap(Color.FromArgb(0));
        var old = SelectObject(mem, hbmp);
        try
        {
            var pos = new POINT { X = rect.Left, Y = rect.Top };
            var size = new SIZE { cx = px, cy = px };
            var src = new POINT();
            var blend = new BLENDFUNCTION
            {
                BlendOp = 0,
                BlendFlags = 0,
                SourceConstantAlpha = (byte)Math.Round(Math.Clamp(view.Opacity, RadialView.OpacityMin, RadialView.OpacityMax) * 255 / 100.0),
                AlphaFormat = 1 /* AC_SRC_ALPHA */
            };
            UpdateLayeredWindow(_hwnd, screen, ref pos, ref size, mem, ref src, 0, ref blend, 2 /* ULW_ALPHA */);
        }
        finally
        {
            SelectObject(mem, old);
            DeleteObject(hbmp);
            DeleteDC(mem);
            ReleaseDC(IntPtr.Zero, screen);
        }
    }

    delegate IntPtr WndProc(IntPtr hwnd, uint msg, IntPtr wParam, IntPtr lParam);

    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    struct WNDCLASSEX
    {
        public uint cbSize;
        public uint style;
        public IntPtr lpfnWndProc;
        public int cbClsExtra;
        public int cbWndExtra;
        public IntPtr hInstance;
        public IntPtr hIcon;
        public IntPtr hCursor;
        public IntPtr hbrBackground;
        public string? lpszMenuName;
        public string lpszClassName;
        public IntPtr hIconSm;
    }

    [StructLayout(LayoutKind.Sequential)]
    struct MSG
    {
        public IntPtr hwnd;
        public uint message;
        public IntPtr wParam;
        public IntPtr lParam;
        public uint time;
        public POINT pt;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct POINT
    {
        public int X;
        public int Y;
    }

    [StructLayout(LayoutKind.Sequential)]
    struct SIZE
    {
        public int cx;
        public int cy;
    }

    [StructLayout(LayoutKind.Sequential)]
    struct RECT
    {
        public int Left;
        public int Top;
        public int Right;
        public int Bottom;
    }

    [StructLayout(LayoutKind.Sequential)]
    struct MONITORINFO
    {
        public uint cbSize;
        public RECT rcMonitor;
        public RECT rcWork;
        public uint dwFlags;
    }

    [StructLayout(LayoutKind.Sequential, Pack = 1)]
    struct BLENDFUNCTION
    {
        public byte BlendOp;
        public byte BlendFlags;
        public byte SourceConstantAlpha;
        public byte AlphaFormat;
    }

    [DllImport("user32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
    static extern ushort RegisterClassExW(ref WNDCLASSEX wc);

    [DllImport("user32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
    static extern IntPtr CreateWindowExW(uint exStyle, string className, string windowName, uint style, int x, int y,
        int width, int height, IntPtr parent, IntPtr menu, IntPtr instance, IntPtr param);

    [DllImport("user32.dll")]
    static extern IntPtr DefWindowProcW(IntPtr hwnd, uint msg, IntPtr wParam, IntPtr lParam);

    [DllImport("user32.dll")]
    static extern int GetMessageW(out MSG msg, IntPtr hwnd, uint min, uint max);

    [DllImport("user32.dll")]
    static extern bool TranslateMessage(ref MSG msg);

    [DllImport("user32.dll")]
    static extern IntPtr DispatchMessageW(ref MSG msg);

    [DllImport("user32.dll")]
    static extern bool PostMessageW(IntPtr hwnd, uint msg, IntPtr wParam, IntPtr lParam);

    [DllImport("user32.dll")]
    static extern void PostQuitMessage(int code);

    [DllImport("user32.dll")]
    static extern bool DestroyWindow(IntPtr hwnd);

    [DllImport("user32.dll")]
    static extern IntPtr SetTimer(IntPtr hwnd, IntPtr id, uint ms, IntPtr proc);

    [DllImport("user32.dll")]
    static extern bool KillTimer(IntPtr hwnd, IntPtr id);

    [DllImport("user32.dll")]
    static extern bool ShowWindow(IntPtr hwnd, int cmd);

    [DllImport("user32.dll")]
    static extern bool SetWindowPos(IntPtr hwnd, IntPtr after, int x, int y, int cx, int cy, uint flags);

    [DllImport("user32.dll")]
    static extern bool IsWindowVisible(IntPtr hwnd);

    [DllImport("user32.dll")]
    static extern IntPtr GetForegroundWindow();

    [DllImport("user32.dll")]
    static extern bool GetCursorPos(out POINT point);

    [DllImport("user32.dll")]
    static extern IntPtr MonitorFromPoint(POINT point, uint flags);

    [DllImport("user32.dll")]
    static extern IntPtr MonitorFromWindow(IntPtr hwnd, uint flags);

    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    static extern bool GetMonitorInfoW(IntPtr monitor, ref MONITORINFO info);

    [DllImport("user32.dll")]
    static extern IntPtr SetThreadDpiAwarenessContext(IntPtr context);

    [DllImport("user32.dll", SetLastError = true)]
    static extern bool UpdateLayeredWindow(IntPtr hwnd, IntPtr dst, ref POINT pos, ref SIZE size, IntPtr src,
        ref POINT srcPos, uint key, ref BLENDFUNCTION blend, uint flags);

    [DllImport("user32.dll")]
    static extern IntPtr GetDC(IntPtr hwnd);

    [DllImport("user32.dll")]
    static extern int ReleaseDC(IntPtr hwnd, IntPtr dc);

    [DllImport("gdi32.dll")]
    static extern IntPtr CreateCompatibleDC(IntPtr dc);

    [DllImport("gdi32.dll")]
    static extern IntPtr SelectObject(IntPtr dc, IntPtr obj);

    [DllImport("gdi32.dll")]
    static extern bool DeleteObject(IntPtr obj);

    [DllImport("gdi32.dll")]
    static extern bool DeleteDC(IntPtr dc);

    [DllImport("shcore.dll")]
    static extern int GetDpiForMonitor(IntPtr monitor, int type, out uint dpiX, out uint dpiY);

    [DllImport("dwmapi.dll")]
    static extern int DwmFlush();

    [DllImport("kernel32.dll", CharSet = CharSet.Unicode)]
    static extern IntPtr GetModuleHandleW(string? name);
}

/* The ring, like RadialPreview in the editor: numbered wedges clockwise from
   the top, the lit one in the accent, the menu name and the keys in the hub. */
static class RadialArt
{
    static readonly Color Accent = Color.FromArgb(255, 255, 107, 0);
    static readonly Color Set = Color.FromArgb(240, 31, 31, 31);
    static readonly Color Empty = Color.FromArgb(225, 22, 22, 22);
    static readonly Color Line = Color.FromArgb(255, 58, 58, 58);
    static readonly Color Hub = Color.FromArgb(245, 11, 11, 11);
    static readonly Color Text = Color.White;
    static readonly Color Muted = Color.FromArgb(255, 180, 180, 180);
    static readonly Color Ink = Color.FromArgb(255, 10, 10, 10);

    public static void Paint(Graphics g, RadialView view, int px)
    {
        g.Clear(Color.Transparent);
        g.SmoothingMode = SmoothingMode.AntiAlias;
        g.TextRenderingHint = TextRenderingHint.AntiAliasGridFit;
        var count = Math.Max(1, view.SlotCount);
        float c = px / 2f;
        float outer = c - Math.Max(1.5f, px / 170f);
        float inner = px * 0.17f;
        float mid = (outer + inner) / 2f;
        var sweep = 360f / count;
        using var line = new Pen(Line, Math.Max(1f, px / 170f));
        using var numFont = new Font("Segoe UI Semibold", Math.Max(8f, px * 0.068f), FontStyle.Regular, GraphicsUnit.Pixel);
        using var nameFont = new Font("Segoe UI", Math.Max(7f, px * 0.04f), FontStyle.Regular, GraphicsUnit.Pixel);
        using var center = new StringFormat { Alignment = StringAlignment.Center, LineAlignment = StringAlignment.Center };
        var width = (float)(2 * Math.PI * mid / count * 0.86);
        for (var i = 0; i < count; i++)
        {
            var label = i < view.Labels.Length ? view.Labels[i] : "";
            var lit = i == view.Lit;
            var start = i * sweep - sweep / 2f - 90f;
            using (var path = new GraphicsPath())
            {
                path.AddArc(c - outer, c - outer, outer * 2, outer * 2, start, sweep);
                path.AddArc(c - inner, c - inner, inner * 2, inner * 2, start + sweep, -sweep);
                path.CloseFigure();
                using var fill = new SolidBrush(lit ? Accent : label != "" ? Set : Empty);
                g.FillPath(fill, path);
                g.DrawPath(line, path);
            }
            var angle = (i * sweep - 90f) * Math.PI / 180.0;
            var x = c + (float)Math.Cos(angle) * mid;
            var y = c + (float)Math.Sin(angle) * mid;
            using var numBrush = new SolidBrush(lit ? Ink : Text);
            using var nameBrush = new SolidBrush(lit ? Ink : label != "" ? Text : Muted);
            g.DrawString((i + 1).ToString(), numFont, numBrush, new RectangleF(x - width / 2, y - numFont.Size * 1.05f, width, numFont.Size * 1.3f), center);
            var shownLabel = Fit(g, label == "" ? "Empty" : label, nameFont, width);
            g.DrawString(shownLabel, nameFont, nameBrush, new RectangleF(x - width / 2, y + numFont.Size * 0.2f, width, nameFont.Size * 1.5f), center);
        }
        using (var hub = new SolidBrush(Hub))
            g.FillEllipse(hub, c - inner, c - inner, inner * 2, inner * 2);
        g.DrawEllipse(line, c - inner, c - inner, inner * 2, inner * 2);
        using var hubFont = new Font("Segoe UI Semibold", Math.Max(7f, px * 0.036f), FontStyle.Regular, GraphicsUnit.Pixel);
        using var hintFont = new Font("Segoe UI", Math.Max(6f, px * 0.03f), FontStyle.Regular, GraphicsUnit.Pixel);
        using var textBrush = new SolidBrush(Text);
        using var mutedBrush = new SolidBrush(Muted);
        var hubWidth = inner * 1.7f;
        g.DrawString(Fit(g, view.Name, hubFont, hubWidth), hubFont, textBrush,
            new RectangleF(c - hubWidth / 2, c - hubFont.Size * 1.25f, hubWidth, hubFont.Size * 1.4f), center);
        var hint = view.Mode == "flick" ? "Flick" : $"1–{count} · {Key(view.Prev)} / {Key(view.Next)}";
        g.DrawString(Fit(g, hint, hintFont, hubWidth), hintFont, mutedBrush,
            new RectangleF(c - hubWidth / 2, c + hintFont.Size * 0.15f, hubWidth, hintFont.Size * 1.5f), center);
    }

    static string Key(string name) => name.Length == 1 ? name.ToUpperInvariant() : name;

    /* Cut with an ellipsis to fit width. */
    static string Fit(Graphics g, string text, Font font, float width)
    {
        if (text == "" || g.MeasureString(text, font).Width <= width)
            return text;
        for (var n = text.Length - 1; n > 0; n--)
        {
            var cut = text[..n].TrimEnd() + "…";
            if (g.MeasureString(cut, font).Width <= width)
                return cut;
        }
        return "…";
    }
}
