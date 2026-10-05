using System.Diagnostics;
using System.IO;
using System.Text.RegularExpressions;
using Microsoft.Win32;

namespace Vendetta;

sealed class Bootstrap
{
    readonly string _repo;
    readonly Action<string> _status;
    readonly CancellationToken _cancel;
    Process? _running;

    public string ShellExe { get; }

    public Bootstrap(string repo, Action<string> status, CancellationToken cancel)
    {
        _repo = repo;
        _status = status;
        _cancel = cancel;
        ShellExe = ResolveShell(repo);
    }

    public async Task<(bool Ok, string? Error)> Prepare()
    {
        if (!File.Exists(Path.Combine(_repo, "engine", "MacroEngine.ahk")))
            return (false, "The H&le folder was not found.");
        if (!HasTool("node"))
            return (false, "Install Node.js, then run this again.");
        if (!HasTool("dotnet"))
            return (false, "Install .NET, then run this again.");
        if (_cancel.IsCancellationRequested)
            return (false, null);

        if (!Directory.Exists(Path.Combine(_repo, "ui", "node_modules")))
        {
            _status("INSTALLING THE EDITOR");
            if (await Shell("cmd.exe", Path.Combine(_repo, "ui"), "/d", "/c", "npm", "install") != 0)
                return (false, "The editor did not install.");
        }
        if (_cancel.IsCancellationRequested)
            return (false, null);

        var dist = Path.Combine(_repo, "ui", "dist", "index.html");
        if (IsStale(dist, Path.Combine(_repo, "ui", "src"), Path.Combine(_repo, "ui", "index.html")))
        {
            _status("BUILDING THE EDITOR");
            if (await Shell("cmd.exe", Path.Combine(_repo, "ui"), "/d", "/c", "npm", "run", "build") != 0)
                return (false, "The editor did not build.");
        }
        if (_cancel.IsCancellationRequested)
            return (false, null);

        if (IsStale(ShellExe, Path.Combine(_repo, "shell")))
        {
            _status("BUILDING THE WINDOW");
            var csproj = Path.Combine(_repo, "shell", "MacroShell.csproj");
            if (await Shell("dotnet", _repo, "build", csproj, "-c", "Debug", "--nologo") != 0)
                return (false, "The window did not build.");
        }
        if (_cancel.IsCancellationRequested)
            return (false, null);

        if (!RuntimeReady())
        {
            _status("WINDOWS WILL ASK FOR APPROVAL");
            var script = Path.Combine(_repo, "setup", "EnsureRuntime.ps1");
            if (await Shell("powershell.exe", _repo, "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", script) != 0)
                return (false, "Setup did not finish. Run this again when you are online.");
        }
        if (!File.Exists(ShellExe))
            return (false, "The window build is missing.");
        return (true, null);
    }

    public void KillRunning()
    {
        try { _running?.Kill(entireProcessTree: true); } catch { /* already gone */ }
    }

    async Task<int> Shell(string file, string cwd, params string[] args)
    {
        if (_cancel.IsCancellationRequested)
            return 1;
        var start = new ProcessStartInfo(file)
        {
            WorkingDirectory = cwd,
            UseShellExecute = false,
            CreateNoWindow = true,
        };
        foreach (var arg in args)
            start.ArgumentList.Add(arg);
        using var process = Process.Start(start);
        if (process == null)
            return 1;
        _running = process;
        try
        {
            await process.WaitForExitAsync(_cancel);
        }
        catch (OperationCanceledException)
        {
            try { process.Kill(entireProcessTree: true); } catch { /* already gone */ }
            return 1;
        }
        finally
        {
            if (ReferenceEquals(_running, process))
                _running = null;
        }
        return process.ExitCode;
    }

    static string ResolveShell(string repo)
    {
        var framework = "net10.0-windows";
        var project = Path.Combine(repo, "shell", "MacroShell.csproj");
        if (File.Exists(project))
        {
            var match = Regex.Match(File.ReadAllText(project), "<TargetFramework>([^<]+)</TargetFramework>");
            if (match.Success)
                framework = match.Groups[1].Value.Trim();
        }
        return Path.Combine(repo, "shell", "bin", "Debug", framework, "MacroShell.exe");
    }

    static bool IsStale(string output, params string[] roots)
    {
        if (!File.Exists(output))
            return true;
        var built = File.GetLastWriteTimeUtc(output);
        foreach (var root in roots)
        {
            if (!Path.Exists(root))
                continue;
            if (File.Exists(root) && !Directory.Exists(root))
            {
                if (File.GetLastWriteTimeUtc(root) > built)
                    return true;
                continue;
            }
            foreach (var file in Files(root))
            {
                if (File.GetLastWriteTimeUtc(file) > built)
                    return true;
            }
        }
        return false;
    }

    static IEnumerable<string> Files(string root)
    {
        var pending = new Stack<string>();
        pending.Push(root);
        while (pending.Count > 0)
        {
            var dir = pending.Pop();
            IEnumerable<string> children;
            try { children = Directory.EnumerateDirectories(dir); }
            catch { continue; }
            foreach (var child in children)
            {
                var name = Path.GetFileName(child);
                if (name is "bin" or "obj" or "node_modules" or "dist" or ".webview2" or ".git")
                    continue;
                pending.Push(child);
            }
            IEnumerable<string> files;
            try { files = Directory.EnumerateFiles(dir); }
            catch { continue; }
            foreach (var file in files)
                yield return file;
        }
    }

    static bool RuntimeReady()
    {
        var programs = Environment.GetFolderPath(Environment.SpecialFolder.ProgramFiles);
        if (!File.Exists(Path.Combine(programs, "AutoHotkey", "v2", "AutoHotkey64.exe")))
            return false;
        var desktop = Path.Combine(programs, "dotnet", "shared", "Microsoft.WindowsDesktop.App");
        if (!Directory.Exists(desktop) || !Directory.EnumerateDirectories(desktop, "10.*").Any())
            return false;
        return WebViewReady();
    }

    static bool WebViewReady()
    {
        const string id = @"SOFTWARE\Microsoft\EdgeUpdate\Clients\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}";
        const string wow = @"SOFTWARE\WOW6432Node\Microsoft\EdgeUpdate\Clients\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}";
        foreach (var view in new[] { RegistryView.Registry64, RegistryView.Default })
        {
            try
            {
                using var machine = RegistryKey.OpenBaseKey(RegistryHive.LocalMachine, view);
                if (HasRuntime(machine, id) || HasRuntime(machine, wow))
                    return true;
            }
            catch { /* this hive is unavailable */ }
        }
        try
        {
            using var user = RegistryKey.OpenBaseKey(RegistryHive.CurrentUser, RegistryView.Default);
            if (HasRuntime(user, id))
                return true;
        }
        catch { /* this hive is unavailable */ }
        return false;
    }

    static bool HasRuntime(RegistryKey root, string path)
    {
        using var key = root.OpenSubKey(path);
        var version = key?.GetValue("pv")?.ToString();
        return !string.IsNullOrEmpty(version) && version != "0.0.0.0";
    }

    static bool HasTool(string name)
    {
        var path = Environment.GetEnvironmentVariable("PATH");
        if (string.IsNullOrEmpty(path))
            return false;
        foreach (var dir in path.Split(Path.PathSeparator, StringSplitOptions.RemoveEmptyEntries))
        {
            if (File.Exists(Path.Combine(dir, name))
                || File.Exists(Path.Combine(dir, name + ".exe"))
                || File.Exists(Path.Combine(dir, name + ".cmd")))
                return true;
        }
        return false;
    }
}
