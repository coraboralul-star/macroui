using System.Buffers.Binary;
using System.Text;

namespace MacroShell;

/* The pipe peer. AHK messages start with '{'. A native frame is MUI1 version 1.
   See docs/engine/ipc-protocol.md. */
enum EnginePeer
{
    Ahk,
    Native,
    Reject
}

/* One engine message turned into the page JSON the bridge already parses.
   Hello has no JSON. A report is the 156-byte image and is not a page message. */
readonly struct EngineInbound
{
    public string? Json { get; init; }
    public bool Live { get; init; }
    public bool State { get; init; }
    public byte[]? Report { get; init; }
    /* A radial frame: what the overlay draws. Not a page message. */
    public RadialView? Radial { get; init; }
}

static class EngineLink
{
    public const int MaxFrame = 1048576;
    const int MaxString = 4096;

    public static EnginePeer Classify(ReadOnlySpan<byte> message)
    {
        if (message.Length == 0)
            return EnginePeer.Reject;
        if (message[0] == 0x7B)
            return EnginePeer.Ahk;
        if (!ValidHeader(message))
            return EnginePeer.Reject;
        return EnginePeer.Native;
    }

    /* False is a bad frame. The shell drops that client. An unknown type is
       ignored: true, with no JSON and no report. */
    public static bool TryTranslate(ReadOnlySpan<byte> message, out EngineInbound inbound)
    {
        inbound = default;
        if (!ValidHeader(message))
            return false;
        var type = BinaryPrimitives.ReadUInt16LittleEndian(message.Slice(6));
        var payload = message.Slice(12);
        switch (type)
        {
            case 1:
                return Hello(payload, out inbound);
            case 2:
                return State(payload, out inbound);
            case 3:
                return Ack(payload, out inbound);
            case 4:
                return Error(payload, out inbound);
            case 5:
                return Report(payload, out inbound);
            case 9:
                return Radial(payload, out inbound);
            default:
                return true;
        }
    }

    public static byte[] Command(string action)
    {
        byte code = action switch
        {
            "start" => 1,
            "stop" => 2,
            "reload" => 3,
            "quit" => 4,
            _ => 0
        };
        return Frame(8, new[] { code });
    }

    public static byte[] Output(string mode)
    {
        byte code = mode switch
        {
            "rp2040" => 1,
            "rp2350" => 2,
            _ => 0
        };
        return Frame(7, new[] { code });
    }

    /* The profile document itself, not the AHK {"type":"profile"} envelope. */
    public static byte[] Profile(ReadOnlySpan<byte> json)
    {
        if (json.Length > MaxFrame - 16)
            throw new ArgumentException("profile frame is too large");
        var payload = new byte[4 + json.Length];
        BinaryPrimitives.WriteUInt32LittleEndian(payload, (uint)json.Length);
        json.CopyTo(payload.AsSpan(4));
        return Frame(6, payload);
    }

    /* Same escapes as JSON.Quote: short escapes for \ " CR LF tab, then \u00XX. */
    public static string Quote(string text)
    {
        var sb = new StringBuilder(text.Length + 2);
        sb.Append('"');
        foreach (var c in text)
        {
            switch (c)
            {
                case '\\':
                    sb.Append("\\\\");
                    break;
                case '"':
                    sb.Append("\\\"");
                    break;
                case '\r':
                    sb.Append("\\r");
                    break;
                case '\n':
                    sb.Append("\\n");
                    break;
                case '\t':
                    sb.Append("\\t");
                    break;
                default:
                    if (c < 0x20)
                        sb.Append("\\u").Append(((int)c).ToString("X4"));
                    else
                        sb.Append(c);
                    break;
            }
        }
        sb.Append('"');
        return sb.ToString();
    }

    static bool Hello(ReadOnlySpan<byte> payload, out EngineInbound inbound)
    {
        inbound = default;
        if (payload.Length != 1 || payload[0] > 1)
            return false;
        inbound = new EngineInbound { Live = true };
        return true;
    }

