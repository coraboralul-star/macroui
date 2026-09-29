using System.Diagnostics;
using System.IO;
using System.IO.Pipes;
using System.Text;
using System.Text.Json;
using System.Text.Json.Nodes;
using System.Windows;
using Microsoft.Web.WebView2.Core;

namespace MacroShell;

public partial class MainWindow : Window
{
    readonly CancellationTokenSource _cts = new();
    readonly SemaphoreSlim _write = new(1, 1);
    readonly bool _dev;
    readonly string _repo;
    readonly string _profilePath;
    NamedPipeClientStream? _pipe;
    Process? _engine;
    bool _pageReady;
    bool _launchedEngine;
    bool _closing;
    string _lastEngine = "";

    public MainWindow()
    {
        InitializeComponent();
        _dev = Environment.GetCommandLineArgs().Contains("--dev");
        _repo = FindRepo();
        _profilePath = Path.Combine(_repo, "profiles", "default.json");
        Directory.CreateDirectory(Path.GetDirectoryName(_profilePath)!);
        if (!File.Exists(_profilePath))
            File.WriteAllText(_profilePath, """{"version":1,"name":"Default","variables":{},"macros":[]}""", new UTF8Encoding(false));
    }

    async void OnLoaded(object sender, RoutedEventArgs e)
    {
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
            MessageBox.Show(this, ex.Message, "Vendetta Macros");
            return;
        }

        _ = PumpPipe(_cts.Token);
    }

    void OnClosed(object? sender, EventArgs e)
    {
        _closing = true;
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
    }

    void OnPageMessage(object? sender, CoreWebView2WebMessageReceivedEventArgs e)
    {
        JsonNode? node;
        try { node = JsonNode.Parse(e.WebMessageAsJson); }
        catch { return; }
        var type = node?["type"]?.ToString();
        if (type == "window")
        {
            switch (node?["action"]?.ToString())
            {
                case "drag":
                    try { DragMove(); } catch { /* mouse already up */ }
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
        if (type == "hello")
        {
            _pageReady = true;
            SendToPage(LinkJson());
            SendToPage(ReadyJson());
            if (_lastEngine.Length > 0)
                SendToPage(_lastEngine);
            return;
        }
        if (type == "profile" && node?["profile"] is JsonNode profile)
        {
            File.WriteAllText(_profilePath, profile.ToJsonString(new JsonSerializerOptions { WriteIndented = true }), new UTF8Encoding(false));
            _ = WritePipe(e.WebMessageAsJson);
            return;
        }
        if (type == "command")
        {
            var action = node?["action"]?.ToString();
            if (action == "reload")
            {
                _ = WritePipe(e.WebMessageAsJson);
                SendToPage(ReadyJson());
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
                await WritePipe(ProfileEnvelope());
                var buf = new byte[1024 * 1024];
                while (!ct.IsCancellationRequested)
                {
                    var n = await pipe.ReadAsync(buf, ct);
                    if (n <= 0) break;
                    var text = Encoding.UTF8.GetString(buf, 0, n);
                    _lastEngine = text;
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
        if (_launchedEngine)
            return;
        var ahk = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ProgramFiles), "AutoHotkey", "v2", "AutoHotkey64.exe");
        var script = Path.Combine(_repo, "engine", "MacroEngine.ahk");
        if (!File.Exists(ahk) || !File.Exists(script))
            return;
        _launchedEngine = true;
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

    void SendToPage(string json)
    {
        if (!_pageReady || View.CoreWebView2 == null)
            return;
        Dispatcher.Invoke(() => View.CoreWebView2.PostWebMessageAsJson(json));
    }

    string LinkJson() =>
        JsonSerializer.Serialize(new { type = "link", shell = true, pipe = _pipe is { IsConnected: true } });

    string ReadyJson()
    {
        var profile = JsonNode.Parse(File.ReadAllText(_profilePath)) ?? new JsonObject();
        return new JsonObject { ["type"] = "ready", ["profile"] = profile }.ToJsonString();
    }

    string ProfileEnvelope()
    {
        var profile = JsonNode.Parse(File.ReadAllText(_profilePath)) ?? new JsonObject();
        return new JsonObject { ["v"] = 1, ["type"] = "profile", ["profile"] = profile }.ToJsonString();
    }

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
