using System.Text;

namespace MacroShell;

static class EngineLinkTest
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

    static Dictionary<string, (string Direction, byte[] Bytes)> LoadVectors(string path)
    {
        var map = new Dictionary<string, (string, byte[])>();
        string? name = null;
        string? direction = null;
        foreach (var raw in File.ReadAllLines(path))
        {
            var line = raw.Trim();
            if (line.Length == 0 || line[0] == '#')
                continue;
            if (name == null)
            {
                var parts = line.Split(' ', StringSplitOptions.RemoveEmptyEntries);
                if (parts.Length != 2)
                    throw new InvalidDataException(line);
                name = parts[0];
                direction = parts[1];
                continue;
            }
            var bytes = line.Split(' ', StringSplitOptions.RemoveEmptyEntries).Select(hex => Convert.ToByte(hex, 16)).ToArray();
            map[name] = (direction!, bytes);
            name = null;
            direction = null;
        }
        if (name != null)
            throw new InvalidDataException("vector without bytes");
        return map;
    }

    static int Main()
    {
        var vectors = LoadVectors(Path.Combine(FindRepo(), "docs", "engine", "ipc-vectors.txt"));

        Expect(EngineLink.Classify(vectors["json-hello"].Bytes) == EnginePeer.Ahk, "json hello is AHK");
        Expect(EngineLink.Classify(vectors["hello-disarmed"].Bytes) == EnginePeer.Native, "hello classifies native");
        Expect(EngineLink.Classify(vectors["reject-short"].Bytes) == EnginePeer.Reject, "short frame is rejected");
        Expect(EngineLink.Classify(vectors["reject-trailing"].Bytes) == EnginePeer.Reject, "trailing byte is rejected");
        Expect(EngineLink.Classify(vectors["reject-magic"].Bytes) == EnginePeer.Reject, "bad magic is rejected");

        var badVersion = (byte[])vectors["hello-disarmed"].Bytes.Clone();
        badVersion[4] = 2;
        Expect(EngineLink.Classify(badVersion) == EnginePeer.Reject, "version 2 is rejected");

        foreach (var (name, row) in vectors)
        {
            if (row.Direction != "reject")
                continue;
            Expect(!EngineLink.TryTranslate(row.Bytes, out _), name + " does not translate");
        }

        Expect(EngineLink.TryTranslate(vectors["hello-disarmed"].Bytes, out var hello) && hello.Live && hello.Json == null && hello.Report == null, "hello has no page json");
        Expect(EngineLink.TryTranslate(vectors["hello-armed"].Bytes, out var armed) && armed.Live, "armed hello is live");

        Expect(Page("state-sample", vectors) == "{\"v\":1,\"type\":\"state\",\"armed\":true,\"running\":[{\"id\":\"sprint-jump\",\"name\":\"Sprint jump\"}],\"held\":[\"space\"],\"activeId\":\"default\",\"front\":\"Game.exe\",\"windows\":[{\"exe\":\"Game.exe\",\"title\":\"Game\"}]}", "state-sample json");
        Expect(Page("ack-command", vectors) == "{\"v\":1,\"type\":\"ack\",\"ok\":true,\"for\":\"command\"}", "ack command");
        Expect(Page("ack-profile", vectors) == "{\"v\":1,\"type\":\"ack\",\"ok\":true,\"for\":\"profile\"}", "ack profile");
        Expect(Page("ack-bad-frame", vectors) == "{\"v\":1,\"type\":\"ack\",\"ok\":false,\"error\":\"bad frame\"}", "ack bad frame");
        Expect(Page("ack-bad-version", vectors) == "{\"v\":1,\"type\":\"ack\",\"ok\":false,\"error\":\"bad version\"}", "ack bad version");
        Expect(Page("error-profile-read", vectors) == "{\"v\":1,\"type\":\"error\",\"where\":\"engine\",\"code\":\"profile-read\",\"detail\":\"the saved profile could not be read\"}", "error profile-read");

        foreach (var name in new[] { "report-relative", "report-absolute", "report-three-downs", "report-up", "report-overflow" })
        {
            Expect(EngineLink.TryTranslate(vectors[name].Bytes, out var report) && report.Json == null && report.Report != null && report.Report.AsSpan().SequenceEqual(vectors[name].Bytes.AsSpan(12)), name + " image");
        }

        var empty = Encoding.UTF8.GetBytes("{\"version\":1,\"name\":\"Default\",\"variables\":{},\"macros\":[]}");
        Expect(EngineLink.Profile(empty).AsSpan().SequenceEqual(vectors["profile-empty"].Bytes), "profile-empty");
        Expect(EngineLink.Profile(new byte[] { 0x5B, 0x5D }).AsSpan().SequenceEqual(vectors["profile-not-object"].Bytes), "profile-not-object");
        Expect(Same(EngineLink.Output("software"), vectors["output-software"].Bytes), "output software");
        Expect(Same(EngineLink.Output("rp2040"), vectors["output-rp2040"].Bytes), "output rp2040");
        Expect(Same(EngineLink.Output("rp2350"), vectors["output-rp2350"].Bytes), "output rp2350");
        Expect(Same(EngineLink.Output("other"), vectors["output-software"].Bytes), "unknown output is software");
        Expect(Same(EngineLink.Command("start"), vectors["command-start"].Bytes), "command start");
        Expect(Same(EngineLink.Command("stop"), vectors["command-stop"].Bytes), "command stop");
        Expect(Same(EngineLink.Command("reload"), vectors["command-reload"].Bytes), "command reload");
        Expect(Same(EngineLink.Command("quit"), vectors["command-quit"].Bytes), "command quit");
        Expect(Same(EngineLink.Command("nope"), vectors["command-unknown"].Bytes), "unknown command");

        Expect(EngineLink.Quote("a\u0001b") == "\"a\\u0001b\"", "control chars use the long escape");
        Expect(EngineLink.Quote("x\ny") == "\"x\\ny\"", "newline keeps the short escape");
        Expect(EngineLink.Quote("a\"b\\c") == "\"a\\\"b\\\\c\"", "quote and backslash");

        if (fails == 0)
            Console.WriteLine("ok");
        return fails == 0 ? 0 : 1;
    }

    static string? Page(string name, Dictionary<string, (string Direction, byte[] Bytes)> vectors)
    {
        if (!EngineLink.TryTranslate(vectors[name].Bytes, out var inbound))
            return null;
        return inbound.Json;
    }

    static bool Same(byte[] got, byte[] want) => got.AsSpan().SequenceEqual(want);
}
