using System.IO;
using System.IO.Compression;
using System.Reflection;
using System.Security.Cryptography;

namespace Vendetta;

static class InstallLayout
{
    public static (string Root, bool DevRepo) Locate(Action<string> status, CancellationToken cancel)
    {
        cancel.ThrowIfCancellationRequested();
        var found = FindExisting(ExeDirectory());
        if (found != null)
            return (found, IsDevRepo(found));
        return (Unpack(status, cancel), false);
    }

    public static string ExeDirectory()
    {
        var path = Environment.ProcessPath;
        if (!string.IsNullOrEmpty(path))
        {
            var dir = Path.GetDirectoryName(path);
            if (!string.IsNullOrEmpty(dir))
                return dir;
        }
        return AppContext.BaseDirectory;
    }

    static string Unpack(Action<string> status, CancellationToken cancel)
    {
        var root = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "H&le");
        Directory.CreateDirectory(root);
        using var gate = new Mutex(false, @"Local\Handle.Unpack");
        try
        {
            try { gate.WaitOne(); }
            catch (AbandonedMutexException) { /* the previous unpack died; this launch finishes it */ }
            return UnpackCore(root, status, cancel);
        }
        finally
        {
            try { gate.ReleaseMutex(); } catch { /* already released */ }
        }
    }

    static string UnpackCore(string root, Action<string> status, CancellationToken cancel)
    {
        var temp = Path.Combine(Path.GetTempPath(), "handle-payload-" + Environment.ProcessId + ".zip");
        try
        {
            string hash;
            using (var resource = OpenZip())
            using (var sha = SHA256.Create())
            using (var file = File.Create(temp))
            {
                var buffer = new byte[128 * 1024];
                int read;
                while ((read = resource.Read(buffer, 0, buffer.Length)) > 0)
                {
                    cancel.ThrowIfCancellationRequested();
                    sha.TransformBlock(buffer, 0, read, null, 0);
                    file.Write(buffer, 0, read);
                }
                sha.TransformFinalBlock([], 0, 0);
                hash = Convert.ToHexString(sha.Hash!).ToLowerInvariant();
            }

            var stamp = Path.Combine(root, "payload.sha256");
            if (File.Exists(stamp)
                && string.Equals(File.ReadAllText(stamp).Trim(), hash, StringComparison.OrdinalIgnoreCase)
                && Ready(root))
                return root;

            status("PREPARING THE APP");
            Extract(temp, root, cancel);
            if (!Ready(root))
                throw new InvalidOperationException("The app did not unpack.");
            File.WriteAllText(stamp, hash);
            return root;
        }
        finally
        {
            try { if (File.Exists(temp)) File.Delete(temp); } catch { /* the next launch writes a new zip */ }
        }
    }

    // Everything the payload ships. Both engines are in it until #54.
    static bool Ready(string root) =>
        File.Exists(Path.Combine(root, "shell", "bin", "Debug", "net10.0-windows", "MacroShell.exe"))
        && File.Exists(Path.Combine(root, "shell", "bin", "Debug", "net10.0-windows", "macroui-engine.exe"))
        && File.Exists(Path.Combine(root, "ui", "dist", "index.html"))
        && File.Exists(Path.Combine(root, "engine", "MacroEngine.ahk"))
        && File.Exists(Path.Combine(root, "engine", "JSON.ahk"))
        && File.Exists(Path.Combine(root, "engine", "TracePlay.ahk"))
        && File.Exists(Path.Combine(root, "setup", "EnsureRuntime.ps1"));

    static void Extract(string zipPath, string root, CancellationToken cancel)
    {
        var prefix = Path.GetFullPath(root).TrimEnd(Path.DirectorySeparatorChar) + Path.DirectorySeparatorChar;
        using var zip = ZipFile.OpenRead(zipPath);
        foreach (var entry in zip.Entries)
        {
            cancel.ThrowIfCancellationRequested();
            if (string.IsNullOrEmpty(entry.Name))
                continue;
            var parts = entry.FullName.Replace('\\', '/').Split('/', StringSplitOptions.RemoveEmptyEntries);
            if (parts.Length == 0 || parts.Any(part => part is ".." or "."))
                continue;
            if (parts[0].Equals("profiles", StringComparison.OrdinalIgnoreCase))
                continue;
            var dest = Path.GetFullPath(Path.Combine(root, Path.Combine(parts)));
            if (!dest.StartsWith(prefix, StringComparison.OrdinalIgnoreCase))
                continue;
            Directory.CreateDirectory(Path.GetDirectoryName(dest)!);
            entry.ExtractToFile(dest, overwrite: true);
        }
    }

    static Stream OpenZip()
    {
        var asm = Assembly.GetExecutingAssembly();
        var stream = asm.GetManifestResourceStream("payload.zip");
        if (stream != null)
            return stream;
        foreach (var name in asm.GetManifestResourceNames())
        {
            if (!name.EndsWith("payload.zip", StringComparison.OrdinalIgnoreCase))
                continue;
            var found = asm.GetManifestResourceStream(name);
            if (found != null)
                return found;
        }
        throw new InvalidOperationException("The app package is missing.");
    }

    static string? FindExisting(string start)
    {
        var dir = new DirectoryInfo(start);
        while (dir != null)
        {
            if (IsRoot(dir.FullName))
                return dir.FullName;
            dir = dir.Parent;
        }
        return null;
    }

    // An app folder, whichever engine it has. setup\EnsureRuntime.ps1 is in the
    // repo and the payload, and stays when the AHK files go in #54.
    public static bool IsRoot(string dir) =>
        File.Exists(Path.Combine(dir, "engine", "MacroEngine.ahk"))
        || File.Exists(Path.Combine(dir, "setup", "EnsureRuntime.ps1"));

    static bool IsDevRepo(string root) =>
        File.Exists(Path.Combine(root, "shell", "MacroShell.csproj"))
        && File.Exists(Path.Combine(root, "ui", "package.json"));
}
