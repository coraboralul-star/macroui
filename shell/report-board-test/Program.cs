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

    /* F10 is 0x79. Press, turn, press. Only the hand on the physical mouse counts. */
    static void CheckTurnCalibrator()
    {
        var cal = new TurnCalibrator();
        Expect(!cal.Swallows(0x79), "off swallows nothing");
        cal.Arm(0x79);
        Expect(cal.Swallows(0x79) && !cal.Swallows(0x78), "armed swallows only its key");
        cal.Raw(500, false, false, false);
        Expect(cal.Key(0x79, true) == "turning" && cal.Counts == 0, "the first press starts at 0");
        Expect(cal.Key(0x79, true) == "", "a held key's repeat is not a press");
        cal.Key(0x79, false);
        cal.Raw(8000, false, false, false);
        cal.Raw(-500, false, false, false);
        cal.Raw(9000, false, false, false);
        cal.Raw(1000, false, true, false);
        cal.Raw(1000, false, false, true);
        cal.Raw(1000, true, false, false);
        Expect(cal.Counts == 16500, "injected, board and absolute moves are not counted");
        Expect(cal.Key(0x79, true) == "done" && cal.State == TurnCalibrator.Phase.Off && cal.Counts == 16500,
            "the second press is the result");

        cal.Arm(0x79);
        cal.Key(0x79, true);
        cal.Key(0x79, false);
        cal.Raw(-30, false, false, false);
        Expect(cal.Key(0x79, true) == "short" && cal.State == TurnCalibrator.Phase.Armed, "a tiny turn asks again");
        cal.Key(0x79, false);
        cal.Key(0x79, true);
        cal.Key(0x79, false);
        cal.Raw(-12000, false, false, false);
        Expect(cal.Key(0x79, true) == "done" && cal.Counts == 12000, "a left turn counts as positive");

        cal.Test(0x7A, 40000);
        Expect(cal.Key(0x7A, true) == "test" && cal.Key(0x7A, true) == "", "test sends once per press");
        cal.Key(0x7A, false);
        Expect(cal.Key(0x7A, true) == "test", "the next press tests again");
        cal.Cancel();
        Expect(!cal.Swallows(0x7A), "cancel lets the key through");

        Expect(TurnCalibrator.MouseButton(0x20B, 2u << 16) == (0x06, true) && TurnCalibrator.MouseButton(0x20C, 2u << 16) == (0x06, false),
            "M5 down and up are VK_XBUTTON2");
        Expect(TurnCalibrator.MouseButton(0x20B, 1u << 16) == (0x05, true) && TurnCalibrator.MouseButton(0x207, 0) == (0x04, true),
            "M4 and middle map to their vks");
        Expect(TurnCalibrator.MouseButton(0x201, 0).Vk == 0 && TurnCalibrator.MouseButton(0x200, 0).Vk == 0,
            "left click and moves are not watched");
        Expect(!TurnCalibrator.Usable(0x01) && !TurnCalibrator.Usable(0x02) && TurnCalibrator.Usable(0x06) && TurnCalibrator.IsMouse(0x06) && !TurnCalibrator.IsMouse(0x79),
            "left and right click are refused, M5 uses the mouse hook");
        cal.Arm(0x06);
        var m5 = TurnCalibrator.MouseButton(0x20B, 2u << 16);
        Expect(cal.Swallows(m5.Vk) && cal.Key(m5.Vk, m5.Down) == "turning", "M5 starts a calibration");

        var pieces = TurnCalibrator.Pieces(40000);
        Expect(pieces.Count == 3 && pieces[0] == 16000 && pieces[1] == 16000 && pieces[2] == 8000, "a test turn goes out in 16000 pieces");
        Expect(TurnCalibrator.Pieces(-100).Count == 1 && TurnCalibrator.Pieces(-100)[0] == -100, "pieces keep the sign");
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

        var counts = Image();
        Rel(counts, 315, -3, 0, 144);
        BinaryPrimitives.WriteUInt16LittleEndian(counts.AsSpan(2), 4);
        lines = Lines(counts, false, 0, out _);
        Expect(lines.Count == 1 && lines[0] == "M 0 315 -3 0", "a counts report is not scaled by dpi");

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
        CheckTurnCalibrator();

        if (fails != 0)
        {
            Console.Error.WriteLine(fails + " failed");
            return 1;
        }
        Console.WriteLine("ok");
        return 0;
    }
}