    static bool State(ReadOnlySpan<byte> payload, out EngineInbound inbound)
    {
        inbound = default;
        var cur = new Cursor(payload);
        if (!cur.U8(out var armed) || armed > 1)
            return false;
        if (!cur.U8(out _))
            return false;
        if (!cur.U16(out var running) || running > 256)
            return false;
        if (!cur.U16(out var held) || held > 64)
            return false;
        if (!cur.U16(out var windows) || windows > 256)
            return false;
        if (!cur.Str(out var active) || !cur.Str(out var front))
            return false;
        var sb = new StringBuilder();
        sb.Append("{\"v\":1,\"type\":\"state\",\"armed\":");
        sb.Append(armed == 1 ? "true" : "false");
        sb.Append(",\"running\":[");
        for (var i = 0; i < running; i++)
        {
            if (!cur.Str(out var id) || !cur.Str(out var name))
                return false;
            if (i > 0)
                sb.Append(',');
            sb.Append("{\"id\":").Append(Quote(id)).Append(",\"name\":").Append(Quote(name)).Append('}');
        }
        sb.Append("],\"held\":[");
        for (var i = 0; i < held; i++)
        {
            if (!cur.Str(out var key))
                return false;
            if (i > 0)
                sb.Append(',');
            sb.Append(Quote(key));
        }
        sb.Append("],\"activeId\":").Append(Quote(active));
        sb.Append(",\"front\":").Append(Quote(front));
        sb.Append(",\"windows\":[");
        for (var i = 0; i < windows; i++)
        {
            if (!cur.Str(out var exe) || !cur.Str(out var title))
                return false;
            if (i > 0)
                sb.Append(',');
            sb.Append("{\"exe\":").Append(Quote(exe)).Append(",\"title\":").Append(Quote(title)).Append('}');
        }
        sb.Append("]}");
        if (!cur.Done)
            return false;
        inbound = new EngineInbound { Json = sb.ToString(), Live = true, State = true };
        return true;
    }

    static bool Ack(ReadOnlySpan<byte> payload, out EngineInbound inbound)
    {
        inbound = default;
        var cur = new Cursor(payload);
        if (!cur.U8(out var ok) || ok > 1)
            return false;
        if (!cur.U8(out var which) || !cur.Str(out var error) || !cur.Done)
            return false;
        var sb = new StringBuilder();
        sb.Append("{\"v\":1,\"type\":\"ack\",\"ok\":");
        sb.Append(ok == 1 ? "true" : "false");
        if (which == 1)
            sb.Append(",\"for\":\"command\"");
        else if (which == 2)
            sb.Append(",\"for\":\"profile\"");
        if (error.Length > 0)
            sb.Append(",\"error\":").Append(Quote(error));
        sb.Append('}');
        inbound = new EngineInbound { Json = sb.ToString() };
        return true;
    }

    static bool Error(ReadOnlySpan<byte> payload, out EngineInbound inbound)
    {
        inbound = default;
        var cur = new Cursor(payload);
        if (!cur.U8(out var where) || !cur.U8(out var code) || !cur.Str(out var detail) || !cur.Done)
            return false;
        var whereText = where == 1 ? "engine" : "unknown";
        var codeText = where == 1 && code == 1 ? "profile-read" : code.ToString();
        var sb = new StringBuilder();
        sb.Append("{\"v\":1,\"type\":\"error\",\"where\":").Append(Quote(whereText));
        sb.Append(",\"code\":").Append(Quote(codeText));
        sb.Append(",\"detail\":").Append(Quote(detail));
        sb.Append('}');
        inbound = new EngineInbound { Json = sb.ToString() };
        return true;
    }

    static bool Report(ReadOnlySpan<byte> payload, out EngineInbound inbound)
    {
        inbound = default;
        if (payload.Length != 156)
            return false;
        if (payload[0] > 32 || payload[1] > 32)
            return false;
        inbound = new EngineInbound { Report = payload.ToArray() };
        return true;
    }

