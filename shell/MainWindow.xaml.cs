using System.Diagnostics;
using System.IO;
using System.IO.Pipes;
using System.IO.Ports;
using System.Runtime.InteropServices;
using System.Text;
using System.Text.Json;
using System.Text.Json.Nodes;
using System.Windows;
using System.Windows.Interop;
using System.Windows.Threading;
using Microsoft.Web.WebView2.Core;

namespace MacroShell;

public partial class MainWindow : Window
{
    readonly CancellationTokenSource _cts = new();
    readonly SemaphoreSlim _write = new(1, 1);
    readonly bool _dev;
    readonly string _repo;
    readonly string _profilePath;
    readonly string _shellPath;
    NamedPipeClientStream? _pipe;
    Process? _engine;
    bool _pageReady;
    bool _closing;
    readonly bool _hosted;
    EventWaitHandle? _live;
    EventWaitHandle? _reveal;
    EventWaitHandle? _bootFailed;
    bool _engineLive;
    int _readyGate;
    string _lastState = "";
    string _portsJson = "";
    string _inputMode = "software";
    string _outputMode = "software";
    int _mouseBits;
    bool _swapClicks;
    SerialPort? _board;
    readonly SemaphoreSlim _boardLock = new(1, 1);
    DispatcherTimer? _portDebounce;
    DispatcherTimer? _quitTimer;
    EventHandler? _quitTick;
    readonly Queue<string> _hidPending = new();
    int _inputGen;
    string _closeMode = "tray";
    bool _allowClose;
    System.Windows.Forms.NotifyIcon? _tray;
    System.Drawing.Icon? _trayIcon;

    public MainWindow(bool hosted = false)
    {
        InitializeComponent();
        _hosted = hosted;
        if (_hosted)
        {
            WindowStartupLocation = WindowStartupLocation.Manual;
            Left = -20000;
            Top = -20000;
            ShowActivated = false;
        }
        _dev = Environment.GetCommandLineArgs().Contains("--dev");
        _repo = FindRepo();
        _profilePath = Path.Combine(_repo, "profiles", "default.json");
        _shellPath = Path.Combine(_repo, "profiles", "shell.json");
        Directory.CreateDirectory(Path.GetDirectoryName(_profilePath)!);
        LoadCloseMode();
        if (!File.Exists(_profilePath))
            File.WriteAllText(_profilePath, """{"version":1,"name":"Default","variables":{},"macros":[]}""", new UTF8Encoding(false));
    }

    async void OnLoaded(object sender, RoutedEventArgs e)
    {
        if (_hosted)
        {
            _live = new EventWaitHandle(false, EventResetMode.ManualReset, @"Local\Vendetta.Live");
            _reveal = new EventWaitHandle(false, EventResetMode.ManualReset, @"Local\Vendetta.Reveal");
            _bootFailed = new EventWaitHandle(false, EventResetMode.ManualReset, @"Local\Vendetta.BootFailed");
            PublishBoot();
        }
        TryLaunchEngine();
        try
        {
            var folder = Path.Combine(_repo, "shell", ".webview2");
            Directory.CreateDirectory(folder);
            var env = await CoreWebView2Environment.CreateAsync(null, folder);
            await View.EnsureCoreWebView2Async(env);
            var settings = View.CoreWebView2.Settings;
            settings.AreDefaultContextMenusEnabled = false;
            settings.AreDevToolsEnabled = _dev;
            settings.IsStatusBarEnabled = false;
            settings.IsZoomControlEnabled = false;
            try { View.DefaultBackgroundColor = System.Drawing.Color.FromArgb(255, 7, 7, 8); } catch { /* color type may be unavailable */ }
            var iconPath = Path.Combine(_repo, "shell", "vendetta.png");
            if (File.Exists(iconPath))
                Icon = System.Windows.Media.Imaging.BitmapFrame.Create(new Uri(iconPath));
            View.CoreWebView2.WebMessageReceived += OnPageMessage;
            if (_dev)
                View.CoreWebView2.Navigate("http://127.0.0.1:5173");
            else
            {
                var dist = Path.Combine(_repo, "ui", "dist");
                View.CoreWebView2.SetVirtualHostNameToFolderMapping(
                    "macroui.local", dist, CoreWebView2HostResourceAccessKind.Allow);
                View.CoreWebView2.Navigate("https://macroui.local/index.html");
            }
        }
        catch (Exception ex)
        {
            if (_hosted)
                PublishBootFailure(ex.Message.Length > 140 ? "The editor did not load." : ex.Message);
            else
                MessageBox.Show(this, ex.Message, "H&le");
            return;
        }

        _ = PumpPipe(_cts.Token);
        StartShowWait();
    }

