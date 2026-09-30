using System.Runtime.InteropServices;
using Microsoft.Win32.SafeHandles;

namespace MacroShell;

// COM ports whose USB product string is Vendetta RP2040 or Vendetta RP2350.
// The CDC interface string is "Vendetta Control", so we walk to the parent
// composite device to read the real product name.
static class BoardPorts
{
    public readonly record struct Device(string Port, string Chip, string Name);

    public static Device[] List()
    {
        var found = new List<Device>();
        var set = SetupDiGetClassDevs(ref PortsClass, IntPtr.Zero, IntPtr.Zero, DigcfPresent);
        if (set == Invalid)
            return [];

        try
        {
            var info = new SpDevinfoData { cbSize = Marshal.SizeOf<SpDevinfoData>() };
            for (uint i = 0; SetupDiEnumDeviceInfo(set, i, ref info); i++)
            {
                var port = ReadPortName(set, ref info);
                if (string.IsNullOrEmpty(port))
                    continue;
                if (!TryProduct(info.DevInst, out var chip, out var name))
                    continue;
                found.Add(new Device(port, chip, name));
            }
        }
        finally
        {
            SetupDiDestroyDeviceInfoList(set);
        }

        found.Sort((a, b) => string.CompareOrdinal(a.Port, b.Port));
        return found.ToArray();
    }

    static bool TryProduct(uint inst, out string chip, out string name)
    {
        chip = "";
        name = "";
        var walk = inst;
        for (var depth = 0; depth < 8; depth++)
        {
            var text = BusReportedName(walk);
            if (text.StartsWith("Vendetta RP2040", StringComparison.OrdinalIgnoreCase))
            {
                chip = "rp2040";
                name = text;
                return true;
            }
            if (text.StartsWith("Vendetta RP2350", StringComparison.OrdinalIgnoreCase))
            {
                chip = "rp2350";
                name = text;
                return true;
            }
            if (CmGetParent(out var parent, walk, 0) != 0)
                break;
            if (parent == 0 || parent == walk)
                break;
            walk = parent;
        }
        return false;
    }

    static string ReadPortName(IntPtr set, ref SpDevinfoData info)
    {
        var key = SetupDiOpenDevRegKey(set, ref info, DicsFlagGlobal, 0, DiregDev, KeyRead);
        if (key == Invalid)
            return "";
        try
        {
            using var handle = new SafeRegistryHandle(key, ownsHandle: true);
            using var reg = Microsoft.Win32.RegistryKey.FromHandle(handle);
            return reg.GetValue("PortName") as string ?? "";
        }
        catch
        {
            return "";
        }
    }

    static string BusReportedName(uint inst)
    {
        var key = BusReportedKey;
        uint type = 0;
        uint size = 0;
        CmGetDevNodeProperty(inst, ref key, out type, IntPtr.Zero, ref size, 0);
        if (size == 0)
            return "";
        var buf = Marshal.AllocHGlobal((int)size);
        try
        {
            if (CmGetDevNodeProperty(inst, ref key, out type, buf, ref size, 0) != 0)
                return "";
            return Marshal.PtrToStringUni(buf)?.Trim() ?? "";
        }
        finally
        {
            Marshal.FreeHGlobal(buf);
        }
    }

    static Guid PortsClass = new("4d36e978-e325-11ce-bfc1-08002be10318");
    static DevpropKey BusReportedKey = new()
    {
        fmtid = new Guid("540b947e-8b40-45bc-a8a2-6a0b894cbda2"),
        pid = 4,
    };

    const uint DigcfPresent = 0x2;
    const uint DicsFlagGlobal = 0x1;
    const uint DiregDev = 0x1;
    const uint KeyRead = 0x20019;
    static readonly IntPtr Invalid = new(-1);

    [StructLayout(LayoutKind.Sequential)]
    struct SpDevinfoData
    {
        public int cbSize;
        public Guid ClassGuid;
        public uint DevInst;
        public IntPtr Reserved;
    }

    [StructLayout(LayoutKind.Sequential)]
    struct DevpropKey
    {
        public Guid fmtid;
        public uint pid;
    }

    [DllImport("setupapi.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    static extern IntPtr SetupDiGetClassDevs(ref Guid classGuid, IntPtr enumerator, IntPtr parent, uint flags);

    [DllImport("setupapi.dll", SetLastError = true)]
    static extern bool SetupDiEnumDeviceInfo(IntPtr set, uint index, ref SpDevinfoData info);

    [DllImport("setupapi.dll", SetLastError = true)]
    static extern bool SetupDiDestroyDeviceInfoList(IntPtr set);

    [DllImport("setupapi.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    static extern IntPtr SetupDiOpenDevRegKey(IntPtr set, ref SpDevinfoData info, uint scope, uint profile, uint keyType, uint sam);

    [DllImport("cfgmgr32.dll", EntryPoint = "CM_Get_Parent")]
    static extern int CmGetParent(out uint parent, uint inst, uint flags);

    [DllImport("cfgmgr32.dll", CharSet = CharSet.Unicode, EntryPoint = "CM_Get_DevNode_PropertyW")]
    static extern int CmGetDevNodeProperty(uint inst, ref DevpropKey key, out uint type, IntPtr buf, ref uint size, uint flags);
}
