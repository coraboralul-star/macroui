using System.Runtime.InteropServices;
using System.Text.Json.Nodes;
using System.Windows.Interop;

namespace MacroShell;

/* Turn calibration (#80). The page sends {type:"turn", action:"calibrate"|"test"|"cancel"}.
   While it is on, a low-level keyboard hook swallows the chosen key, so the game
   never sees it, and Raw Input reads the physical mouse. The page hears
   {type:"turn", state:"armed"|"turning"|"done"|"short"|"testing"|"sent"|"off"|"error"}. */
public partial class MainWindow
{
    readonly TurnCalibrator _turn = new();
    readonly Dictionary<IntPtr, bool> _turnBoardDevice = new();
    IntPtr _turnHook;
    LowLevelKeyboardProc? _turnHookProc;
    bool _turnRaw;

    void OnTurnMessage(JsonNode? node)
    {
        var action = node?["action"]?.ToString();
        var vk = AsInt(node?["vk"]);
        if (action == "cancel")
        {
            StopTurn();
            SendTurn("off");
            return;
        }
        if (vk is < 1 or > 254)
        {
            SendTurn("error", detail: "Pick a key for calibration.");
            return;
        }
        if (action == "calibrate")
        {
            if (!StartTurnInput(raw: true))
                return;
            _turn.Arm(vk);
            SendTurn("armed");
        }
        else if (action == "test")
        {
            var counts = AsInt(node?["counts"]);
            if (counts < TurnCalibrator.MinCounts)
            {
                SendTurn("error", detail: "Calibrate first, or enter the counts for one full turn.");
                return;
            }
            if (!StartTurnInput(raw: false))
                return;
            _turn.Test(vk, counts);
            SendTurn("testing", counts);
        }
    }

    bool StartTurnInput(bool raw)
    {
        if (_turnHook == IntPtr.Zero)
        {
            _turnHookProc = TurnHook;
            _turnHook = SetWindowsHookEx(13, _turnHookProc, GetModuleHandle(null), 0);
            if (_turnHook == IntPtr.Zero)
            {
                SendTurn("error", detail: "The calibration key could not be watched.");
                return false;
            }
        }
        if (raw != _turnRaw)
        {
            var hwnd = new WindowInteropHelper(this).Handle;
            var dev = new RAWINPUTDEVICE { UsagePage = 1, Usage = 2, Flags = raw ? 0x100u : 0x1u, Target = raw ? hwnd : IntPtr.Zero };
            if (!RegisterRawInputDevices(new[] { dev }, 1, (uint)Marshal.SizeOf<RAWINPUTDEVICE>()) && raw)
            {
                SendTurn("error", detail: "The mouse could not be read for calibration.");
                return false;
            }
            _turnRaw = raw;
        }
        return true;
    }

    void StopTurn()
    {
        _turn.Cancel();
        if (_turnRaw)
        {
            var dev = new RAWINPUTDEVICE { UsagePage = 1, Usage = 2, Flags = 0x1u, Target = IntPtr.Zero };
            RegisterRawInputDevices(new[] { dev }, 1, (uint)Marshal.SizeOf<RAWINPUTDEVICE>());
            _turnRaw = false;
        }
        if (_turnHook != IntPtr.Zero)
        {
            UnhookWindowsHookEx(_turnHook);
            _turnHook = IntPtr.Zero;
        }
    }

    void SendTurn(string state, int? counts = null, string? detail = null)
    {
        var json = new JsonObject { ["type"] = "turn", ["state"] = state };
        if (counts != null)
            json["counts"] = counts.Value;
        if (detail != null)
            json["detail"] = detail;
        SendToPage(json.ToJsonString());
    }

    /* The UI thread. Injected keys are the engines' own output, not a press. */
    IntPtr TurnHook(int code, IntPtr wParam, IntPtr lParam)
    {
        if (code >= 0)
        {
            var info = Marshal.PtrToStructure<KBDLLHOOKSTRUCT>(lParam);
            var msg = (int)wParam;
            var down = msg is 0x100 or 0x104;
            var up = msg is 0x101 or 0x105;
            if ((down || up) && (info.Flags & 0x10) == 0 && _turn.Swallows((int)info.VkCode))
            {
                var what = _turn.Key((int)info.VkCode, down);
                if (what != "")
                    Dispatcher.BeginInvoke(() => OnTurnPress(what));
                return 1;
            }
        }
        return CallNextHookEx(_turnHook, code, wParam, lParam);
    }

    void OnTurnPress(string what)
    {
        if (what == "turning")
            SendTurn("turning");
        else if (what == "short")
            SendTurn("short", _turn.Counts);
        else if (what == "done")
        {
            var counts = _turn.Counts;
            StopTurn();
            SendTurn("done", counts);
        }
        else if (what == "test")
        {
            SendTurnCounts(_turn.TestCounts);
            SendTurn("sent", _turn.TestCounts);
        }
    }

