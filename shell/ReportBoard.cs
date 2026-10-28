using System.Buffers.Binary;
using System.Globalization;

namespace MacroShell;

/* K and M lines for one report image. The port stays here.
   The image is the 156-byte payload from docs/engine/ipc-protocol.md.
   Scale, wheel sign, and swapClicks run here. SteerMouse stays in the window. */
public readonly record struct BoardOp(string? Line, bool Steer, int Bits, int Ax, int Ay);

public static class ReportBoard
{
    public const int Bytes = 156;
    const int Cap = 32;

    public static bool Active(ReadOnlySpan<byte> image)
    {
        if (image.Length != Bytes)
            return false;
        if (image[0] != 0 || image[1] != 0)
            return true;
        if ((Flags(image) & 1) != 0)
            return true;
        if (ReadI32(image, 132) != 0 || ReadI32(image, 136) != 0 || ReadI32(image, 140) != 0)
            return true;
        return false;
    }

    /* Returns the mouse bits after the plan. A short or idle image adds nothing. */
    public static int Apply(ReadOnlySpan<byte> image, bool swapClicks, int bits, List<BoardOp> ops)
    {
        if (!Active(image) || ops == null)
            return bits;

        var dx = ReadI32(image, 132);
        var dy = ReadI32(image, 136);
        var wheel = ReadI32(image, 140);
        var absolute = (Flags(image) & 1) != 0;
        // Bit 2: dx and dy are device counts already (a turn step). Not scaled.
        var counts = (Flags(image) & 4) != 0;
        var dpi = ReadU32(image, 152);
        var mouseTouch = false;
        if (!absolute && !counts)
        {
            if (dpi == 0)
                dpi = 96;
            dx = Scale(dx, dpi);
            dy = Scale(dy, dpi);
        }
        wheel = wheel == int.MinValue ? int.MaxValue : -wheel;

        var downs = Count(image, 0);
        for (var i = 0; i < downs; i++)
        {
            var id = Swap(Id(image, 4, i), swapClicks);
            var bit = MouseBit(id);
            if (bit >= 0)
            {
                bits |= 1 << bit;
                mouseTouch = true;
                continue;
            }
            var usage = HidUsage(id);
            if (usage == 0)
                continue;
            ops.Add(new BoardOp(Line("K {0} 1", usage), false, 0, 0, 0));
        }

        if (absolute)
        {
            ops.Add(new BoardOp(null, true, bits, ReadI32(image, 144), ReadI32(image, 148)));
            mouseTouch = false;
            dx = 0;
            dy = 0;
        }
        else if (mouseTouch || dx != 0 || dy != 0 || wheel != 0)
        {
            ops.Add(new BoardOp(Move(bits, dx, dy, wheel), false, bits, 0, 0));
            dx = 0;
            dy = 0;
            wheel = 0;
            mouseTouch = false;
        }

        var ups = Count(image, 1);
        for (var i = 0; i < ups; i++)
        {
            var id = Swap(Id(image, 68, i), swapClicks);
            var bit = MouseBit(id);
            if (bit >= 0)
            {
                bits &= ~(1 << bit);
                mouseTouch = true;
                continue;
            }
            var usage = HidUsage(id);
            if (usage == 0)
                continue;
            ops.Add(new BoardOp(Line("K {0} 0", usage), false, 0, 0, 0));
        }

        if (wheel != 0)
            ops.Add(new BoardOp(Move(bits, 0, 0, wheel), false, bits, 0, 0));
        if (mouseTouch)
            ops.Add(new BoardOp(Move(bits, 0, 0, 0), false, bits, 0, 0));
        return bits;
    }

    static int Count(ReadOnlySpan<byte> image, int offset)
    {
        var count = image[offset];
        return count > Cap ? Cap : count;
    }

    static ushort Id(ReadOnlySpan<byte> image, int origin, int index)
    {
        return BinaryPrimitives.ReadUInt16LittleEndian(image.Slice(origin + (index * 2)));
    }

    static ushort Flags(ReadOnlySpan<byte> image)
    {
        return BinaryPrimitives.ReadUInt16LittleEndian(image.Slice(2));
    }

    static int ReadI32(ReadOnlySpan<byte> image, int offset)
    {
        return BinaryPrimitives.ReadInt32LittleEndian(image.Slice(offset));
    }

    static uint ReadU32(ReadOnlySpan<byte> image, int offset)
    {
        return BinaryPrimitives.ReadUInt32LittleEndian(image.Slice(offset));
    }

    static ushort Swap(ushort id, bool on)
    {
        if (!on)
            return id;
        if (id == 257)
            return 258;
        if (id == 258)
            return 257;
        return id;
    }

    static int MouseBit(ushort id)
    {
        if (id < 257 || id > 261)
            return -1;
        return id - 257;
    }

    /* The name AHK sends to the board for a native key id, then HidMap, so
       both engines press the same HID usage. Ids are engine_key_id in
       engine-native/src/core/runner.c. An id with no name is 0. */
    static int HidUsage(ushort id)
    {
        var name = BoardName(id);
        if (name == null)
            return 0;
        return HidMap.KeyUsage(name, out var usage) ? usage : 0;
    }

    static string? BoardName(ushort id)
    {
        if (id is >= 257 and <= 261)
            return null;
        if (id == 256)
            return "numpadenter";
        if (id is >= 0x41 and <= 0x5A)
            return ((char)('a' + (id - 0x41))).ToString();
        if (id is >= 0x30 and <= 0x39)
            return ((char)('0' + (id - 0x30))).ToString();
        if (id is >= 0x70 and <= 0x87)
            return "f" + (id - 0x70 + 1).ToString(CultureInfo.InvariantCulture);
        return id switch
        {
            0x08 => "backspace",
            0x09 => "tab",
            0x0D => "enter",
            0x10 or 0xA0 => "lshift",
            0x11 or 0xA2 => "lctrl",
            0x12 or 0xA4 => "lalt",
            0x14 => "capslock",
            0x1B => "escape",
            0x20 => "space",
            0x25 => "left",
            0x26 => "up",
            0x27 => "right",
            0x28 => "down",
            0x2D => "insert",
            0x2E => "delete",
            0x5B => "lwin",
            0x5C => "rwin",
            0xA1 => "rshift",
            0xA3 => "rctrl",
            0xA5 => "ralt",
            0xBA => ";",
            0xBB => "=",
            0xBC => ",",
            0xBD => "-",
            0xBE => ".",
            0xBF => "/",
            0xC0 => "`",
            0xDB => "[",
            0xDC => "\\",
            0xDD => "]",
            0xDE => "'",
            _ => null,
        };
    }

    static int Scale(int value, uint dpi)
    {
        if (dpi == 0 || dpi == 96)
            return value;
        var scaled = (double)value * dpi / 96.0;
        if (double.IsNaN(scaled) || double.IsInfinity(scaled))
            return 0;
        var shifted = scaled >= 0 ? Math.Floor(scaled + 0.5) : Math.Ceiling(scaled - 0.5);
        if (shifted >= int.MaxValue)
            return int.MaxValue;
        if (shifted <= int.MinValue)
            return int.MinValue;
        return (int)shifted;
    }

    static string Line(string format, int usage)
    {
        return string.Format(CultureInfo.InvariantCulture, format, usage);
    }

    static string Move(int bits, int dx, int dy, int wheel)
    {
        return string.Format(CultureInfo.InvariantCulture, "M {0} {1} {2} {3}", bits, dx, dy, wheel);
    }
}