    /* engine_radial_frame. Fixed 22 bytes, then id, name, prev, next and one
       label per slot. */
    static bool Radial(ReadOnlySpan<byte> payload, out EngineInbound inbound)
    {
        inbound = default;
        if (payload.Length < 22)
            return false;
        var open = payload[0];
        var mode = payload[1] == 1 ? "flick" : "keys";
        var count = payload[2];
        if (open > 1 || count > 8)
            return false;
        var cur = new Cursor(payload.Slice(22));
        if (!cur.Str(out var id) || !cur.Str(out var name) || !cur.Str(out var prev) || !cur.Str(out var next))
            return false;
        var labels = new string[count];
        for (var i = 0; i < count; i++)
        {
            if (!cur.Str(out var label))
                return false;
            labels[i] = label;
        }
        if (!cur.Done)
            return false;
        inbound = new EngineInbound
        {
            Radial = new RadialView
            {
                Open = open == 1,
                Mode = mode,
                SlotCount = count,
                Lit = (sbyte)payload[3],
                At = payload[4] == 1 ? "cursor" : "center",
                Why = open == 1 ? "" : RadialView.WhyName(payload[5], mode),
                Size = BinaryPrimitives.ReadUInt16LittleEndian(payload.Slice(6)),
                Opacity = payload[8],
                Seq = BinaryPrimitives.ReadUInt32LittleEndian(payload.Slice(10)),
                Stamp = BinaryPrimitives.ReadInt64LittleEndian(payload.Slice(14)),
                Id = id,
                Name = name,
                Prev = prev,
                Next = next,
                Labels = labels
            }.Clamped()
        };
        return true;
    }

    static bool ValidHeader(ReadOnlySpan<byte> message)
    {
        if (message.Length < 12 || message.Length > MaxFrame)
            return false;
        if (message[0] != 0x4D || message[1] != 0x55 || message[2] != 0x49 || message[3] != 0x31)
            return false;
        if (BinaryPrimitives.ReadUInt16LittleEndian(message.Slice(4)) != 1)
            return false;
        var length = BinaryPrimitives.ReadUInt32LittleEndian(message.Slice(8));
        return length == (uint)(message.Length - 12);
    }

    static byte[] Frame(ushort type, ReadOnlySpan<byte> payload)
    {
        var msg = new byte[12 + payload.Length];
        msg[0] = 0x4D;
        msg[1] = 0x55;
        msg[2] = 0x49;
        msg[3] = 0x31;
        BinaryPrimitives.WriteUInt16LittleEndian(msg.AsSpan(4), 1);
        BinaryPrimitives.WriteUInt16LittleEndian(msg.AsSpan(6), type);
        BinaryPrimitives.WriteUInt32LittleEndian(msg.AsSpan(8), (uint)payload.Length);
        payload.CopyTo(msg.AsSpan(12));
        return msg;
    }

    ref struct Cursor
    {
        readonly ReadOnlySpan<byte> _buf;
        int _at;

        public Cursor(ReadOnlySpan<byte> buf)
        {
            _buf = buf;
            _at = 0;
        }

        public bool Done => _at == _buf.Length;

        public bool U8(out byte value)
        {
            value = 0;
            if (_at >= _buf.Length)
                return false;
            value = _buf[_at++];
            return true;
        }

        public bool U16(out ushort value)
        {
            value = 0;
            if (_at + 2 > _buf.Length)
                return false;
            value = BinaryPrimitives.ReadUInt16LittleEndian(_buf.Slice(_at));
            _at += 2;
            return true;
        }

        public bool Str(out string value)
        {
            value = "";
            if (!U16(out var count) || count > MaxString || _at + count > _buf.Length)
                return false;
            value = Encoding.UTF8.GetString(_buf.Slice(_at, count));
            _at += count;
            return true;
        }
    }
}
