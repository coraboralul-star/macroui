using System.Runtime.InteropServices;
using System.Text.Json.Nodes;
using System.Windows.Interop;

namespace MacroShell;

/* Turn calibration (#80). The page sends {type:"turn", action:"calibrate"|"test"|"cancel"}.
   While it is on, a low-level hook swallows the chosen key or mouse button (a
   keyboard hook, or a mouse hook for middle, M4 and M5), so the game never sees
   it, and Raw Input reads the physical mouse. The page pauses macros meanwhile. The page hears
   {type:"turn", state:"armed"|"turning"|"done"|"short"|"testing"|"sent"|"off"|"error"}. */
public partial class MainWindow
{
    readonly TurnCalibrator _turn = new();
    readonly Dictionary<IntPtr, bool> _turnBoardDevice = new();
    // Set and read on the hook thread only, except the thread and its id.
    IntPtr _turnHook;
    Thread? _turnHookThread;
    uint _turnHookThreadId;
    LowLevelKeyboardProc? _turnHookProc;
    LowLevelKeyboardProc? _turnMouseProc;
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
        if (vk is < 1 or > 254 || !TurnCalibrator.Usable(vk))
        {
            SendTurn("error", detail: "Pick a key or mouse button for calibration. Left and right click can't be used.");
            return;
        }
        // A new start, or the same test with nudged counts. The hook is for this vk.
        StopTurn();
        if (action == "calibrate")
        {
            if (!StartTurnInput(raw: true, vk))
                return;
            lock (_turn)
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
            if (!StartTurnInput(raw: false, vk))
                return;
            lock (_turn)
                _turn.Test(vk, counts);
            SendTurn("testing", counts);
        }
    }

    bool StartTurnInput(bool raw, int vk)
    {
        if (_turnHookThread == null && !StartTurnHook(TurnCalibrator.IsMouse(vk)))
        {
            SendTurn("error", detail: "The calibration button could not be watched.");
            return false;
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

    /* The hook gets its own thread and message loop. A mouse hook sees every
       move, so on the UI thread any stall there would lag the cursor in game. */
    bool StartTurnHook(bool mouse)
    {
        var ready = new ManualResetEventSlim();
        var ok = false;
        var thread = new Thread(() =>
        {
            _turnHookThreadId = GetCurrentThreadId();
            _turnHookProc = TurnHook;
            _turnMouseProc = TurnMouseHook;
            _turnHook = SetWindowsHookEx(mouse ? 14 : 13, mouse ? _turnMouseProc : _turnHookProc, GetModuleHandle(null), 0);
            ok = _turnHook != IntPtr.Zero;
            ready.Set();
            if (!ok)
                return;
            while (GetMessage(out _, IntPtr.Zero, 0, 0) > 0)
            {
            }
            UnhookWindowsHookEx(_turnHook);
            _turnHook = IntPtr.Zero;
        })
        { IsBackground = true, Name = "turn hook" };
        thread.Start();
        ready.Wait(2000);
        if (!ok)
            return false;
        _turnHookThread = thread;
        return true;
    }

    void StopTurn()
    {
        lock (_turn)
            _turn.Cancel();
        if (_turnRaw)
        {
            var dev = new RAWINPUTDEVICE { UsagePage = 1, Usage = 2, Flags = 0x1u, Target = IntPtr.Zero };
            RegisterRawInputDevices(new[] { dev }, 1, (uint)Marshal.SizeOf<RAWINPUTDEVICE>());
            _turnRaw = false;
        }
        if (_turnHookThread != null)
        {
            PostThreadMessage(_turnHookThreadId, 0x0012, IntPtr.Zero, IntPtr.Zero);
            _turnHookThread.Join(1000);
            _turnHookThread = null;
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

    /* The hook thread. Injected keys are the engines' own output, not a press. */
    IntPtr TurnHook(int code, IntPtr wParam, IntPtr lParam)
    {
        if (code >= 0)
        {
            var info = Marshal.PtrToStructure<KBDLLHOOKSTRUCT>(lParam);
            var msg = (int)wParam;
            var down = msg is 0x100 or 0x104;
            var up = msg is 0x101 or 0x105;
            if ((down || up) && (info.Flags & 0x10) == 0 && TurnPress((int)info.VkCode, down))
                return 1;
        }
        return CallNextHookEx(_turnHook, code, wParam, lParam);
    }

    /* Middle, M4 and M5. Moves and other buttons pass straight through. */
    IntPtr TurnMouseHook(int code, IntPtr wParam, IntPtr lParam)
    {
        if (code >= 0)
        {
            var info = Marshal.PtrToStructure<MSLLHOOKSTRUCT>(lParam);
            var press = TurnCalibrator.MouseButton((int)wParam, info.MouseData);
            if (press.Vk != 0 && (info.Flags & 0x1) == 0 && TurnPress(press.Vk, press.Down))
                return 1;
        }
        return CallNextHookEx(_turnHook, code, wParam, lParam);
    }

    /* true swallows it. The page hears the press on the UI thread. */
    bool TurnPress(int vk, bool down)
    {
        string what;
        lock (_turn)
        {
            if (!_turn.Swallows(vk))
                return false;
            what = _turn.Key(vk, down);
        }
        if (what != "")
            Dispatcher.BeginInvoke(() => OnTurnPress(what));
        return true;
    }

    void OnTurnPress(string what)
    {
        int counts;
        int test;
        lock (_turn)
        {
            counts = _turn.Counts;
            test = _turn.TestCounts;
        }
        if (what == "turning")
            SendTurn("turning");
        else if (what == "short")
            SendTurn("short", counts);
        else if (what == "done")
        {
            StopTurn();
            SendTurn("done", counts);
        }
        else if (what == "test")
        {
            SendTurnCounts(test);
            SendTurn("sent", test);
        }
    }

    /* WM_INPUT while calibrating. */
    void OnTurnRawInput(IntPtr lParam)
    {
        lock (_turn)
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
            var board = TurnIsBoard(header.Device);
            lock (_turn)
                _turn.Raw(mouse.LastX, (mouse.Flags & 1) != 0, header.Device == IntPtr.Zero, board);
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
    struct MSLLHOOKSTRUCT
    {
        public int X;
        public int Y;
        public uint MouseData;
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

    [StructLayout(LayoutKind.Sequential)]
    struct MSG
    {
        public IntPtr Hwnd;
        public uint Message;
        public IntPtr WParam;
        public IntPtr LParam;
        public uint Time;
        public int X;
        public int Y;
    }

    [DllImport("user32.dll")]
    static extern int GetMessage(out MSG msg, IntPtr hwnd, uint min, uint max);

    [DllImport("user32.dll")]
    static extern bool PostThreadMessage(uint threadId, uint msg, IntPtr wParam, IntPtr lParam);

    [DllImport("kernel32.dll")]
    static extern uint GetCurrentThreadId();

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
