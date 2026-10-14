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

        if (fails != 0)
        {
            Console.Error.WriteLine(fails + " failed");
            return 1;
        }
        Console.WriteLine("ok");
        return 0;
    }
}