    /* WM_INPUT while calibrating. */
    void OnTurnRawInput(IntPtr lParam)
    {
        if (_turn.State != TurnCalibrator.Phase.Turning)
            return;
        uint size = 0;
        var head = (uint)Marshal.SizeOf<RAWINPUTHEADER>();
        if (GetRawInputData(lParam, 0x10000003, IntPtr.Zero, ref size, head) != 0 || size == 0)
            return;
        var buf = Marshal.AllocHGlobal((int)size);
        try
        {
            if (GetRawInputData(lParam, 0x10000003, buf, ref size, head) != size)
                return;
            var header = Marshal.PtrToStructure<RAWINPUTHEADER>(buf);
            if (header.Type != 0)
                return;
            var mouse = Marshal.PtrToStructure<RAWMOUSE>(buf + (int)head);
            _turn.Raw(mouse.LastX, (mouse.Flags & 1) != 0, header.Device == IntPtr.Zero, TurnIsBoard(header.Device));
        }
        finally
        {
            Marshal.FreeHGlobal(buf);
        }
    }

    /* The MacroUI board, VID_CAFE&PID_4004, like the engines' Raw Input check. */
    bool TurnIsBoard(IntPtr device)
    {
        if (device == IntPtr.Zero)
            return false;
        if (_turnBoardDevice.TryGetValue(device, out var known))
            return known;
        uint chars = 0;
        var board = false;
        if (GetRawInputDeviceInfo(device, 0x20000007, IntPtr.Zero, ref chars) == 0 && chars > 0)
        {
            var buf = Marshal.AllocHGlobal((int)chars * 2);
            try
            {
                if (GetRawInputDeviceInfo(device, 0x20000007, buf, ref chars) != unchecked((uint)-1))
                    board = (Marshal.PtrToStringUni(buf) ?? "").Contains("VID_CAFE&PID_4004", StringComparison.OrdinalIgnoreCase);
            }
            finally
            {
                Marshal.FreeHGlobal(buf);
            }
        }
        _turnBoardDevice[device] = board;
        return board;
    }

    /* Test 360: counts, not pixels. The open board gets M lines and adds them up
       (127 a report). Otherwise SendInput relative moves. */
    void SendTurnCounts(int counts)
    {
        var pieces = TurnCalibrator.Pieces(counts);
        if (_inputMode is "rp2040" or "rp2350" && _board is { IsOpen: true })
        {
            if (!_boardLock.Wait(200))
                return;
            try
            {
                FlushBoardPending();
                if (_board is not { IsOpen: true })
                    return;
                _board.ReadTimeout = 80;
                foreach (var piece in pieces)
                    BoardLine(_board, $"M {_mouseBits} {piece} 0 0");
            }
            catch
            {
                CloseBoard();
            }
            finally
            {
                _boardLock.Release();
            }
            return;
        }
        var inputs = new INPUT[pieces.Count];
        for (var i = 0; i < pieces.Count; i++)
            inputs[i] = new INPUT { Type = 0, Mouse = new MOUSEINPUT { Dx = pieces[i], Flags = 0x0001 } };
        if (inputs.Length > 0)
            SendInput((uint)inputs.Length, inputs, Marshal.SizeOf<INPUT>());
    }

    delegate IntPtr LowLevelKeyboardProc(int code, IntPtr wParam, IntPtr lParam);

    [StructLayout(LayoutKind.Sequential)]
    struct KBDLLHOOKSTRUCT
    {
        public uint VkCode;
        public uint ScanCode;
        public uint Flags;
        public uint Time;
        public IntPtr ExtraInfo;
    }

    [StructLayout(LayoutKind.Sequential)]
    struct RAWINPUTDEVICE
    {
        public ushort UsagePage;
        public ushort Usage;
        public uint Flags;
        public IntPtr Target;
    }

    [StructLayout(LayoutKind.Sequential)]
    struct RAWINPUTHEADER
    {
        public uint Type;
        public uint Size;
        public IntPtr Device;
        public IntPtr WParam;
    }

    [StructLayout(LayoutKind.Sequential)]
    struct RAWMOUSE
    {
        public ushort Flags;
        public ushort Pad;
        public uint Buttons;
        public uint RawButtons;
        public int LastX;
        public int LastY;
        public uint Extra;
    }

    [StructLayout(LayoutKind.Sequential)]
    struct MOUSEINPUT
    {
        public int Dx;
        public int Dy;
        public uint Data;
        public uint Flags;
        public uint Time;
        public IntPtr ExtraInfo;
    }

    [StructLayout(LayoutKind.Sequential)]
    struct INPUT
    {
        public uint Type;
        public MOUSEINPUT Mouse;
    }

    [DllImport("user32.dll", SetLastError = true)]
    static extern IntPtr SetWindowsHookEx(int idHook, LowLevelKeyboardProc proc, IntPtr module, uint threadId);

    [DllImport("user32.dll")]
    static extern bool UnhookWindowsHookEx(IntPtr hook);

    [DllImport("user32.dll")]
    static extern IntPtr CallNextHookEx(IntPtr hook, int code, IntPtr wParam, IntPtr lParam);

    [DllImport("kernel32.dll", CharSet = CharSet.Unicode)]
    static extern IntPtr GetModuleHandle(string? name);

    [DllImport("user32.dll", SetLastError = true)]
    static extern bool RegisterRawInputDevices(RAWINPUTDEVICE[] devices, uint count, uint size);

    [DllImport("user32.dll")]
    static extern uint GetRawInputData(IntPtr rawInput, uint command, IntPtr data, ref uint size, uint headerSize);

    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    static extern uint GetRawInputDeviceInfo(IntPtr device, uint command, IntPtr data, ref uint size);

    [DllImport("user32.dll", SetLastError = true)]
    static extern uint SendInput(uint count, INPUT[] inputs, int size);
}
