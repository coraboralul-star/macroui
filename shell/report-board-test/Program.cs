using System.Buffers.Binary;

namespace MacroShell;

static class ReportBoardTest
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

    static byte[] Image()
    {
        return new byte[ReportBoard.Bytes];
    }

    static void Down(byte[] image, params ushort[] ids)
    {
        image[0] = (byte)ids.Length;
        for (var i = 0; i < ids.Length; i++)
            BinaryPrimitives.WriteUInt16LittleEndian(image.AsSpan(4 + (i * 2)), ids[i]);
    }

    static void Up(byte[] image, ushort id)
    {
        image[1] = 1;
        BinaryPrimitives.WriteUInt16LittleEndian(image.AsSpan(68), id);
    }

    static void Rel(byte[] image, int dx, int dy, int wheel, uint dpi)
    {
        BinaryPrimitives.WriteInt32LittleEndian(image.AsSpan(132), dx);
        BinaryPrimitives.WriteInt32LittleEndian(image.AsSpan(136), dy);
        BinaryPrimitives.WriteInt32LittleEndian(image.AsSpan(140), wheel);
        BinaryPrimitives.WriteUInt32LittleEndian(image.AsSpan(152), dpi);
    }

    static void Abs(byte[] image, int ax, int ay, int wheel, uint dpi)
    {
        BinaryPrimitives.WriteUInt16LittleEndian(image.AsSpan(2), 1);
        BinaryPrimitives.WriteInt32LittleEndian(image.AsSpan(144), ax);
        BinaryPrimitives.WriteInt32LittleEndian(image.AsSpan(148), ay);
        BinaryPrimitives.WriteInt32LittleEndian(image.AsSpan(140), wheel);
        BinaryPrimitives.WriteUInt32LittleEndian(image.AsSpan(152), dpi);
    }

    static List<string> Lines(byte[] image, bool swap, int bits, out int next)
    {
        var ops = new List<BoardOp>();
        next = ReportBoard.Apply(image, swap, bits, ops);
        var lines = new List<string>();
        foreach (var op in ops)
        {
            if (op.Steer)
                lines.Add(string.Create(System.Globalization.CultureInfo.InvariantCulture, $"steer {op.Bits} {op.Ax} {op.Ay}"));
            else if (op.Line != null)
                lines.Add(op.Line);
        }
        return lines;
    }

    /* Every key id engine_key_id can compile (map_name in runner.c), with the
       name AHK sends to the board for it. The native board line must use the
       same HID usage as AHK's HidMap lookup of that name. */
    static readonly (ushort Id, string Name)[] NativeKeys =
    {
        (0x08, "backspace"), (0x09, "tab"), (0x0D, "enter"), (0x14, "CapsLock"), (0x1B, "escape"),
        (0x20, "space"), (0x25, "left"), (0x26, "up"), (0x27, "right"), (0x28, "down"),
        (0x2D, "Insert"), (0x2E, "Delete"),
        (0x5B, "LWin"), (0x5C, "RWin"),
        (0xA0, "LShift"), (0xA1, "RShift"), (0xA2, "LCtrl"), (0xA3, "RCtrl"), (0xA4, "LAlt"), (0xA5, "RAlt"),
        (0xBA, ";"), (0xBB, "="), (0xBC, ","), (0xBD, "-"), (0xBE, "."), (0xBF, "/"), (0xC0, "`"),
        (0xDB, "["), (0xDC, "\\"), (0xDD, "]"), (0xDE, "'"),
        (256, "numpadenter"),
    };

    static void CheckEveryNativeKey()
    {
        var keys = new List<(ushort Id, string Name)>(NativeKeys);
        for (var c = 'a'; c <= 'z'; c++)
            keys.Add(((ushort)(0x41 + (c - 'a')), c.ToString()));
        for (var c = '0'; c <= '9'; c++)
            keys.Add(((ushort)(0x30 + (c - '0')), c.ToString()));
        for (var n = 1; n <= 12; n++)
            keys.Add(((ushort)(0x70 + n - 1), "f" + n));
        foreach (var (id, name) in keys)
        {
            Expect(HidMap.KeyUsage(name, out var usage), "AHK has a board usage for " + name);
            var down = Image();
            Down(down, id);
            var lines = Lines(down, false, 0, out _);
            Expect(lines.Count == 1 && lines[0] == "K " + usage + " 1",
                   "native board down for " + name + " matches AHK");
            var up = Image();
            Up(up, id);
            lines = Lines(up, false, 0, out _);
            Expect(lines.Count == 1 && lines[0] == "K " + usage + " 0",
                   "native board up for " + name + " matches AHK");
        }
    }

    static int Main()
    {
        var relative = Image();
        Down(relative, 0x20);
        Rel(relative, 4, -3, 1, 96);
        var lines = Lines(relative, false, 0, out var bits);
        Expect(bits == 0 && lines.Count == 2, "space and a move");
        Expect(lines[0] == "K 44 1" && lines[1] == "M 0 4 -3 -1", "report-relative lines");

        var scaled = Image();
        Down(scaled, 0x20);
        Rel(scaled, 1, -1, 1, 144);
        lines = Lines(scaled, false, 0, out _);
        Expect(lines[1] == "M 0 2 -2 -1", "dpi 144 scales half away");

        var dpi0 = Image();
        Rel(dpi0, 4, -3, 0, 0);
        lines = Lines(dpi0, false, 0, out _);
        Expect(lines.Count == 1 && lines[0] == "M 0 4 -3 0", "dpi 0 is 96");

        var three = Image();
        Down(three, 257, 0x57, 0x20);
        Rel(three, 4, -3, 0, 96);
        lines = Lines(three, false, 0, out bits);
        Expect(bits == 1 && lines.Count == 3, "three downs");
        Expect(lines[0] == "K 26 1" && lines[1] == "K 44 1" && lines[2] == "M 1 4 -3 0", "w, space, then M");

        var absolute = Image();
        Down(absolute, 257);
        Abs(absolute, 10, 20, 0, 120);
        lines = Lines(absolute, false, 0, out bits);
        Expect(bits == 1 && lines.Count == 1 && lines[0] == "steer 1 10 20", "absolute is steer");

        var absoluteWheel = Image();
        Down(absoluteWheel, 257);
        Abs(absoluteWheel, 10, 20, 1, 96);
        lines = Lines(absoluteWheel, false, 0, out _);
        Expect(lines.Count == 2 && lines[0] == "steer 1 10 20" && lines[1] == "M 1 0 0 -1", "absolute leaves the wheel");

        var buttonUp = Image();
        Up(buttonUp, 257);
        lines = Lines(buttonUp, false, 1, out bits);
        Expect(bits == 0 && lines.Count == 1 && lines[0] == "M 0 0 0 0", "button up sends the cleared bits");

        var swapped = Image();
        Down(swapped, 257);
        lines = Lines(swapped, true, 0, out bits);
        Expect(bits == 2 && lines.Count == 1 && lines[0] == "M 2 0 0 0", "swap left sends the right bit");

        var middle = Image();
        Down(middle, 259);
        lines = Lines(middle, true, 0, out bits);
        Expect(bits == 4 && lines[0] == "M 4 0 0 0", "swap leaves the middle bit");

        var unnamed = Image();
        Down(unnamed, 1);
        lines = Lines(unnamed, false, 7, out bits);
        Expect(lines.Count == 0 && bits == 7, "an unnamed id writes no line");

        var numpad = Image();
        Down(numpad, 256);
        lines = Lines(numpad, false, 0, out _);
        Expect(lines.Count == 1 && lines[0] == "K 88 1", "numpad enter");

        var empty = Image();
        Expect(!ReportBoard.Active(empty), "zeros are not sent");
        lines = Lines(empty, false, 0, out bits);
        Expect(lines.Count == 0 && bits == 0, "an empty image writes nothing");

        var wheel = Image();
        Rel(wheel, 0, 0, 2, 96);
        lines = Lines(wheel, false, 0, out _);
        Expect(lines.Count == 1 && lines[0] == "M 0 0 0 -2", "wheel only");

        var click = Image();
        Down(click, 257);
        Up(click, 257);
        lines = Lines(click, false, 0, out bits);
        Expect(bits == 0 && lines.Count == 2 && lines[0] == "M 1 0 0 0" && lines[1] == "M 0 0 0 0", "click down then up");

        CheckEveryNativeKey();

        if (fails != 0)
        {
            Console.Error.WriteLine(fails + " failed");
            return 1;
        }
        Console.WriteLine("ok");
        return 0;
    }
}
