using System.Text.Json;

namespace MacroShell;

/* What the overlay draws, as the engine sent it. The shell never reads the
   profile for this: a native radial frame (type 9) or the AHK {"type":"radial"}
   message is the whole truth. Lit is 0-based, -1 for none. Stamp is the QPC
   of the trigger down, 0 when unknown. Why is the close reason. */
sealed record RadialView
{
    public bool Open { get; init; }
    public string Mode { get; init; } = "keys";
    public int SlotCount { get; init; }
    public int Lit { get; init; } = -1;
    public string At { get; init; } = "center";
    public string Why { get; init; } = "";
    public int Size { get; init; } = 340;
    public int Opacity { get; init; } = 88;
    public uint Seq { get; init; }
    public long Stamp { get; init; }
    public string Id { get; init; } = "";
    public string Name { get; init; } = "";
    public string Prev { get; init; } = "";
    public string Next { get; init; } = "";
    public string[] Labels { get; init; } = Array.Empty<string>();

    public const int SizeMin = 200;
    public const int SizeMax = 600;
    public const int OpacityMin = 40;
    public const int OpacityMax = 100;

    /* engine_radial_why_name. No slot reads dead-zone for flick. */
    public static string WhyName(int why, string mode) => why switch
    {
        1 => "played",
        2 => "playing",
        3 => "refused",
        4 => mode == "flick" ? "dead-zone" : "no-slot",
        5 => "empty-slot",
        6 => "focus",
        7 => "disabled",
        8 => "profile",
        9 => "stop",
        _ => "none"
    };

    /* Sizes and opacity out of range are clamped, as the engines do. */
    public RadialView Clamped() => this with
    {
        Size = Math.Clamp(Size, SizeMin, SizeMax),
        Opacity = Math.Clamp(Opacity, OpacityMin, OpacityMax),
        SlotCount = Math.Clamp(SlotCount, 0, 8),
        Lit = Lit >= 0 && Lit < Math.Clamp(SlotCount, 0, 8) ? Lit : -1
    };

    /* The AHK message. Null when it is not a radial message. */
    public static RadialView? FromJson(string text)
    {
        try
        {
            using var doc = JsonDocument.Parse(text);
            var root = doc.RootElement;
            if (root.ValueKind != JsonValueKind.Object || Str(root, "type") != "radial")
                return null;
            var labels = new List<string>();
            if (root.TryGetProperty("labels", out var list) && list.ValueKind == JsonValueKind.Array)
                foreach (var item in list.EnumerateArray())
                    labels.Add(item.ValueKind == JsonValueKind.String ? item.GetString() ?? "" : "");
            return new RadialView
            {
                Open = root.TryGetProperty("open", out var open) && open.ValueKind == JsonValueKind.True,
                Mode = Str(root, "mode") == "flick" ? "flick" : "keys",
                SlotCount = Int(root, "slots", labels.Count),
                Lit = Int(root, "lit", -1),
                At = Str(root, "at") == "cursor" ? "cursor" : "center",
                Why = Str(root, "why"),
                Size = Int(root, "size", 340),
                Opacity = Int(root, "opacity", 88),
                Seq = (uint)Math.Max(0, Int(root, "seq", 0)),
                Stamp = root.TryGetProperty("stamp", out var stamp) && stamp.TryGetInt64(out var s) ? s : 0,
                Id = Str(root, "id"),
                Name = Str(root, "name"),
                Prev = Str(root, "prev"),
                Next = Str(root, "next"),
                Labels = labels.ToArray()
            }.Clamped();
        }
        catch (JsonException)
        {
            return null;
        }
    }

    static string Str(JsonElement root, string name) =>
        root.TryGetProperty(name, out var v) && v.ValueKind == JsonValueKind.String ? v.GetString() ?? "" : "";

    static int Int(JsonElement root, string name, int fallback) =>
        root.TryGetProperty(name, out var v) && v.ValueKind == JsonValueKind.Number && v.TryGetInt32(out var n) ? n : fallback;
}
