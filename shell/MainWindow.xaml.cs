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
    /* The radial ring over the game (#97). Its own thread; see RadialOverlay. */
    readonly RadialOverlay _radial;
    readonly object _logGate = new();
    bool _pageReady;
    bool _closing;
    readonly bool _hosted;
    EventWaitHandle? _live;
    EventWaitHandle? _reveal;
    EventWaitHandle? _bootFailed;
    bool _engineLive;
    int _readyGate;
    string _lastState = "";
    string _pendingError = "";
    string _portsJson = "";
    string _inputMode = "software";
    string _outputMode = "software";
    string _outputRequested = "software";
    string _outputPageJson = "{\"type\":\"output\",\"mode\":\"software\",\"requested\":\"software\"}";
    int _mouseBits;
    bool _swapClicks;
    SerialPort? _board;
    readonly SemaphoreSlim _boardLock = new(1, 1);
    DispatcherTimer? _portDebounce;
    DispatcherTimer? _quitTimer;
    EventHandler? _quitTick;
    readonly Queue<string> _hidPending = new();
    readonly Queue<byte[]> _reportPending = new();
    int _inputGen;
    string _closeMode = "tray";
    // The engine that runs. Native is the default (#54 step 1).
    string _engineChoice = "native";
    // The engine shell.json names, or null until the user picks one. Only this
    // is written, so the default and a fallback for one session never stick.
    string? _savedEngine;
    // Native failed twice with no AutoHotkey to fall back to. Cleared when the
    // user picks an engine.
    volatile bool _nativeGaveUp;
    string _wire = "";
    int _nativeStrikes;
    int _expectExit;
    int _suppressLaunch;
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
        _radial = new RadialOverlay(Log);
        _radial.Start();
        _profilePath = Path.Combine(_repo, "profiles", "default.json");
        _shellPath = Path.Combine(_repo, "profiles", "shell.json");
        Directory.CreateDirectory(Path.GetDirectoryName(_profilePath)!);
        LoadShellSettings();
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

    void LoadShellSettings()
    {
        try
        {
            if (!File.Exists(_shellPath)) return;
            var node = JsonNode.Parse(File.ReadAllText(_shellPath));
            var close = node?["close"]?.ToString();
            if (close == "tray" || close == "quit")
                _closeMode = close;
            var engine = node?["engine"]?.ToString();
            if (engine == "ahk" || engine == "native")
            {
                _engineChoice = engine;
                _savedEngine = engine;
            }
        }
        catch (Exception ex)
        {
            Log("shell settings " + ex.Message);
        }
        finally
        {
            if (_savedEngine == null && NativeExe() == null && AhkInstalled())
            {
                _engineChoice = "ahk";
                Log("the native engine exe was not found, starting AutoHotkey without saving it");
            }
        }
    }

    void SaveShellSettings()
    {
        try
        {
            Directory.CreateDirectory(Path.GetDirectoryName(_shellPath)!);
            var engine = _savedEngine != null ? ",\"engine\":\"" + _savedEngine + "\"" : "";
            File.WriteAllText(_shellPath, "{\"close\":\"" + _closeMode + "\"" + engine + "}", new UTF8Encoding(false));
        }
        catch (Exception ex)
        {
            Log("shell settings save " + ex.Message);
        }
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
        try { _radial.Dispose(); } catch (Exception ex) { Log("radial overlay close " + ex.Message); }
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
                var quit = QuitBytes();
                _pipe.Write(quit, 0, quit.Length);
                _pipe.Flush();
            }
        }
        catch (Exception ex)
        {
            Log("engine quit " + ex.Message);
        }
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
        else if (msg == 0x00FF)
            OnTurnRawInput(lParam);
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
        if (type == "turn")
        {
            OnTurnMessage(node);
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
                SaveShellSettings();
            }
            var engine = node?["engine"]?.ToString();
            if (engine == "ahk" || engine == "native")
                RequestEngine(engine);
            return;
        }
        if (type == "hello")
        {
            _pageReady = true;
            SendToPage(LinkJson());
            SendReady();
            SendPorts(true);
            SendToPage(_outputPageJson);
            SendWindowState();
            if (_pendingError.Length > 0)
                SendToPage(_pendingError);
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
            string json;
            try
            {
                json = profile.ToJsonString(new JsonSerializerOptions { WriteIndented = true });
                WriteProfileFile(json);
            }
            catch
            {
                SendToPage(ErrorJson("shell", "profile-write", "could not save the profile to disk"));
                return;
            }
            // AutoHotkey re-reads the file. The document itself is not piped
            // there: parsing it on the hook thread makes Windows drop the hook.
            // Native parses on the IPC thread, then playback adopts that image
            // on the next tick. A reload would race the file replace.
            if (_wire == "native")
                _ = WriteNativeSavedProfile(json);
            else
                _ = WriteCommand("reload");
            return;
        }
        if (type == "command")
        {
            var action = node?["action"]?.ToString() ?? "";
            if (action == "reload")
            {
                _ = WriteCommand(action, e.WebMessageAsJson);
                SendReady();
                SendPorts(true);
                return;
            }
            _ = WriteCommand(action, e.WebMessageAsJson);
        }
    }

    async Task PumpPipe(CancellationToken ct)
    {
        var misses = 0;
        var buf = new byte[1024 * 1024];
        while (!ct.IsCancellationRequested)
        {
            NamedPipeClientStream? pipe = null;
            try
            {
                pipe = new NamedPipeClientStream(".", "MacroUI", PipeDirection.InOut, PipeOptions.Asynchronous);
                await pipe.ConnectAsync(1500, ct).ConfigureAwait(false);
                pipe.ReadMode = PipeTransmissionMode.Message;
                var first = await ReadPipe(pipe, buf, ct, 2000).ConfigureAwait(false);
                if (first <= 0)
                    throw new IOException("engine hello did not arrive");
                if (!pipe.IsMessageComplete)
                    throw new IOException("engine hello was truncated");
                var peer = EngineLink.Classify(buf.AsSpan(0, first));
                if (peer == EnginePeer.Reject)
                {
                    Log("engine hello was not a known protocol");
                    PostError(ErrorJson("shell", "bad-engine", "The engine spoke a protocol this app does not know."));
                    throw new IOException("engine hello was rejected");
                }
                _pipe = pipe;
                _wire = peer == EnginePeer.Native ? "native" : "ahk";
                if (_wire == "native")
                    _nativeStrikes = 0;
                misses = 0;
                SendToPage(LinkJson());
                // An engine left running from before, such as a resident AHK
                // after the default became native. The quit goes over this
                // pipe, so it releases keys before the chosen engine starts.
                var switching = _wire != _engineChoice && !_nativeGaveUp
                    && Volatile.Read(ref _suppressLaunch) == 0 && Volatile.Read(ref _expectExit) == 0;
                if (switching)
                {
                    Log("engine on the pipe is " + _wire + ", switching to " + _engineChoice);
                    _ = Task.Run(RestartEngine);
                }
                AcceptMessage(buf.AsSpan(0, first));
                if (switching)
                {
                    /* that engine is quitting; the next one gets the profile */
                }
                else if (peer == EnginePeer.Native)
                    await SendNativeProfile().ConfigureAwait(false);
                else
                {
                    await SendProfileEnvelope().ConfigureAwait(false);
                    await WritePipe(OutputJson(_outputMode)).ConfigureAwait(false);
                }
                while (!ct.IsCancellationRequested)
                {
                    var n = await ReadPipe(pipe, buf, ct, 0).ConfigureAwait(false);
                    if (n <= 0)
                        break;
                    if (!pipe.IsMessageComplete)
                        throw new IOException("engine frame was truncated");
                    AcceptMessage(buf.AsSpan(0, n));
                }
            }
            catch (OperationCanceledException) when (ct.IsCancellationRequested)
            {
                break;
            }
            catch (Exception ex)
            {
                Log("engine pipe " + ex.Message);
                if (Volatile.Read(ref _suppressLaunch) != 0 || Volatile.Read(ref _expectExit) != 0)
                {
                    /* a switch owns the next launch */
                }
                else if (_engineChoice == "native" && ex.Message.StartsWith("engine hello", StringComparison.Ordinal))
                    NoteNativeFailure(ex.Message);
                else
                {
                    misses++;
                    if (misses == 2)
                        TryLaunchEngine();
                }
            }
            finally
            {
                if (!ReferenceEquals(pipe, _pipe))
                {
                    try { pipe?.Dispose(); } catch (Exception ex) { Log("engine pipe close " + ex.Message); }
                }
            }
            var dead = _pipe;
            _pipe = null;
            // A crashed or switched engine cannot close its menu. Hide the ring now.
            _radial.Reset("engine-lost");
            _wire = "";
            _lastState = "";
            try { dead?.Dispose(); }
            catch (Exception ex) { Log("engine pipe close " + ex.Message); }
            if (!_closing)
                SendToPage(LinkJson());
            try { await Task.Delay(700, ct).ConfigureAwait(false); } catch { break; }
        }
    }

    static async Task<int> ReadPipe(NamedPipeClientStream pipe, byte[] buf, CancellationToken ct, int timeoutMs)
    {
        if (timeoutMs <= 0)
            return await pipe.ReadAsync(buf, ct).ConfigureAwait(false);
        using var linked = CancellationTokenSource.CreateLinkedTokenSource(ct);
        linked.CancelAfter(timeoutMs);
        try
        {
            return await pipe.ReadAsync(buf, linked.Token).ConfigureAwait(false);
        }
        catch (OperationCanceledException) when (!ct.IsCancellationRequested)
        {
            throw new IOException("engine hello timed out");
        }
    }

    void AcceptMessage(ReadOnlySpan<byte> message)
    {
        var received = System.Diagnostics.Stopwatch.GetTimestamp();
        if (_wire != "native")
        {
            var text = Encoding.UTF8.GetString(message);
            var kind = MessageType(text);
            if (kind == "radial")
            {
                var view = RadialView.FromJson(text);
                if (view != null)
                    _radial.Post(view, received);
                return;
            }
            if (kind == "state")
                _lastState = text;
            if (kind == "hello" || kind == "state")
                NoteEngine();
            if (kind == "hid")
            {
                WriteHid(text);
                return;
            }
            SendToPage(text);
            return;
        }
        if (!EngineLink.TryTranslate(message, out var inbound))
            throw new IOException("bad engine frame");
        if (inbound.Live)
            NoteEngine();
        if (inbound.Report != null)
            WriteReport(inbound.Report);
        if (inbound.Radial != null)
        {
            _radial.Post(inbound.Radial, received);
            return;
        }
        if (inbound.Json == null)
            return;
        if (inbound.State)
            _lastState = inbound.Json;
        SendToPage(inbound.Json);
    }

    async Task WriteNativeSavedProfile(string json)
    {
        var bytes = new UTF8Encoding(false).GetBytes(json);
        try
        {
            await WritePipeBytes(EngineLink.Profile(bytes)).ConfigureAwait(false);
        }
        catch (Exception ex)
        {
            Log("native profile " + ex.Message);
            await WritePipeBytes(EngineLink.Command("reload")).ConfigureAwait(false);
        }
    }

    async Task WriteCommand(string action, string? ahkJson = null)
    {
        if (_wire == "native")
            await WritePipeBytes(EngineLink.Command(action)).ConfigureAwait(false);
        else
            await WritePipe(ahkJson ?? "{\"v\":1,\"type\":\"command\",\"action\":\"" + action + "\"}").ConfigureAwait(false);
    }

    async Task WritePipe(string json)
    {
        await WritePipeBytes(Encoding.UTF8.GetBytes(json)).ConfigureAwait(false);
    }

    async Task WritePipeBytes(byte[] bytes)
    {
        var pipe = _pipe;
        if (pipe is not { IsConnected: true })
            return;
        await _write.WaitAsync();
        try
        {
            if (!ReferenceEquals(pipe, _pipe) || !pipe.IsConnected)
                return;
            await pipe.WriteAsync(bytes);
        }
        catch (Exception ex)
        {
            Log("pipe write " + ex.Message);
        }
        finally
        {
            _write.Release();
        }
    }

    void RequestEngine(string want)
    {
        if (want != "ahk" && want != "native")
            return;
        if (want == _engineChoice && !_nativeGaveUp)
            return;
        _engineChoice = want;
        _savedEngine = want;
        _nativeStrikes = 0;
        _nativeGaveUp = false;
        SaveShellSettings();
        SendToPage(LinkJson());
        _ = Task.Run(RestartEngine);
    }

    void RestartEngine()
    {
        Interlocked.Exchange(ref _suppressLaunch, 1);
        try
        {
            QuitEngine();
            LaunchEngine();
        }
        catch (Exception ex)
        {
            Log("engine switch " + ex.Message);
        }
        finally
        {
            Interlocked.Exchange(ref _suppressLaunch, 0);
        }
        SendToPage(LinkJson());
    }

    void QuitEngine()
    {
        // Release keys, quit, then wait until the mutex is free. The engine
        // releases on the way out. Kill is only if that exit does not finish.
        Interlocked.Exchange(ref _expectExit, 1);
        try
        {
            WritePipeSync(QuitBytes());
            var proc = _engine;
            if (proc != null)
            {
                try
                {
                    if (!proc.HasExited && !proc.WaitForExit(2000))
                    {
                        Log("engine did not exit after quit");
                        proc.Kill(entireProcessTree: true);
                        proc.WaitForExit(1000);
                    }
                }
                catch (Exception ex)
                {
                    Log("engine quit " + ex.Message);
                }
            }
            for (var i = 0; i < 40 && EngineMutexHeld(); i++)
                Thread.Sleep(50);
            if (EngineMutexHeld())
                Log("engine mutex still held after quit");
            ForgetEngine();
        }
        finally
        {
            Interlocked.Exchange(ref _expectExit, 0);
        }
    }

    byte[] QuitBytes()
    {
        if (_wire == "native" || NativeProcess(_engine))
            return EngineLink.Command("quit");
        return Encoding.UTF8.GetBytes("{\"v\":1,\"type\":\"command\",\"action\":\"quit\"}\n");
    }

    void WritePipeSync(byte[] bytes)
    {
        var pipe = _pipe;
        if (pipe is not { IsConnected: true })
            return;
        _write.Wait();
        try
        {
            if (!ReferenceEquals(pipe, _pipe) || !pipe.IsConnected)
                return;
            pipe.Write(bytes, 0, bytes.Length);
            pipe.Flush();
        }
        catch (Exception ex)
        {
            Log("pipe write " + ex.Message);
        }
        finally
        {
            _write.Release();
        }
    }

    void NoteNativeFailure(string why)
    {
        _nativeStrikes++;
        Log(why + " (" + _nativeStrikes + ")");
        if (_nativeStrikes >= 2 && !AhkInstalled())
        {
            // Nothing to fall back to. Stop retrying until the user picks an
            // engine or restarts.
            _nativeStrikes = 0;
            _nativeGaveUp = true;
            PostError(ErrorJson("shell", "engine-failed", "The native engine failed twice. AutoHotkey is not installed, so nothing is running."));
            SendToPage(LinkJson());
            return;
        }
        if (_nativeStrikes >= 2)
        {
            // For this session only. The next start tries native again.
            _engineChoice = "ahk";
            _nativeStrikes = 0;
            PostError(ErrorJson("shell", "engine-fallback", "The native engine failed twice. AutoHotkey is running until H&le is restarted."));
            SendToPage(LinkJson());
            Interlocked.Exchange(ref _suppressLaunch, 1);
            try
            {
                QuitEngine();
                LaunchEngine();
            }
            catch (Exception ex)
            {
                Log("engine fallback " + ex.Message);
            }
            finally
            {
                Interlocked.Exchange(ref _suppressLaunch, 0);
            }
        }
        else
            LaunchEngine();
        SendToPage(LinkJson());
    }

    void TryLaunchEngine()
    {
        if (Volatile.Read(ref _suppressLaunch) != 0)
            return;
        LaunchEngine();
    }

    void LaunchEngine()
    {
        // One engine. A second process would share the mutex and exit, or
        // AutoHotkey's #SingleInstance Force would kill the resident one.
        if (_engine is { HasExited: false })
            return;
        if (_nativeGaveUp && _engineChoice == "native")
            return;
        if (_engine != null)
        {
            var unexpected = Volatile.Read(ref _expectExit) == 0 && _engineChoice == "native";
            ForgetEngine();
            if (unexpected)
            {
                NoteNativeFailure("native engine exited");
                return;
            }
        }
        if (EngineMutexHeld())
        {
            _engine = FindEngineProcess(_engineChoice);
            if (_engine == null)
                Log("engine mutex is held by a different process");
            return;
        }
        if (_engineChoice == "native")
            StartNative();
        else
            StartAhk();
    }

    void StartNative()
    {
        var exe = NativeExe();
        if (exe == null)
        {
            Log("native engine exe was not found");
            NoteNativeFailure("native engine did not start");
            return;
        }
        try
        {
            var start = HiddenStart(exe);
            start.ArgumentList.Add("--profile");
            start.ArgumentList.Add(_profilePath);
            _engine = Process.Start(start);
        }
        catch (Exception ex)
        {
            Log("native engine start " + ex.Message);
            NoteNativeFailure("native engine did not start");
            return;
        }
        if (_engine == null)
        {
            NoteNativeFailure("native engine did not start");
            return;
        }
        try
        {
            if (_engine.WaitForExit(300))
            {
                var code = _engine.ExitCode;
                ForgetEngine();
                NoteNativeFailure("native engine exited " + code);
            }
        }
        catch (Exception ex)
        {
            Log("native engine start " + ex.Message);
            ForgetEngine();
            NoteNativeFailure("native engine did not start");
        }
    }

    static string AhkPath() =>
        Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ProgramFiles), "AutoHotkey", "v2", "AutoHotkey64.exe");

    bool AhkInstalled() =>
        File.Exists(AhkPath()) && File.Exists(Path.Combine(_repo, "engine", "MacroEngine.ahk"));

    void StartAhk()
    {
        var ahk = AhkPath();
        var script = Path.Combine(_repo, "engine", "MacroEngine.ahk");
        if (!File.Exists(ahk) || !File.Exists(script))
        {
            Log("AutoHotkey engine was not found");
            // The launcher installs AutoHotkey on the next start once it is the saved engine.
            if (!File.Exists(ahk))
                PostError(ErrorJson("shell", "ahk-missing", "AutoHotkey is not installed. Close and reopen H&le to install it, or pick native."));
            if (_hosted)
                PublishBootFailure("The macro system did not start.");
            return;
        }
        try
        {
            var start = HiddenStart(ahk);
            start.ArgumentList.Add(script);
            _engine = Process.Start(start);
        }
        catch (Exception ex)
        {
            Log("AutoHotkey start " + ex.Message);
            if (_hosted)
                PublishBootFailure("The macro system did not start.");
        }
    }

    string? NativeExe()
    {
        string[] paths =
        [
            Path.Combine(_repo, "engine-native", "build", "Release", "macroui-engine.exe"),
            Path.Combine(_repo, "engine-native", "build", "Debug", "macroui-engine.exe"),
            Path.Combine(_repo, "engine-native", "build", "macroui-engine.exe"),
            Path.Combine(AppContext.BaseDirectory, "macroui-engine.exe"),
        ];
        foreach (var path in paths)
        {
            if (File.Exists(path))
                return path;
        }
        return null;
    }

    ProcessStartInfo HiddenStart(string file) => new(file)
    {
        UseShellExecute = false,
        WorkingDirectory = _repo,
        CreateNoWindow = true,
        WindowStyle = ProcessWindowStyle.Hidden,
    };

    void ForgetEngine()
    {
        var proc = _engine;
        _engine = null;
        if (proc == null)
            return;
        try { proc.Dispose(); }
        catch (Exception ex) { Log("engine dispose " + ex.Message); }
    }

    static bool NativeProcess(Process? process)
    {
        try
        {
            return process != null && !process.HasExited
                && process.ProcessName.Equals("macroui-engine", StringComparison.OrdinalIgnoreCase);
        }
        catch
        {
            return false;
        }
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

    static Process? FindEngineProcess(string choice)
    {
        if (choice == "native")
            return FindNamed("macroui-engine", "macroui-engine");
        return FindNamed("AutoHotkey64", "MacroEngine.ahk");
    }

    static Process? FindNamed(string processName, string marker)
    {
        Process? match = null;
        foreach (var process in Process.GetProcessesByName(processName))
        {
            var cmd = "";
            try { cmd = CommandLine(process.Id); }
            catch { /* already exited */ }
            var native = processName.Equals("macroui-engine", StringComparison.OrdinalIgnoreCase);
            var skip = cmd.Contains("--self-test", StringComparison.OrdinalIgnoreCase)
                || cmd.Contains("--bench", StringComparison.OrdinalIgnoreCase);
            if (match == null && !skip && (native || cmd.Contains(marker, StringComparison.OrdinalIgnoreCase)))
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
        if (!_pageReady)
            return;
        // Posted, not Invoked: the pipe reader must never wait on the UI thread,
        // or engine writes back up while the window is busy. The view is only
        // touched inside the post. Reading it from the pipe thread throws, the
        // read loop treats that as a dead pipe, and the engine hello never lands.
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

    string LinkJson()
    {
        var connected = _pipe is { IsConnected: true };
        return JsonSerializer.Serialize(new
        {
            type = "link",
            shell = true,
            pipe = connected,
            engine = connected ? _wire : "",
            choice = _engineChoice,
        });
    }

    void PostError(string json)
    {
        _pendingError = json;
        SendToPage(json);
    }

    void Log(string message)
    {
        var line = DateTime.Now.ToString("yyyy-MM-dd HH:mm:ss", System.Globalization.CultureInfo.InvariantCulture) + " " + message;
        System.Diagnostics.Trace.TraceError(line);
        try
        {
            // The radial overlay logs from its own thread.
            lock (_logGate)
                File.AppendAllText(Path.Combine(_repo, "profiles", "shell.log"), line + Environment.NewLine);
        }
        catch
        {
            /* the trace line is already written */
        }
    }

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
        catch (Exception ex)
        {
            Log("profile read " + ex.Message);
            SendToPage(ErrorJson("shell", "profile-read", "could not read the profile on disk"));
            return;
        }
        await WritePipe(json);
    }

    async Task SendNativeProfile()
    {
        byte[] file;
        try
        {
            var text = File.ReadAllText(_profilePath);
            var profile = JsonNode.Parse(text);
            RememberSwap(profile);
            file = new UTF8Encoding(false).GetBytes(text);
        }
        catch (Exception ex)
        {
            Log("profile read " + ex.Message);
            SendToPage(ErrorJson("shell", "profile-read", "could not read the profile on disk"));
            return;
        }
        try
        {
            await WritePipeBytes(EngineLink.Profile(file)).ConfigureAwait(false);
            await WritePipeBytes(EngineLink.Output(_outputMode)).ConfigureAwait(false);
        }
        catch (Exception ex)
        {
            Log("native profile " + ex.Message);
            SendToPage(ErrorJson("shell", "profile-read", "could not read the profile on disk"));
        }
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
                await SetOutput("software", mode);
                return;
            }

            var hit = BoardPorts.List().FirstOrDefault(item => item.Chip == mode);
            if (string.IsNullOrEmpty(hit.Port))
            {
                CloseBoard();
                await SetOutput("software", mode);
                SendToPage(ErrorJson("shell", "no-board", $"no H&le {ChipLabel(mode)} board was found"));
                return;
            }

            var fail = await Task.Run(() => OpenAndAsk(hit.Port, mode));
            if (gen != _inputGen)
                return;
            if (fail != "")
            {
                CloseBoard();
                await SetOutput("software", mode);
                if (fail == "mismatch")
                    SendToPage(ErrorJson("shell", "board-mismatch", $"the board on {hit.Port} is not an H&le {ChipLabel(mode)}"));
                else
                    SendToPage(ErrorJson("shell", "no-board", $"could not open the H&le {ChipLabel(mode)} board"));
                return;
            }

            await SetOutput(mode, mode);
        }
        finally
        {
            _boardLock.Release();
        }
    }

    async Task SetOutput(string mode, string requested)
    {
        _outputMode = mode;
        _outputRequested = requested;
        var page = JsonSerializer.Serialize(new { type = "output", mode, requested });
        _outputPageJson = page;
        SendToPage(page);
        if (_wire == "native")
            await WritePipeBytes(EngineLink.Output(mode));
        else
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
        var wheel = AsInt(node?["wheel"]);
        var abs = AsBool(node?["abs"]);
        var ax = AsInt(node?["ax"]);
        var ay = AsInt(node?["ay"]);
        // A dropped up leaves that key held on the board. Wait out the lock for
        // ups. A down that loses the race is queued and sent ahead of the next
        // report, so the board still sees down before up.
        var keyed = down.Count > 0 || up.Count > 0;
        if (!_boardLock.Wait(up.Count > 0 ? 1500 : 40))
        {
            if (keyed || wheel != 0)
            {
                _hidPending.Enqueue(json);
                while (_hidPending.Count > 16)
                    _hidPending.Dequeue();
            }
            return;
        }
        try
        {
            FlushBoardPending();
            WriteHidLocked(down, up, dx, dy, wheel, abs, ax, ay);
        }
        finally
        {
            _boardLock.Release();
        }
    }

    void FlushBoardPending()
    {
        while (_hidPending.Count > 0)
            WriteHidLocked(_hidPending.Dequeue());
        while (_reportPending.Count > 0)
            WriteReportLocked(_reportPending.Dequeue());
    }

    /* The report payload. AHK still arrives as hid JSON. This is the native image.
       An up waits 1500 ms. A down that misses the wait is queued, cap 16. */
    internal void WriteReport(ReadOnlySpan<byte> image)
    {
        if (!ReportBoard.Active(image))
            return;
        var copy = image.ToArray();
        var up = copy[1] != 0;
        var keyed = copy[0] != 0 || copy[1] != 0;
        var wheel = System.Buffers.Binary.BinaryPrimitives.ReadInt32LittleEndian(copy.AsSpan(140));
        if (!_boardLock.Wait(up ? 1500 : 40))
        {
            if (keyed || wheel != 0)
            {
                _reportPending.Enqueue(copy);
                while (_reportPending.Count > 16)
                    _reportPending.Dequeue();
            }
            return;
        }
        try
        {
            FlushBoardPending();
            WriteReportLocked(copy);
        }
        finally
        {
            _boardLock.Release();
        }
    }

    void WriteReportLocked(byte[] image)
    {
        try
        {
            if (_board is not { IsOpen: true })
                return;
            var serial = _board;
            serial.ReadTimeout = 80;
            var ops = new List<BoardOp>();
            var bits = ReportBoard.Apply(image, _swapClicks, _mouseBits, ops);
            foreach (var op in ops)
            {
                if (op.Steer)
                    SteerMouse(serial, op.Bits, op.Ax, op.Ay);
                else if (op.Line != null)
                    BoardLine(serial, op.Line);
            }
            _mouseBits = bits;
        }
        catch
        {
            CloseBoard();
        }
    }

    void WriteHidLocked(string json)
    {
        JsonNode? node;
        try { node = JsonNode.Parse(json); }
        catch { return; }
        WriteHidLocked(ReadNames(node?["down"]), ReadNames(node?["up"]), AsInt(node?["x"]), AsInt(node?["y"]), AsInt(node?["wheel"]), AsBool(node?["abs"]), AsInt(node?["ax"]), AsInt(node?["ay"]));
    }

    void WriteHidLocked(List<string> down, List<string> up, int dx, int dy, int wheel, bool abs, int ax, int ay)
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
            else if (mouseTouch || dx != 0 || dy != 0 || wheel != 0)
            {
                BoardLine(serial, $"M {bits} {dx} {dy} {wheel}");
                dx = 0;
                dy = 0;
                wheel = 0;
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
            if (wheel != 0)
                BoardLine(serial, $"M {bits} 0 0 {wheel}");
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
            // Same rule as the launcher's InstallLayout.IsRoot.
            if (File.Exists(Path.Combine(dir.FullName, "engine", "MacroEngine.ahk"))
                || File.Exists(Path.Combine(dir.FullName, "setup", "EnsureRuntime.ps1")))
                return dir.FullName;
            dir = dir.Parent;
        }
        return Directory.GetCurrentDirectory();
    }
}