    void LoadCloseMode()
    {
        try
        {
            if (!File.Exists(_shellPath)) return;
            var close = JsonNode.Parse(File.ReadAllText(_shellPath))?["close"]?.ToString();
            if (close == "tray" || close == "quit")
                _closeMode = close;
        }
        catch { /* keep the tray default */ }
    }

    void SaveCloseMode()
    {
        try
        {
            Directory.CreateDirectory(Path.GetDirectoryName(_shellPath)!);
            File.WriteAllText(_shellPath, "{\"close\":\"" + _closeMode + "\"}", new UTF8Encoding(false));
        }
        catch { /* the page still remembers the choice */ }
    }

    void StartShowWait()
    {
        var signal = App.ShowSignal;
        if (signal == null) return;
        Task.Run(() =>
        {
            while (!_cts.IsCancellationRequested)
            {
                bool signaled;
                try { signaled = signal.WaitOne(400); }
                catch { break; }
                if (!signaled || _cts.IsCancellationRequested) continue;
                Dispatcher.BeginInvoke(ShowFromTray);
            }
        });
    }

    void OnWindowClosing(object? sender, System.ComponentModel.CancelEventArgs e)
    {
        if (_allowClose || _closeMode != "tray")
            return;
        e.Cancel = true;
        try
        {
            HideToTray();
        }
        catch
        {
            _allowClose = true;
            Close();
        }
    }

    void HideToTray()
    {
        EnsureTray();
        _tray!.Visible = true;
        ShowInTaskbar = false;
        Hide();
    }

    void ShowFromTray()
    {
        if (_tray != null)
            _tray.Visible = false;
        ShowInTaskbar = true;
        // A second launch during the splash used to Show() at Left = -20000.
        if (!OnAnyScreen(Left, Top, Width, Height))
        {
            var area = SystemParameters.WorkArea;
            var width = Width > 0 ? Width : 1180;
            var height = Height > 0 ? Height : 760;
            Left = area.Left + Math.Max(0, (area.Width - width) / 2);
            Top = area.Top + Math.Max(0, (area.Height - height) / 2);
        }
        Show();
        if (WindowState == WindowState.Minimized)
            WindowState = WindowState.Normal;
        Activate();
    }

    static bool OnAnyScreen(double left, double top, double width, double height)
    {
        var rect = new System.Drawing.Rectangle((int)left, (int)top, Math.Max(1, (int)width), Math.Max(1, (int)height));
        foreach (var screen in System.Windows.Forms.Screen.AllScreens)
            if (screen.WorkingArea.IntersectsWith(rect))
                return true;
        return false;
    }

