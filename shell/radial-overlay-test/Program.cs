using System.Diagnostics;
using System.Runtime.InteropServices;

namespace MacroShell;

/* The radial overlay without the engine. Placement and decoding always run.
   The live part needs a desktop that lets this process take the foreground:
   a stand-in game window comes to the front, then the overlay shows and hides
   over it. Without that desktop it prints SKIP and does not fail. */
static class RadialOverlayTest
{
    static int fails;

    static void Expect(bool cond, string name)
    {
        if (!cond)
        {
            Console.Error.WriteLine("fail " + name);
            fails++;
        }
    }

    static string FindRepo()
    {
        var dir = new DirectoryInfo(AppContext.BaseDirectory);
        while (dir != null)
        {
            if (File.Exists(Path.Combine(dir.FullName, "docs", "engine", "ipc-vectors.txt")))
                return dir.FullName;
            dir = dir.Parent;
        }
        throw new DirectoryNotFoundException("ipc-vectors.txt");
    }

    static byte[] Vector(string name)
    {
        var lines = File.ReadAllLines(Path.Combine(FindRepo(), "docs", "engine", "ipc-vectors.txt"));
        for (var i = 0; i + 1 < lines.Length; i++)
        {
            if (lines[i].Trim() == name + " engine")
                return lines[i + 1].Trim().Split(' ').Select(h => Convert.ToByte(h, 16)).ToArray();
        }
        throw new KeyNotFoundException(name);
    }

    static void Placement()
    {
        var hd = new PixelRect(0, 0, 1920, 1080);
        Expect(RadialPlacement.Place(hd, 96, "center", 0, 0, 340) == new PixelRect(790, 370, 1130, 710),
            "placement: middle of a 1080p monitor at 100 %");
        Expect(RadialPlacement.Place(hd, 144, "center", 0, 0, 340).Width == 510,
            "placement: 150 % scaling draws 340 px as 510");
        var right = new PixelRect(1920, 0, 1920 + 2560, 1440);
        Expect(RadialPlacement.Place(right, 144, "center", 0, 0, 340) == new PixelRect(1920 + 1280 - 255, 720 - 255, 1920 + 1280 + 255, 720 + 255),
            "placement: the game's monitor, not the primary, at its own scale");
        var left = new PixelRect(-1920, -200, 0, 880);
        Expect(RadialPlacement.Place(left, 96, "center", 0, 0, 340) == new PixelRect(-1130, 170, -790, 510),
            "placement: a monitor left of and above the primary");
        Expect(RadialPlacement.Place(hd, 96, "cursor", 5, 1075, 340) == new PixelRect(0, 740, 340, 1080),
            "placement: at the cursor near a corner stays on the monitor");
        Expect(RadialPlacement.Place(hd, 96, "cursor", 1000, 500, 340) == new PixelRect(830, 330, 1170, 670),
            "placement: at the cursor is centred on it");
        var portrait = new PixelRect(0, 0, 1080, 1920);
        Expect(RadialPlacement.Place(portrait, 96, "center", 0, 0, 600) == new PixelRect(240, 660, 840, 1260),
            "placement: a portrait monitor");
        var tiny = new PixelRect(0, 0, 400, 300);
        Expect(RadialPlacement.Place(tiny, 192, "cursor", 0, 0, 340) == new PixelRect(-140, -190, 540, 490),
            "placement: a ring bigger than the monitor is centred on it");
        Expect(RadialPlacement.Pixels(90, 96) == 200 && RadialPlacement.Pixels(900, 96) == 600,
            "placement: size is clamped to 200 to 600 at 100 %");
    }

