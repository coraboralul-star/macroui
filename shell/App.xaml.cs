using System.Threading;
using System.Windows;

namespace MacroShell;

public partial class App : System.Windows.Application
{
    Mutex? _mutex;
    bool _ownsMutex;

    public static EventWaitHandle? ShowSignal { get; private set; }

    protected override void OnStartup(StartupEventArgs e)
    {
        ShowSignal = new EventWaitHandle(false, EventResetMode.AutoReset, @"Local\MacroUI.Show");
        _mutex = new Mutex(false, @"Local\MacroUI.Shell");
        try
        {
            _ownsMutex = _mutex.WaitOne(0);
        }
        catch (AbandonedMutexException)
        {
            _ownsMutex = true;
        }
        if (!_ownsMutex)
        {
            try { ShowSignal.Set(); } catch { /* the first window will notice if it is still there */ }
            Shutdown();
            return;
        }
        var window = new MainWindow(e.Args.Contains("--splash-host"));
        MainWindow = window;
        window.Show();
    }

    protected override void OnExit(ExitEventArgs e)
    {
        if (_ownsMutex)
        {
            try { _mutex?.ReleaseMutex(); } catch { /* already released */ }
        }
        try { _mutex?.Dispose(); } catch { /* closing */ }
        try { ShowSignal?.Dispose(); } catch { /* closing */ }
        base.OnExit(e);
    }
}
