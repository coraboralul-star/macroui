using System.Diagnostics;
using System.IO;
using System.Windows;
using System.Windows.Threading;

namespace Vendetta;

public partial class App : System.Windows.Application
{
    Process? _shell;
    Bootstrap? _boot;

    protected override void OnStartup(StartupEventArgs e)
    {
        base.OnStartup(e);
        if (e.Args.Contains("--preview"))
        {
            ShutdownMode = ShutdownMode.OnMainWindowClose;
            var preview = new SplashWindow();
            MainWindow = preview;
            preview.Show();
            var folder = Path.Combine(Path.GetTempPath(), "vendetta-splash");
            preview.CaptureAfter(TimeSpan.FromMilliseconds(1100), Path.Combine(folder, "mid.png"));
            preview.CaptureAfter(TimeSpan.FromMilliseconds(2600), Path.Combine(folder, "hold.png"));
            var swap = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(1600) };
            swap.Tick += (_, _) =>
            {
                swap.Stop();
                preview.SetStatus("STARTING THE MACRO SYSTEM");
            };
            swap.Start();
            var close = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(3200) };
            close.Tick += (_, _) =>
            {
                close.Stop();
                preview.Close();
            };
            close.Start();
            return;
        }

        ShutdownMode = ShutdownMode.OnExplicitShutdown;
        if (ShellIsOpen())
        {
            try
            {
                using var show = EventWaitHandle.OpenExisting(@"Local\MacroUI.Show");
                show.Set();
            }
            catch { /* the open window will surface on its own if the signal is missing */ }
            Shutdown();
            return;
        }

        var splash = new SplashWindow();
        MainWindow = splash;
        splash.Show();
        var cancel = new CancellationTokenSource();
        splash.Closing += (_, _) =>
        {
            if (splash.Finished)
                return;
            cancel.Cancel();
            _boot?.KillRunning();
            try { _shell?.Kill(entireProcessTree: true); } catch { /* already gone */ }
        };
        splash.Closed += (_, _) =>
        {
            if (!splash.Finished)
                Shutdown();
        };
        _ = Open(splash, cancel, e.Args.Contains("--dev"));
    }

    async Task Open(SplashWindow splash, CancellationTokenSource cancel, bool dev)
    {
        var repo = FindRepo();
        var boot = new Bootstrap(repo, splash.SetStatus, cancel.Token);
        _boot = boot;
        var prepared = await boot.Prepare();
        if (cancel.IsCancellationRequested)
            return;
        if (!prepared.Ok)
        {
            splash.Fail(prepared.Error ?? "Setup did not finish.");
            return;
        }

        using var live = new EventWaitHandle(false, EventResetMode.ManualReset, @"Local\Vendetta.Live");
        using var failed = new EventWaitHandle(false, EventResetMode.ManualReset, @"Local\Vendetta.BootFailed");
        using var reveal = new EventWaitHandle(false, EventResetMode.ManualReset, @"Local\Vendetta.Reveal");
        live.Reset();
        failed.Reset();
        reveal.Reset();
        var bootFile = Path.Combine(Path.GetTempPath(), "Vendetta.boot");
        try { File.Delete(bootFile); } catch { /* the shell will write a fresh line */ }

        splash.SetStatus("STARTING THE WINDOW");
        var start = new ProcessStartInfo(boot.ShellExe)
        {
            UseShellExecute = false,
            WorkingDirectory = repo,
        };
        start.ArgumentList.Add("--splash-host");
        if (dev)
            start.ArgumentList.Add("--dev");
        _shell = Process.Start(start);
        if (_shell == null)
        {
            splash.Fail("The window did not open.");
            return;
        }

        var poll = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(80) };
        poll.Tick += (_, _) =>
        {
            try
            {
                if (!File.Exists(bootFile))
                    return;
                var text = File.ReadAllText(bootFile).Trim();
                if (text.Length > 0)
                    splash.SetStatus(text);
            }
            catch { /* the next tick reads it */ }
        };
        poll.Start();

        var shell = _shell;
        var waited = await Task.Run(() => WaitForLive(live, failed, shell, bootFile, cancel.Token));
        poll.Stop();
        var ready = waited.Ready;
        var error = waited.Error;
        if (cancel.IsCancellationRequested)
        {
            try { _shell.Kill(entireProcessTree: true); } catch { /* already gone */ }
            return;
        }
        if (!ready)
        {
            try { _shell.Kill(entireProcessTree: true); } catch { /* already gone */ }
            splash.Fail(error);
            return;
        }

        splash.MarkFinished();
        splash.SetStatus("OPENING");
        try { reveal.Set(); } catch { /* the shell reveals itself if this handle is already gone */ }
        await Task.Delay(80);
        splash.Close();
        Shutdown();
    }

    static (bool Ready, string Error) WaitForLive(EventWaitHandle live, EventWaitHandle failed, Process shell, string bootFile, CancellationToken cancel)
    {
        var error = "The macro system did not start.";
        var clock = Stopwatch.StartNew();
        var gates = new WaitHandle[] { live, failed };
        while (!cancel.IsCancellationRequested && clock.Elapsed < TimeSpan.FromSeconds(90))
        {
            var which = WaitHandle.WaitAny(gates, 200);
            if (which == 0)
                return (true, error);
            if (which == 1)
            {
                try
                {
                    if (File.Exists(bootFile))
                    {
                        var text = File.ReadAllText(bootFile).Trim();
                        if (text.Length > 0)
                            error = text;
                    }
                }
                catch { /* keep the fallback line */ }
                return (false, error);
            }
            try
            {
                if (shell.HasExited)
                    return (false, error);
            }
            catch
            {
                return (false, error);
            }
        }
        return (false, error);
    }

    static bool ShellIsOpen()
    {
        Mutex? mutex = null;
        try
        {
            mutex = new Mutex(false, @"Local\MacroUI.Shell");
            if (!mutex.WaitOne(0))
                return true;
            mutex.ReleaseMutex();
            return false;
        }
        catch (AbandonedMutexException)
        {
            try { mutex?.ReleaseMutex(); } catch { /* the next launch can take it */ }
            return false;
        }
        finally
        {
            mutex?.Dispose();
        }
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