    static void Decode()
    {
        Expect(EngineLink.TryTranslate(Vector("radial-open"), out var open) && open.Radial != null && open.Json == null,
            "decode: radial-open is a radial frame, not a page message");
        var v = open.Radial!;
        Expect(v.Open && v.Mode == "keys" && v.SlotCount == 4 && v.Lit == 1 && v.At == "center" && v.Size == 340 && v.Opacity == 88,
            "decode: radial-open fields");
        Expect(v.Seq == 7 && v.Stamp == 0x100000002 && v.Id == "rk" && v.Name == "Keys" && v.Prev == "q" && v.Next == "e",
            "decode: radial-open sequence, stamp and names");
        Expect(v.Labels.SequenceEqual(new[] { "Strat A", "Strat B", "", "Held C" }), "decode: radial-open labels");
        Expect(EngineLink.TryTranslate(Vector("radial-closed"), out var closed) && closed.Radial is { Open: false, Why: "no-slot", Seq: 8 },
            "decode: radial-closed with its reason");
        var truncated = Vector("radial-open")[..^3];
        truncated[8] = (byte)(truncated.Length - 12);
        Expect(!EngineLink.TryTranslate(truncated, out _), "decode: a cut-short label is a bad frame");

        var ahk = RadialView.FromJson("{\"v\":1,\"type\":\"radial\",\"open\":true,\"mode\":\"flick\",\"slots\":3,\"lit\":2,\"at\":\"cursor\",\"size\":900,\"opacity\":10,\"seq\":5,\"stamp\":123,\"id\":\"rf\",\"name\":\"Flick menu\",\"prev\":\"q\",\"next\":\"e\",\"labels\":[\"A\",\"\",\"C\"]}");
        Expect(ahk is { Open: true, Mode: "flick", SlotCount: 3, Lit: 2, At: "cursor", Seq: 5, Stamp: 123, Id: "rf" },
            "decode: the AHK radial message");
        Expect(ahk is { Size: 600, Opacity: 40 }, "decode: AHK size and opacity are clamped");
        Expect(RadialView.FromJson("{\"v\":1,\"type\":\"radial\",\"open\":false,\"why\":\"played\",\"seq\":6}") is { Open: false, Why: "played" },
            "decode: the AHK close");
        Expect(RadialView.FromJson("{\"type\":\"state\"}") == null && RadialView.FromJson("not json") == null,
            "decode: anything else is not a radial view");
    }

    static RadialView Menu(string id, uint seq, int lit = -1) => new()
    {
        Open = true,
        Mode = "keys",
        SlotCount = 4,
        Lit = lit,
        Size = 340,
        Opacity = 88,
        Seq = seq,
        Stamp = Stopwatch.GetTimestamp(),
        Id = id,
        Name = "Stratagems",
        Prev = "q",
        Next = "e",
        Labels = new[] { "Reinforce", "Resupply", "", "Orbital Laser" }
    };

    /* A stand-in game: a plain window this process brings to the front. */
    static System.Windows.Forms.Form? Game()
    {
        var form = new System.Windows.Forms.Form
        {
            Text = "stand-in game",
            StartPosition = System.Windows.Forms.FormStartPosition.Manual,
            Bounds = System.Windows.Forms.Screen.PrimaryScreen!.Bounds,
            FormBorderStyle = System.Windows.Forms.FormBorderStyle.None,
            ShowInTaskbar = false
        };
        form.Show();
        for (var i = 0; i < 20 && GetForegroundWindow() != form.Handle; i++)
        {
            form.Activate();
            SetForegroundWindow(form.Handle);
            System.Windows.Forms.Application.DoEvents();
            Thread.Sleep(50);
        }
        if (GetForegroundWindow() == form.Handle)
            return form;
        form.Close();
        return null;
    }

    static bool Wait(Func<bool> done, int ms)
    {
        var sw = Stopwatch.StartNew();
        while (!done() && sw.ElapsedMilliseconds < ms)
        {
            System.Windows.Forms.Application.DoEvents();
            Thread.Sleep(2);
        }
        return done();
    }

