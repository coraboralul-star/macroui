# Vendetta USB output board

Firmware for a Raspberry Pi Pico (RP2040) or Pico 2 (RP2350) that turns the
board into a real USB keyboard and mouse.

## Why this exists

The engine normally types with AutoHotkey `Send` and moves the mouse with
`MouseMove`. Windows marks those events as injected, and some games ignore
injected input. A macro runs correctly and the game never sees it.

This board is a genuine USB keyboard and mouse on a separate cable. A game
receives its reports the same way it receives a second physical keyboard.

It is not a driver and it does not hook anything. The board presents an ordinary
HID keyboard and mouse and does not claim to be another company's hardware.

Two limits worth knowing. Online games with kernel level anti-cheat are a
separate problem and out of scope. The target game must be the focused window,
because a real keyboard types into whatever has focus.

## What the board does and does not do

The board has no idea what a macro is. All timing, blocks, repeats, hold modes
and mute logic stay in `engine/MacroEngine.ahk`. The engine decides every press,
release and movement, then sends it here one line at a time.

That split is the point. New editor features change what the engine decides,
never what this board understands, so the firmware should not need to change as
the app grows.

## USB layout

One cable, three interfaces:

| Interface | Purpose |
| --- | --- |
| HID keyboard | Modifier byte, a reserved byte, then 16 key slots |
| HID mouse | 5 buttons, 16 bit relative x and y, wheel |
| CDC serial | Control channel, this is how the shell talks to the board |

The keyboard carries 16 key slots because a boot keyboard only holds 6 and a
macro can hold more than that at once. The first two bytes keep the boot layout
so anything expecting that still reads the modifiers correctly.

Mouse x and y are 16 bit on purpose. The engine clamps movement to plus or minus
2000 per tick, which does not fit the 8 bit field a plain boot mouse uses.

The USB product string is `Vendetta RP2040` or `Vendetta RP2350`. The shell finds
the board by matching that string, which is more reliable than guessing a
product id.

## Serial protocol

One command per line. Lines end with `\n`. A line that does not parse is ignored.

```text
K <hid-usage> <0|1>              key down when 1, up when 0
M <buttons> <dx> <dy> <wheel>    buttons are absolute, movement adds on
?                                replies with the USB product string
```

`K` takes a HID usage id, not a letter. Decimal or `0x` hex both work. Usages
224 to 231 are the modifiers and go into the modifier byte. Everything else takes
a key slot. Name to usage mapping lives in the shell, next to the port.

`M` button bits: 1 left, 2 right, 4 middle, 8 X1, 16 X2. The button field
replaces the current state. `dx`, `dy` and `wheel` are added to whatever has not
been reported yet, so a large movement is carried across reports instead of being
thrown away.

If the serial port closes, the board releases every key and button it is holding.
Nothing can stay stuck down if the app crashes or the cable is pulled.

## Building

The old Pico Windows installer is retired. Install the four tools, clone the
SDK, then run `build.ps1`.

1. Open a new PowerShell as yourself, not as admin, and install the tools. Say
   yes to any path prompts. Git can be skipped if `git --version` already works.

```powershell
winget install -e --id Git.Git
winget install -e --id Python.Python.3.12
winget install -e --id Kitware.CMake
winget install -e --id Ninja-build.Ninja
winget install -e --id Arm.ArmGnuToolchain
```

2. Close that PowerShell window and open a fresh one so PATH updates. Then
   clone the SDK with its submodules. TinyUSB lives in a submodule, so a plain
   clone without `--recurse-submodules` will fail later.

```powershell
mkdir C:\pico -ErrorAction SilentlyContinue
git clone --recurse-submodules https://github.com/raspberrypi/pico-sdk.git C:\pico\pico-sdk
```

3. Point the session at that SDK, and make it stick for future windows:

```powershell
$env:PICO_SDK_PATH = "C:\pico\pico-sdk"
[Environment]::SetEnvironmentVariable("PICO_SDK_PATH", "C:\pico\pico-sdk", "User")
```

4. Confirm the tools are visible. Each command should print a version, not an
   error.

```powershell
cmake --version
ninja --version
arm-none-eabi-gcc --version
python --version
```

If `arm-none-eabi-gcc` is missing, add the toolchain `bin` folder to PATH. It
is usually under `C:\Program Files (x86)\Arm GNU Toolchain arm-none-eabi\`.

5. Build both UF2 files. The script downloads a prebuilt `picotool` the first
   time. That tool writes the UF2. Building it from source needs Visual Studio's
   C compiler, which is a separate install we skip.

From the `firmware` folder:

```powershell
.\build.ps1
```

If a previous run failed while compiling picotool, just run `.\build.ps1`
again. The script wipes `firmware/build` so CMake does not reuse that broken
cache.

Both files land in `firmware/out`:

- `vendetta-rp2040.uf2` for Pico and Pico W
- `vendetta-rp2350.uf2` for Pico 2 and Pico 2 W

Each chip needs its own build directory because CMake caches the platform, which
is why the script configures twice.

## Flashing

1. Use a cable that carries data. Some charging cables do not.
2. Hold the BOOTSEL button, plug the board in, then let go. A drive appears.
3. Copy the UF2 that matches your chip onto that drive. The drive disappears.
4. Unplug and plug back in normally.

## Testing without the app

Find the port in Device Manager, then in PowerShell:

```powershell
$p = New-Object System.IO.Ports.SerialPort "COM5", 115200
$p.NewLine = "`n"
$p.Open()

$p.WriteLine("?")
$p.ReadLine()          # should print Vendetta RP2040 or Vendetta RP2350

# Types "a" into whatever window has focus. Usage 4 is the letter a.
$p.WriteLine("K 4 1")
$p.WriteLine("K 4 0")

# Holds left shift, types a, releases. Usage 225 is left shift.
$p.WriteLine("K 225 1")
$p.WriteLine("K 4 1")
$p.WriteLine("K 4 0")
$p.WriteLine("K 225 0")

# Moves the pointer right and down, no buttons.
$p.WriteLine("M 0 100 100 0")

# Left click down then up.
$p.WriteLine("M 1 0 0 0")
$p.WriteLine("M 0 0 0 0")

$p.Close()
```

Focus Notepad before the key lines. A few usages for testing: `a` to `z` are 4 to
29, `1` to `9` are 30 to 38, `0` is 39, enter 40, escape 41, space 44, left
control 224, left shift 225, left alt 226.

Closing the port should release anything still held. To check that, send
`K 225 1` and close without sending the matching up.

## Status

Written but not yet compiled or flashed. This machine has no CMake, no ARM
toolchain and no Pico SDK, so the code has not been through a compiler. Expect to
fix small build errors on the first run, most likely around TinyUSB version
differences in the SDK you install.
