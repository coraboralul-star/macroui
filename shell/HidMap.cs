namespace MacroShell;

static class HidMap
{
    public static bool MouseBit(string name, out int bit)
    {
        bit = name.ToLowerInvariant() switch
        {
            "lbutton" => 0,
            "rbutton" => 1,
            "mbutton" => 2,
            "xbutton1" => 3,
            "xbutton2" => 4,
            _ => -1,
        };
        return bit >= 0;
    }

    public static string SwapClick(string name, bool on)
    {
        if (!on)
            return name;
        if (name.Equals("LButton", StringComparison.OrdinalIgnoreCase))
            return "RButton";
        if (name.Equals("RButton", StringComparison.OrdinalIgnoreCase))
            return "LButton";
        return name;
    }

    public static bool KeyUsage(string name, out byte usage)
    {
        usage = 0;
        if (string.IsNullOrEmpty(name))
            return false;
        if (Keys.TryGetValue(name, out usage))
            return usage != 0;
        if (name.Length == 1)
        {
            var ch = name[0];
            if (ch is >= 'a' and <= 'z')
            {
                usage = (byte)(0x04 + (ch - 'a'));
                return true;
            }
            if (ch is >= 'A' and <= 'Z')
            {
                usage = (byte)(0x04 + (ch - 'A'));
                return true;
            }
            if (ch is >= '1' and <= '9')
            {
                usage = (byte)(0x1E + (ch - '1'));
                return true;
            }
            if (ch == '0')
            {
                usage = 0x27;
                return true;
            }
        }
        return false;
    }

    static readonly Dictionary<string, byte> Keys = new(StringComparer.OrdinalIgnoreCase)
    {
        ["enter"] = 0x28,
        ["escape"] = 0x29,
        ["esc"] = 0x29,
        ["backspace"] = 0x2A,
        ["tab"] = 0x2B,
        ["space"] = 0x2C,
        ["-"] = 0x2D,
        ["="] = 0x2E,
        ["["] = 0x2F,
        ["]"] = 0x30,
        ["\\"] = 0x31,
        [";"] = 0x33,
        ["'"] = 0x34,
        ["`"] = 0x35,
        [","] = 0x36,
        ["."] = 0x37,
        ["/"] = 0x38,
        ["capslock"] = 0x39,
        ["f1"] = 0x3A,
        ["f2"] = 0x3B,
        ["f3"] = 0x3C,
        ["f4"] = 0x3D,
        ["f5"] = 0x3E,
        ["f6"] = 0x3F,
        ["f7"] = 0x40,
        ["f8"] = 0x41,
        ["f9"] = 0x42,
        ["f10"] = 0x43,
        ["f11"] = 0x44,
        ["f12"] = 0x45,
        ["printscreen"] = 0x46,
        ["scrolllock"] = 0x47,
        ["pause"] = 0x48,
        ["insert"] = 0x49,
        ["home"] = 0x4A,
        ["pgup"] = 0x4B,
        ["delete"] = 0x4C,
        ["end"] = 0x4D,
        ["pgdn"] = 0x4E,
        ["right"] = 0x4F,
        ["left"] = 0x50,
        ["down"] = 0x51,
        ["up"] = 0x52,
        ["numlock"] = 0x53,
        ["numpaddiv"] = 0x54,
        ["numpadmult"] = 0x55,
        ["numpadsub"] = 0x56,
        ["numpadadd"] = 0x57,
        ["numpadenter"] = 0x58,
        ["numpad1"] = 0x59,
        ["numpad2"] = 0x5A,
        ["numpad3"] = 0x5B,
        ["numpad4"] = 0x5C,
        ["numpad5"] = 0x5D,
        ["numpad6"] = 0x5E,
        ["numpad7"] = 0x5F,
        ["numpad8"] = 0x60,
        ["numpad9"] = 0x61,
        ["numpad0"] = 0x62,
        ["numpaddot"] = 0x63,
        ["appskey"] = 0x65,
        ["ctrl"] = 0xE0,
        ["lcontrol"] = 0xE0,
        ["lctrl"] = 0xE0,
        ["shift"] = 0xE1,
        ["lshift"] = 0xE1,
        ["alt"] = 0xE2,
        ["lalt"] = 0xE2,
        ["lwin"] = 0xE3,
        ["rctrl"] = 0xE4,
        ["rcontrol"] = 0xE4,
        ["rshift"] = 0xE5,
        ["ralt"] = 0xE6,
        ["rwin"] = 0xE7,
    };
}