    static void Live()
    {
        var game = Game();
        if (game == null)
        {
            Console.WriteLine("SKIP live overlay: this desktop does not give a test window the foreground");
            return;
        }
        var logs = new List<string>();
        using var overlay = new RadialOverlay(line => { lock (logs) logs.Add(line); });
        overlay.Start();
        var shown = new List<RadialShown>();
        overlay.Shown += s => { lock (shown) shown.Add(s); };
        var hidden = new List<string>();
        overlay.Hidden += r => { lock (hidden) hidden.Add(r); };
        var frameToShown = new List<double>();
        var focusKept = true;
        var clickThrough = true;
        uint seq = 0;
        for (var i = 0; i < 50; i++)
        {
            var before = shown.Count;
            overlay.Post(Menu("rk", ++seq), Stopwatch.GetTimestamp());
            Expect(Wait(() => { lock (shown) return shown.Count > before; }, 1000), "live: open " + i + " shows the ring");
            lock (shown)
                if (shown.Count > before)
                    frameToShown.Add(shown[^1].Draw + shown[^1].Screen);
            focusKept &= GetForegroundWindow() == game.Handle;
            var r = overlay.Rect;
            var hit = WindowFromPoint(new POINT { X = r.Left + r.Width / 2, Y = r.Top + r.Height / 4 });
            clickThrough &= hit != overlay.Handle;
            overlay.Post(Menu("rk", ++seq, i % 4), Stopwatch.GetTimestamp());
            var hides = hidden.Count;
            overlay.Post(new RadialView { Open = false, Why = "played", Seq = ++seq }, Stopwatch.GetTimestamp());
            Expect(Wait(() => { lock (hidden) return hidden.Count > hides; }, 1000), "live: close " + i + " hides the ring");
        }
        Expect(focusKept, "live: the game keeps the foreground through 50 opens");
        Expect(overlay.StoleFocus == 0, "live: the overlay never became the foreground window");
        Expect(clickThrough, "live: a click on the ring reaches the game");
        frameToShown.Sort();
        var p95 = frameToShown.Count > 0 ? frameToShown[Math.Min(frameToShown.Count - 1, (int)Math.Ceiling(0.95 * frameToShown.Count) - 1)] : double.MaxValue;
        Console.WriteLine($"live: frame to shown p50 {frameToShown[frameToShown.Count / 2]:0.0} ms p95 {p95:0.0} ms over {frameToShown.Count}");
        Expect(p95 < 25, "live: frame to shown under 25 ms at p95");
        lock (logs)
            Expect(logs.Any(l => l.StartsWith("radial latency n=20 p50=", StringComparison.Ordinal)), "live: a latency summary every 20 opens");

        overlay.Post(Menu("rk", ++seq), Stopwatch.GetTimestamp());
        Expect(Wait(() => overlay.Visible, 1000), "live: open for the heartbeat test");
        var opened = Stopwatch.StartNew();
        Expect(Wait(() => !overlay.Visible, 2000), "live: no heartbeat hides the ring");
        Expect(opened.ElapsedMilliseconds >= RadialOverlay.BeatTimeoutMs - 50 && opened.ElapsedMilliseconds < RadialOverlay.BeatTimeoutMs + 300,
            $"live: the heartbeat hide comes at about 750 ms ({opened.ElapsedMilliseconds} ms)");
        lock (hidden)
            Expect(hidden.Count > 0 && hidden[^1] == "heartbeat", "live: the hide reason is heartbeat");

        overlay.Post(Menu("rk", ++seq), Stopwatch.GetTimestamp());
        for (var i = 0; i < 6; i++)
        {
            Thread.Sleep(200);
            overlay.Post(Menu("rk", ++seq), Stopwatch.GetTimestamp());
        }
        Expect(overlay.Visible, "live: a 200 ms heartbeat keeps the ring up for 1.2 s");
        overlay.Reset("engine-lost");
        Expect(Wait(() => !overlay.Visible, 200), "live: the engine going away hides the ring at once");
        overlay.Post(Menu("rk", 1), Stopwatch.GetTimestamp());
        Expect(Wait(() => overlay.Visible, 1000), "live: a new engine starting at seq 1 shows again after the reset");
        overlay.Post(Menu("rk", 0 + 1, 2) with { Seq = 0 }, Stopwatch.GetTimestamp());
        overlay.Reset("stop");
        Wait(() => !overlay.Visible, 200);
        game.Close();
    }

    /* --png dir writes the ring as the overlay draws it, for a look without a game. */
    static void Png(string dir)
    {
        Directory.CreateDirectory(dir);
        foreach (var (name, view, px) in new[]
        {
            ("keys", Menu("rk", 1, 1), 340),
            ("flick-150", Menu("rf", 1, 3) with { Mode = "flick", SlotCount = 8, Labels = new[] { "Reinforce", "Resupply", "", "Orbital Laser", "Eagle Airstrike", "Autocannon", "Shield Generator Relay", "SOS Beacon" } }, 510)
        })
        {
            using var bmp = new System.Drawing.Bitmap(px, px, System.Drawing.Imaging.PixelFormat.Format32bppArgb);
            using (var g = System.Drawing.Graphics.FromImage(bmp))
            {
                RadialArt.Paint(g, view, px);
            }
            using var shot = new System.Drawing.Bitmap(px, px);
            using (var g = System.Drawing.Graphics.FromImage(shot))
            {
                g.Clear(System.Drawing.Color.FromArgb(70, 90, 60));
                g.DrawImage(bmp, 0, 0);
            }
            shot.Save(Path.Combine(dir, name + ".png"));
        }
    }

    static int Main(string[] args)
    {
        if (args.Length == 2 && args[0] == "--png")
        {
            Png(args[1]);
            return 0;
        }
        Placement();
        Decode();
        Live();
        if (fails > 0)
        {
            Console.Error.WriteLine(fails + " failed");
            return 1;
        }
        Console.WriteLine("ok");
        return 0;
    }

    [StructLayout(LayoutKind.Sequential)]
    struct POINT
    {
        public int X;
        public int Y;
    }

    [DllImport("user32.dll")]
    static extern IntPtr GetForegroundWindow();

    [DllImport("user32.dll")]
    static extern bool SetForegroundWindow(IntPtr hwnd);

    [DllImport("user32.dll")]
    static extern IntPtr WindowFromPoint(POINT point);
}
