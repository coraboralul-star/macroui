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
        var dpi = ReadU32(image, 152);
        var mouseTouch = false;
        if (!absolute)
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

    /* Names from engine_key_name, usages from HidMap. An omitted name is 0. */
    static int HidUsage(ushort id)
    {
        if (id is >= 257 and <= 261)
            return 0;
        if (id == 256)
            return 0x58;
        if (id is >= 0x41 and <= 0x5A)
            return 0x04 + (id - 0x41);
        if (id is >= 0x31 and <= 0x39)
            return 0x1E + (id - 0x31);
        if (id == 0x30)
            return 0x27;
        if (id is >= 0x70 and <= 0x7B)
            return 0x3A + (id - 0x70);
        return id switch
        {
            0x0D => 0x28,
            0x10 or 0xA0 => 0xE1,
            0x11 or 0xA2 => 0xE0,
            0x12 or 0xA4 => 0xE2,
            0x20 => 0x2C,
            0x5B => 0xE3,
            0x5C => 0xE7,
            0xA1 => 0xE5,
            0xA3 => 0xE4,
            0xA5 => 0xE6,
            _ => 0,
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