    void QuitFromTray()
    {
        // The page owns the debounced edit. Ask it to post the profile before
        // the process exits; the timer covers a page that never answers.
        if (!_pageReady || View.CoreWebView2 == null)
        {
            FinishQuit();
            return;
        }
        SendToPage("{\"type\":\"flush\"}");
        _quitTimer ??= new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(400) };
        _quitTick ??= FinishQuitOnTick;
        _quitTimer.Tick -= _quitTick;
        _quitTimer.Tick += _quitTick;
        _quitTimer.Start();
    }

    void FinishQuitOnTick(object? sender, EventArgs e) => FinishQuit();

    void FinishQuit()
    {
        if (_allowClose)
            return;
        if (_quitTimer != null)
            _quitTimer.Stop();
        _allowClose = true;
        Close();
    }

    void EnsureTray()
    {
        if (_tray != null) return;
        _trayIcon = LoadTrayIcon();
        var tray = new System.Windows.Forms.NotifyIcon
        {
            Icon = _trayIcon,
            Text = "H&le",
            Visible = false,
        };
        var menu = new System.Windows.Forms.ContextMenuStrip();
        menu.Items.Add("Show", null, (_, _) => Dispatcher.BeginInvoke(ShowFromTray));
        menu.Items.Add("Quit", null, (_, _) => Dispatcher.BeginInvoke(QuitFromTray));
        tray.ContextMenuStrip = menu;
        tray.DoubleClick += (_, _) => Dispatcher.BeginInvoke(ShowFromTray);
        _tray = tray;
    }

    System.Drawing.Icon LoadTrayIcon()
    {
        var path = Path.Combine(_repo, "shell", "vendetta.png");
        try
        {
            if (File.Exists(path))
            {
                using var bmp = new System.Drawing.Bitmap(path);
                var handle = bmp.GetHicon();
                try
                {
                    using var tmp = System.Drawing.Icon.FromHandle(handle);
                    return (System.Drawing.Icon)tmp.Clone();
                }
                finally
                {
                    DestroyIcon(handle);
                }
            }
        }
        catch { /* fall through to the default icon */ }
        return (System.Drawing.Icon)System.Drawing.SystemIcons.Application.Clone();
    }

    [DllImport("user32.dll", CharSet = CharSet.Auto)]
    static extern bool DestroyIcon(IntPtr handle);

    void OnClosed(object? sender, EventArgs e)
    {
        _closing = true;
        try
        {
            if (_tray != null)
            {
                _tray.Visible = false;
                _tray.Dispose();
                _tray = null;
            }
        }
        catch { /* icon already gone */ }
        try { _trayIcon?.Dispose(); } catch { /* icon already gone */ }
        try
        {
            if (_pipe is { IsConnected: true })
            {
                var quit = Encoding.UTF8.GetBytes("{\"v\":1,\"type\":\"command\",\"action\":\"quit\"}\n");
                _pipe.Write(quit, 0, quit.Length);
                _pipe.Flush();
            }
        }
        catch { /* the process kill below is the backup */ }
        try
        {
            if (_engine is { HasExited: false } && !_engine.WaitForExit(500))
                _engine.Kill(entireProcessTree: true);
        }
        catch { /* already gone */ }
        _cts.Cancel();
        try { _pipe?.Dispose(); } catch { /* closing */ }
        try
        {
            _boardLock.Wait(500);
            CloseBoard();
        }
        catch { CloseBoard(); }
        finally
        {
            try { _boardLock.Release(); } catch { /* not held */ }
        }
    }

    protected override void OnSourceInitialized(EventArgs e)
    {
        base.OnSourceInitialized(e);
        if (_hosted)
        {
            try { ShowWindow(new WindowInteropHelper(this).Handle, 0); }
            catch { /* the window stays off-screen if the hide call fails */ }
        }
        if (PresentationSource.FromVisual(this) is HwndSource source)
            source.AddHook(WndProc);
    }

    IntPtr WndProc(IntPtr hwnd, int msg, IntPtr wParam, IntPtr lParam, ref bool handled)
    {
        if (msg == 0x0219)
            QueuePortRefresh();
        return IntPtr.Zero;
    }

    [DllImport("user32.dll")]
    static extern bool ReleaseCapture();

    [DllImport("user32.dll")]
    static extern IntPtr SendMessage(IntPtr hWnd, int msg, IntPtr wParam, IntPtr lParam);

    void DragWindow()
    {
        try
        {
            var hwnd = new WindowInteropHelper(this).Handle;
            ReleaseCapture();
            SendMessage(hwnd, 0xA1, (IntPtr)2, IntPtr.Zero);
        }
        catch { /* mouse already up */ }
    }

    void OnPageMessage(object? sender, CoreWebView2WebMessageReceivedEventArgs e)
    {
        JsonNode? node;
        try { node = JsonNode.Parse(e.WebMessageAsJson); }
        catch { return; }
        var type = node?["type"]?.ToString();
        if (type == "flushed")
        {
            FinishQuit();
            return;
        }
        if (type == "window")
        {
            switch (node?["action"]?.ToString())
            {
                case "drag":
                    DragWindow();
                    break;
                case "minimize":
                    WindowState = WindowState.Minimized;
                    break;
                case "maximize":
                    WindowState = WindowState == WindowState.Maximized ? WindowState.Normal : WindowState.Maximized;
                    break;
                case "close":
                    Close();
                    break;
            }
            return;
        }
        if (type == "shell")
        {
            var close = node?["close"]?.ToString();
            if (close == "tray" || close == "quit")
            {
                _closeMode = close;
                SaveCloseMode();
            }
            return;
        }
        if (type == "hello")
        {
            _pageReady = true;
            SendToPage(LinkJson());
            SendReady();
            SendPorts(true);
            SendWindowState();
            if (_lastState.Length > 0)
                SendToPage(_lastState);
            MaybeReady();
            return;
        }
        if (type == "input")
        {
            _ = ApplyInput(node?["mode"]?.ToString() ?? "software");
            return;
        }
        if (type == "profile")
        {
            if (node?["profile"] is not JsonObject profile || profile["macros"] is not JsonArray)
            {
                SendToPage(ErrorJson("shell", "bad-shape", "profile payload was not a profile object"));
                return;
            }
            RememberSwap(profile);
            try
            {
                WriteProfileFile(profile.ToJsonString(new JsonSerializerOptions { WriteIndented = true }));
            }
            catch
            {
                SendToPage(ErrorJson("shell", "profile-write", "could not save the profile to disk"));
                return;
            }
            // The engine re-reads the file. Piping the whole profile parsed it on
            // the hook thread, and a long stall there makes Windows drop the hook.
            _ = WritePipe("{\"v\":1,\"type\":\"command\",\"action\":\"reload\"}");
            return;
        }
        if (type == "command")
        {
            var action = node?["action"]?.ToString();
            if (action == "reload")
            {
                _ = WritePipe(e.WebMessageAsJson);
                SendReady();
                SendPorts(true);
                return;
            }
            _ = WritePipe(e.WebMessageAsJson);
        }
    }

    async Task PumpPipe(CancellationToken ct)
    {
        var misses = 0;
        while (!ct.IsCancellationRequested)
        {
            try
            {
                var pipe = new NamedPipeClientStream(".", "MacroUI", PipeDirection.InOut, PipeOptions.Asynchronous);
                await pipe.ConnectAsync(1500, ct);
                pipe.ReadMode = PipeTransmissionMode.Message;
                _pipe = pipe;
                misses = 0;
                SendToPage(LinkJson());
                await SendProfileEnvelope();
                await WritePipe(OutputJson(_outputMode));
                var buf = new byte[1024 * 1024];
                while (!ct.IsCancellationRequested)
                {
                    var n = await pipe.ReadAsync(buf, ct);
                    if (n <= 0) break;
                    var text = Encoding.UTF8.GetString(buf, 0, n);
                    var kind = MessageType(text);
                    if (kind == "state")
                        _lastState = text;
                    if (kind == "hello" || kind == "state")
                        NoteEngine();
                    if (kind == "hid")
                    {
                        WriteHid(text);
                        continue;
                    }
                    SendToPage(text);
                }
            }
            catch (OperationCanceledException)
            {
                break;
            }
            catch
            {
                misses++;
                if (misses == 2)
                    TryLaunchEngine();
            }
            _pipe = null;
            _lastState = "";
            if (!_closing)
                SendToPage(LinkJson());
            try { await Task.Delay(700, ct); } catch { break; }
        }
    }

    async Task WritePipe(string json)
    {
        var pipe = _pipe;
        if (pipe is not { IsConnected: true })
            return;
        var bytes = Encoding.UTF8.GetBytes(json);
        await _write.WaitAsync();
        try
        {
            if (!ReferenceEquals(pipe, _pipe) || !pipe.IsConnected)
                return;
            await pipe.WriteAsync(bytes);
        }
        catch
        {
            /* the read loop reconnects */
        }
        finally
        {
            _write.Release();
        }
    }

    void TryLaunchEngine()
    {
        // Relaunch only when the running engine is gone. Starting a second
        // AutoHotkey hits #SingleInstance Force, which kills the resident one
        // and releases every held key.
        if (_engine is { HasExited: false })
            return;
        if (_engine != null)
        {
            try { _engine.Dispose(); } catch { /* already gone */ }
            _engine = null;
        }
        if (EngineMutexHeld())
        {
            _engine = FindEngineProcess();
            return;
        }
        var ahk = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ProgramFiles), "AutoHotkey", "v2", "AutoHotkey64.exe");
        var script = Path.Combine(_repo, "engine", "MacroEngine.ahk");
        if (!File.Exists(ahk) || !File.Exists(script))
        {
            if (_hosted)
                PublishBootFailure("The macro system did not start.");
            return;
        }
        var start = new ProcessStartInfo(ahk)
        {
            UseShellExecute = false,
            WorkingDirectory = _repo,
            CreateNoWindow = true,
            WindowStyle = ProcessWindowStyle.Hidden,
        };
        start.ArgumentList.Add(script);
        _engine = Process.Start(start);
    }

    static bool EngineMutexHeld()
    {
        try
        {
            using var mutex = Mutex.OpenExisting("Local\\MacroUI.Engine");
            return true;
        }
        catch (WaitHandleCannotBeOpenedException)
        {
            return false;
        }
        catch (UnauthorizedAccessException)
        {
            return true;
        }
    }

    static Process? FindEngineProcess()
    {
        Process? match = null;
        foreach (var process in Process.GetProcessesByName("AutoHotkey64"))
        {
            var cmd = "";
            try { cmd = CommandLine(process.Id); }
            catch { /* already exited */ }
            if (match == null
                && cmd.Contains("MacroEngine.ahk", StringComparison.OrdinalIgnoreCase)
                && !cmd.Contains("--self-test", StringComparison.OrdinalIgnoreCase))
                match = process;
            else
                process.Dispose();
        }
        return match;
    }

    static string CommandLine(int pid)
    {
        var handle = OpenProcess(0x1000, false, pid);
        if (handle == IntPtr.Zero)
            return "";
        try
        {
            NtQueryInformationProcess(handle, 60, IntPtr.Zero, 0, out var size);
            if (size <= 0)
                return "";
            var buf = Marshal.AllocHGlobal(size);
            try
            {
                if (NtQueryInformationProcess(handle, 60, buf, size, out size) != 0)
                    return "";
                var text = Marshal.PtrToStructure<UnicodeString>(buf);
                if (text.Buffer == IntPtr.Zero || text.Length == 0)
                    return "";
                return Marshal.PtrToStringUni(text.Buffer, text.Length / 2) ?? "";
            }
            finally { Marshal.FreeHGlobal(buf); }
        }
        finally { CloseHandle(handle); }
    }

    [StructLayout(LayoutKind.Sequential)]
    struct UnicodeString
    {
        public ushort Length;
        public ushort MaximumLength;
        public IntPtr Buffer;
    }

    [DllImport("kernel32.dll", SetLastError = true)]
    static extern IntPtr OpenProcess(uint access, bool inherit, int pid);

    [DllImport("kernel32.dll")]
    static extern bool CloseHandle(IntPtr handle);

    [DllImport("ntdll.dll")]
    static extern int NtQueryInformationProcess(IntPtr process, int cls, IntPtr info, int len, out int ret);

    void SendToPage(string json)
    {
        if (!_pageReady || View.CoreWebView2 == null)
            return;
        // Posted, not Invoked: the pipe reader must never wait on the UI thread,
        // or engine writes back up while the window is busy. The try keeps one
        // bad payload (invalid JSON, WebView2 already closed) from throwing into
        // PumpPipe, which used to tear the pipe down and reconnect in a loop.
        Dispatcher.BeginInvoke(() =>
        {
            try { View.CoreWebView2?.PostWebMessageAsJson(json); }
            catch { /* dropped; the next state message replaces it */ }
        });
    }

    void OnWindowStateChanged(object sender, EventArgs e) => SendWindowState();

    void SendWindowState()
    {
        SendToPage(JsonSerializer.Serialize(new { type = "chrome", maximized = WindowState == WindowState.Maximized }));
    }

    string LinkJson() =>
        JsonSerializer.Serialize(new { type = "link", shell = true, pipe = _pipe is { IsConnected: true } });

    // Write to a sibling file and swap it in. A crash or power loss during a
    // plain WriteAllText left a truncated default.json, which the engine could
    // not parse and which the page then replaced with the built-in profile.
    // The previous file is kept as default.json.bak so that is recoverable.
    void WriteProfileFile(string text)
    {
        var tmp = _profilePath + ".tmp";
        var bak = _profilePath + ".bak";
        File.WriteAllText(tmp, text, new UTF8Encoding(false));
        if (File.Exists(_profilePath))
            File.Replace(tmp, _profilePath, bak, ignoreMetadataErrors: true);
        else
            File.Move(tmp, _profilePath);
    }

    void SendReady()
    {
        string json;
        try
        {
            var profile = JsonNode.Parse(File.ReadAllText(_profilePath)) ?? new JsonObject();
            RememberSwap(profile);
            json = new JsonObject { ["type"] = "ready", ["profile"] = profile }.ToJsonString();
        }
        catch
        {
            json = ErrorJson("shell", "profile-read", "could not read the profile on disk");
        }
        SendToPage(json);
    }

    async Task SendProfileEnvelope()
    {
        string json;
        try
        {
            var profile = JsonNode.Parse(File.ReadAllText(_profilePath)) ?? new JsonObject();
            RememberSwap(profile);
            json = new JsonObject { ["v"] = 1, ["type"] = "profile", ["profile"] = profile }.ToJsonString();
        }
        catch
        {
            SendToPage(ErrorJson("shell", "profile-read", "could not read the profile on disk"));
            return;
        }
        await WritePipe(json);
    }

    void QueuePortRefresh()
    {
        Dispatcher.BeginInvoke(() =>
        {
            _portDebounce ??= new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(400) };
            _portDebounce.Stop();
            _portDebounce.Tick -= OnPortDebounce;
            _portDebounce.Tick += OnPortDebounce;
            _portDebounce.Start();
        });
    }

    void OnPortDebounce(object? sender, EventArgs e)
    {
        _portDebounce?.Stop();
        SendPorts(false);
        if (_inputMode is "rp2040" or "rp2350")
            _ = ApplyInput(_inputMode);
    }

    void SendPorts(bool force)
    {
        var devices = BoardPorts.List().Select(item => new { port = item.Port, chip = item.Chip, name = item.Name }).ToArray();
        var json = JsonSerializer.Serialize(new { type = "ports", devices });
        if (!force && json == _portsJson)
            return;
        _portsJson = json;
        SendToPage(json);
    }

    async Task ApplyInput(string mode)
    {
        if (mode != "rp2040" && mode != "rp2350")
            mode = "software";
        _inputMode = mode;
        var gen = Interlocked.Increment(ref _inputGen);
        await _boardLock.WaitAsync();
        try
        {
            if (gen != _inputGen)
                return;
            if (mode == "software")
            {
                CloseBoard();
                await SetOutput("software");
                return;
            }

            var hit = BoardPorts.List().FirstOrDefault(item => item.Chip == mode);
            if (string.IsNullOrEmpty(hit.Port))
            {
                CloseBoard();
                await SetOutput("software");
                SendToPage(ErrorJson("shell", "no-board", $"no H&le {ChipLabel(mode)} board was found"));
                return;
            }

            var fail = await Task.Run(() => OpenAndAsk(hit.Port, mode));
            if (gen != _inputGen)
                return;
            if (fail != "")
            {
                CloseBoard();
                await SetOutput("software");
                if (fail == "mismatch")
                    SendToPage(ErrorJson("shell", "board-mismatch", $"the board on {hit.Port} is not an H&le {ChipLabel(mode)}"));
                else
                    SendToPage(ErrorJson("shell", "no-board", $"could not open the H&le {ChipLabel(mode)} board"));
                return;
            }

            await SetOutput(mode);
        }
        finally
        {
            _boardLock.Release();
        }
    }

    async Task SetOutput(string mode)
    {
        _outputMode = mode;
        await WritePipe(OutputJson(mode));
    }

    string OpenAndAsk(string port, string mode)
    {
        try
        {
            if (_board is not { IsOpen: true } || !string.Equals(_board.PortName, port, StringComparison.OrdinalIgnoreCase))
            {
                CloseBoard();
                var serial = new SerialPort(port, 115200)
                {
                    NewLine = "\n",
                    Encoding = new UTF8Encoding(false),
                    ReadTimeout = 1500,
                    WriteTimeout = 1500,
                    DtrEnable = true,
                    RtsEnable = true,
                };
                serial.Open();
                _board = serial;
                Thread.Sleep(250);
            }
            _board.DiscardInBuffer();
            _board.WriteLine("?");
            var reply = (_board.ReadLine() ?? "").Trim();
            if (mode == "rp2040" && reply.StartsWith("Vendetta RP2040", StringComparison.OrdinalIgnoreCase))
                return "";
            if (mode == "rp2350" && reply.StartsWith("Vendetta RP2350", StringComparison.OrdinalIgnoreCase))
                return "";
            return "mismatch";
        }
        catch
        {
            return "open";
        }
    }

    void CloseBoard()
    {
        _mouseBits = 0;
        var serial = _board;
        _board = null;
        if (serial == null)
            return;
        try { serial.Close(); } catch { /* already gone */ }
        try { serial.Dispose(); } catch { /* already gone */ }
    }

    static string OutputJson(string mode) =>
        JsonSerializer.Serialize(new { v = 1, type = "output", mode });

    static string ChipLabel(string mode) => mode == "rp2040" ? "RP2040" : "RP2350";

    void WriteHid(string json)
    {
        JsonNode? node;
        try { node = JsonNode.Parse(json); }
        catch { return; }
        var down = ReadNames(node?["down"]);
        var up = ReadNames(node?["up"]);
        var dx = AsInt(node?["x"]);
        var dy = AsInt(node?["y"]);
        var abs = AsBool(node?["abs"]);
        var ax = AsInt(node?["ax"]);
        var ay = AsInt(node?["ay"]);
        // A dropped up leaves that key held on the board. Wait out the lock for
        // ups. A down that loses the race is queued and sent ahead of the next
        // report, so the board still sees down before up.
        var keyed = down.Count > 0 || up.Count > 0;
        if (!_boardLock.Wait(up.Count > 0 ? 1500 : 40))
        {
            if (keyed)
            {
                _hidPending.Enqueue(json);
                while (_hidPending.Count > 16)
                    _hidPending.Dequeue();
            }
            return;
        }
        try
        {
            while (_hidPending.Count > 0)
                WriteHidLocked(_hidPending.Dequeue());
            WriteHidLocked(down, up, dx, dy, abs, ax, ay);
        }
        finally
        {
            _boardLock.Release();
        }
    }

    void WriteHidLocked(string json)
    {
        JsonNode? node;
        try { node = JsonNode.Parse(json); }
        catch { return; }
        WriteHidLocked(ReadNames(node?["down"]), ReadNames(node?["up"]), AsInt(node?["x"]), AsInt(node?["y"]), AsBool(node?["abs"]), AsInt(node?["ax"]), AsInt(node?["ay"]));
    }

    void WriteHidLocked(List<string> down, List<string> up, int dx, int dy, bool abs, int ax, int ay)
    {
        try
        {
            if (_board is not { IsOpen: true })
                return;
            var serial = _board;
            serial.ReadTimeout = 80;
            var bits = _mouseBits;
            var mouseTouch = false;
            foreach (var raw in down)
            {
                var name = HidMap.SwapClick(raw, _swapClicks);
                if (HidMap.MouseBit(name, out var bit))
                {
                    bits |= 1 << bit;
                    mouseTouch = true;
                }
                else if (HidMap.KeyUsage(name, out var usage))
                    BoardLine(serial, $"K {usage} 1");
            }
            if (abs)
            {
                SteerMouse(serial, bits, ax, ay);
                mouseTouch = false;
                dx = 0;
                dy = 0;
            }
            else if (mouseTouch || dx != 0 || dy != 0)
            {
                BoardLine(serial, $"M {bits} {dx} {dy} 0");
                dx = 0;
                dy = 0;
                mouseTouch = false;
            }
            foreach (var raw in up)
            {
                var name = HidMap.SwapClick(raw, _swapClicks);
                if (HidMap.MouseBit(name, out var bit))
                {
                    bits &= ~(1 << bit);
                    mouseTouch = true;
                }
                else if (HidMap.KeyUsage(name, out var usage))
                    BoardLine(serial, $"K {usage} 0");
            }
            if (mouseTouch)
                BoardLine(serial, $"M {bits} 0 0 0");
            _mouseBits = bits;
        }
        catch
        {
            CloseBoard();
        }
    }

    static void BoardLine(SerialPort serial, string line)
    {
        serial.WriteLine(line);
        try { serial.ReadLine(); }
        catch { /* ok / hid-busy / timeout */ }
    }

    [StructLayout(LayoutKind.Sequential)]
    struct POINT
    {
        public int X;
        public int Y;
    }

    [DllImport("user32.dll")]
    static extern bool GetCursorPos(out POINT lpPoint);

    [DllImport("user32.dll")]
    static extern IntPtr GetForegroundWindow();

    [DllImport("user32.dll")]
    static extern bool LogicalToPhysicalPointForPerMonitorDPI(IntPtr hWnd, ref POINT lpPoint);

    [DllImport("user32.dll")]
    static extern uint GetDpiForWindow(IntPtr hwnd);

    static void SteerMouse(SerialPort serial, int bits, int ax, int ay)
    {
        var hwnd = GetForegroundWindow();
        var target = new POINT { X = ax, Y = ay };
        if (hwnd != IntPtr.Zero)
        {
            if (!LogicalToPhysicalPointForPerMonitorDPI(hwnd, ref target))
            {
                var dpi = GetDpiForWindow(hwnd);
                if (dpi == 0)
                    dpi = 96;
                target.X = (int)Math.Round(ax * dpi / 96.0);
                target.Y = (int)Math.Round(ay * dpi / 96.0);
            }
        }
        if (!GetCursorPos(out var cur))
            return;
        var leftX = target.X - cur.X;
        var leftY = target.Y - cur.Y;
        for (var i = 0; i < 64; i++)
        {
            if (leftX == 0 && leftY == 0)
                break;
            var sx = Math.Clamp(leftX, -127, 127);
            var sy = Math.Clamp(leftY, -127, 127);
            BoardLine(serial, $"M {bits} {sx} {sy} 0");
            if (!GetCursorPos(out var now))
                break;
            if (now.X != cur.X || now.Y != cur.Y)
            {
                leftX = target.X - now.X;
                leftY = target.Y - now.Y;
                cur = now;
            }
            else
            {
                leftX -= sx;
                leftY -= sy;
            }
        }
    }

    void RememberSwap(JsonNode? profile)
    {
        _swapClicks = false;
        if (profile?["swapClicks"] is not JsonValue v)
            return;
        if (v.TryGetValue(out bool b))
            _swapClicks = b;
        else if (v.TryGetValue(out int n))
            _swapClicks = n != 0;
        else if (v.TryGetValue(out string? s))
            _swapClicks = string.Equals(s, "true", StringComparison.OrdinalIgnoreCase) || s == "1";
    }

    static int AsInt(JsonNode? node)
    {
        if (node is JsonValue v)
        {
            if (v.TryGetValue(out int i))
                return i;
            if (v.TryGetValue(out long l))
                return (int)l;
            if (v.TryGetValue(out double d))
                return (int)d;
        }
        return 0;
    }

    static bool AsBool(JsonNode? node)
    {
        if (node is not JsonValue v)
            return false;
        if (v.TryGetValue(out bool b))
            return b;
        if (v.TryGetValue(out int n))
            return n != 0;
        return false;
    }

    static List<string> ReadNames(JsonNode? node)
    {
        var names = new List<string>();
        if (node is not JsonArray list)
            return names;
        foreach (var item in list)
        {
            if (item is not JsonValue v || !v.TryGetValue(out string? name) || string.IsNullOrEmpty(name))
                continue;
            names.Add(name);
        }
        return names;
    }

    static string ErrorJson(string where, string code, string detail) =>
        JsonSerializer.Serialize(new { v = 1, type = "error", where, code, detail });

    static string MessageType(string json)
    {
        try { return JsonNode.Parse(json)?["type"]?.ToString() ?? ""; }
        catch { return ""; }
    }

    void NoteEngine()
    {
        if (Dispatcher.CheckAccess())
            MarkEngine();
        else
            Dispatcher.BeginInvoke(MarkEngine);
    }

    void MarkEngine()
    {
        _engineLive = true;
        MaybeReady();
    }

    void MaybeReady()
    {
        if (!_hosted)
            return;
        if (!_pageReady || !_engineLive)
        {
            PublishBoot();
            return;
        }
        if (Interlocked.Exchange(ref _readyGate, 1) != 0)
            return;
        Task.Run(() =>
        {
            try { _live?.Set(); } catch { /* the launcher is already gone */ }
            try { _reveal?.WaitOne(4000); } catch { /* show the window anyway */ }
            Dispatcher.BeginInvoke(Reveal);
        });
    }

    void PublishBoot()
    {
        if (!_hosted)
            return;
        WriteBoot(!_pageReady ? "LOADING THE EDITOR" : "STARTING THE MACRO SYSTEM");
    }

    void PublishBootFailure(string text)
    {
        WriteBoot(text);
        try { _bootFailed?.Set(); } catch { /* the launcher is already gone */ }
    }

    static void WriteBoot(string text)
    {
        try { File.WriteAllText(Path.Combine(Path.GetTempPath(), "Vendetta.boot"), text); }
        catch { /* the launcher keeps its last line */ }
    }

    void Reveal()
    {
        var area = SystemParameters.WorkArea;
        var width = Width > 0 ? Width : 1180;
        var height = Height > 0 ? Height : 760;
        Left = area.Left + Math.Max(0, (area.Width - width) / 2);
        Top = area.Top + Math.Max(0, (area.Height - height) / 2);
        ShowInTaskbar = true;
        Show();
        try { ShowWindow(new WindowInteropHelper(this).Handle, 5); }
        catch { /* Show already asked for the window */ }
        Activate();
    }

    [DllImport("user32.dll")]
    static extern bool ShowWindow(IntPtr hWnd, int nCmdShow);

    static string FindRepo()
    {
        var dir = new DirectoryInfo(AppContext.BaseDirectory);
        while (dir != null)
        {
            if (File.Exists(Path.Combine(dir.FullName, "engine", "MacroEngine.ahk")))
                return dir.FullName;
            dir = dir.Parent;
        }
        return Directory.GetCurrentDirectory();
    }
}
