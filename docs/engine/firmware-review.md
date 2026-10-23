# Firmware and serial protocol review

#51. Measured on the same PC as `docs/engine/benchmarks.md`. The decision is to change nothing in the protocol now. `K`, `M`, `?` and `!` stay as they are. The one change with a measurable gain is a batch line, and it only helps a report that changes more than one key. It is written down below as an option, not built.

## Machine

| | |
|---|---|
| CPU | 13th Gen Intel Core i7-13700KF |
| OS | Windows 10 Pro 26H2, build 26300.9711 |
| Power | High performance |
| Board | Vendetta RP2350 on COM7 |
| Firmware on the board | The Sep 30 build, from before #17, for every table below. See "What is on the board". |
| Engine for the spam rows | AutoHotkey v2, `engine/MacroEngine.ahk` |

## What the firmware does today

`firmware/main.c`. One line per command, up to 64 bytes (`LINE_LIMIT`, `main.c:23`).

- `K <usage> <0|1>` updates the key state and sends one keyboard report before the next line is read (`handle_line`, `main.c:162`). That is on purpose: a down and an up in the same burst must be two reports, or Windows never sees the key (`main.c:64`).
- `M <buttons> <dx> <dy> <wheel>` sets buttons and adds movement, then sends one mouse report (`main.c:180`).
- Both reply `ok` or `hid-busy`. `hid-busy` means the HID endpoint was not free within 50 ms (`hid_wait`, `main.c:45`). The main loop sends a dirty report again when the endpoint frees (`send_reports`, `main.c:224`).
- `?` replies with the product string. `!` replies with HID ready and mount state.
- Both HID endpoints have `bInterval` 1 (`usb_descriptors.c:68`, `:70`). The RP2040 and RP2350 are full-speed USB, so 1 ms is the shortest poll there is.
- The keyboard is a boot keyboard. The firmware tracks 16 keys, and the report carries the first 6 (`fill_boot_keys`, `main.c:55`). The mouse is a boot mouse, at most ±127 per axis per report. The rest carries into the next report (`take_8`, `main.c:74`).

The shell writes one line per key change and waits for its reply before the next line (`BoardLine`, `shell/MainWindow.xaml.cs:1491`; the native report image is mapped the same way in `shell/ReportBoard.cs`).

## How it was measured

`tools/bench/bench.c`. The hook and the serial port are in the bench process, not in an engine.

- **Spam** is the #39 board row. A `repeat` macro presses F14, waits 1 ms, releases it, then waits 2 ms. The engine sends hid messages and the bench writes the `K` lines. Each reply is counted as `ok`, `hid-busy`, or other (110d5c0). 60 seconds, 3 runs.
- **Burst** is new for this review: `bench --only burst`. No engine is in the path. The bench presses k keys at once on F15 up (k = 1, 2, 4, 6). **Lockstep** is what the shell does: one line, then its reply, then the next. **Piped** writes all k lines in one write and reads the replies after, which is what "skip the reply" would cost at best. Span is the first write to the hook seeing the last down. Spread is the hook's first down to its last down. 1000 bursts per case, 3 runs.

Run it with:

```powershell
tools\bench\out\bench.exe --repo . --ahk "$env:ProgramFiles\AutoHotkey\v2\AutoHotkey64.exe" --out burst.csv --run 1 --only burst
```

## Results

### Spam: one key change per line

Line is one `K` line to its reply, in milliseconds.

| Run | Lines | p50 | p95 | p99 | max | Lines/s | Engine cpu_pct | `hid-busy` |
|---|---|---|---|---|---|---|---|---|
| 1 | 48060 | 0.116 | 0.188 | 0.229 | 2.261 | 801 | 20.49 | 0 |
| 2 | 43614 | 0.111 | 0.181 | 0.212 | 0.364 | 727 | 17.47 | 0 |
| 3 | 43112 | 0.112 | 0.181 | 0.217 | 0.775 | 719 | 18.81 | 0 |

The reply is about 0.11 ms, a little under the 0.13 ms in #39. There was not one `hid-busy` in 134,786 lines here, or in the 78,000 burst downs below. The endpoint is free again 1 ms after a report, and the 50 ms wait never runs out while the host is polling. `hid-busy` is a suspend or unplug signal, not a load signal.

### Burst: several key changes at once

Span p50, milliseconds, runs 1 / 2 / 3:

| Keys | Lockstep | Piped | Spread (lockstep) |
|---|---|---|---|
| 1 | 0.67 / 0.67 / 0.71 | 0.67 / 0.72 / 0.70 | 0 |
| 2 | 1.78 / 1.76 / 1.77 | 1.77 / 1.76 / 1.76 | 0.99 / 0.98 / 0.98 |
| 4 | 3.76 / 3.76 / 3.75 | 3.76 / 3.75 / 3.78 | 2.98 / 2.99 / 2.98 |
| 6 | 5.77 / 5.75 / 5.83 | 5.76 / 5.75 / 5.77 | 4.99 / 4.97 / 4.97 |

Span p99 and max, runs 1 / 2 / 3:

| Keys | Lockstep p99 | Lockstep max | Piped p99 | Piped max |
|---|---|---|---|---|
| 1 | 1.23 / 1.21 / 1.58 | 1.36 / 1.51 / 8.23 | 1.24 / 1.45 / 4.97 | 1.43 / 2.02 / 9.73 |
| 2 | 2.05 / 2.06 / 2.16 | 2.37 / 2.42 / 5.89 | 2.07 / 2.08 / 2.11 | 2.52 / 2.25 / 44.61 |
| 4 | 4.04 / 4.03 / 4.04 | 4.27 / 4.41 / 4.44 | 4.06 / 4.05 / 5.10 | 4.34 / 4.47 / 57.52 |
| 6 | 7.11 / 6.05 / 6.34 | 16.45 / 6.52 / 7.59 | 6.02 / 6.35 / 6.20 | 6.39 / 9.17 / 6.70 |

Every extra key in a burst adds 1.0 ms, because each `K` line is its own report and each report waits for the next 1 ms poll. Piped is the same as lockstep. The serial reply is not the cost. The USB poll is. Run 3 has a few long tails in both modes, like run 3 in #39.

## The options in #51

| Option | Gain measured | Decision |
|---|---|---|
| Skip replies unless asked | None. Piped equals lockstep at every k. | No. The reply is also how the shell knows a line was taken before it writes the next one. |
| Lower `bInterval` | Not possible. It is already 1, the full-speed minimum. | No change. |
| Binary framing | At most part of the 0.11 ms reply. A line is about 8 bytes. USB CDC ignores the 115200 baud setting. | No. |
| Batch line, one report for several key changes | About (k − 1) ms for a report that changes k keys. Nothing for one key. | Not now. Design below. Worth it only if real macros change several keys in one publish. |

Most macro steps change one key per publish, and the spam rows above are that case. A modifier combo is the common two-key case, and a real keyboard also sends those about a poll apart. So the batch line is a nice-to-have, not a fix.

## If a batch line is added later

This keeps every old board and the AHK path working.

- New line: `B <+|-><usage> ...`, for example `B +225 +4`. The board applies the changes in order and sends one keyboard report at the end. If one line has a down and an up for the same usage, the board sends a report at that point first, so the rule at `main.c:64` still holds. The reply is one `ok` or `hid-busy`.
- Announce it on `?`: `Vendetta RP2350 B`. The shell checks `?` with `StartsWith("Vendetta RP2350")` (`shell/MainWindow.xaml.cs:1294`), and the bench uses `strncmp(reply, "Vendetta", 8)`. Both already accept a suffix. `BoardPorts.cs` reads the USB product string, not `?`, so the descriptor string stays `Vendetta RP2350`.
- The PC sends `B` only after it has seen `B` in the `?` reply. An old board ignores a line it cannot parse and sends no reply, so the shell would sit in its 1500 ms read timeout.
- `K` and `M` stay, and the shell keeps using them on old boards.

## What is on the board

The measurements above ran on the Sep 30 build, from before the #17 retry. On that build a keyboard report that gets `hid-busy` clears `kb_dirty` and is never sent again. With zero `hid-busy` above, that did not change any number here. A key-up that does get `hid-busy` stays unsent until the next `K` line, so Windows sees the key held.

How this was checked (SDK 2.3.1 for every build):

| Image | Program bytes | Build date | Against the flash |
|---|---|---|---|
| Flash readback, `firmware/out/backup/vendetta-rp2350-current.uf2` | 82,944 | Sep 30 2026 | |
| Sep 30 UF2, now `firmware/out/backup/vendetta-rp2350-sep30.uf2` | 82,944 | Sep 30 2026 | Identical |
| Fresh build of `ede3dd4`, the source before #17 | 82,944 | Oct 23 2026 | 77 bytes: the date string, some branch offsets, and veneer order. Same layout. |
| Fresh build of `main`, now `firmware/out/vendetta-rp2350.uf2` | binary end `0x10014390` vs `0x10014388` | Oct 23 2026 | Only `main()` changed size, 0x918 to 0x924. The line handlers are inlined there. That is the #17 change. |

Only the program family blocks are compared. The SDK build also writes an absolute-family block near the top of flash, which a readback does not have. A rebuild is never byte-identical: the build date is in the image, and the link order follows the build path.

The `main` build (SHA-256 `74a43f84…`) was flashed on 2026-10-23. The rollback is the Oct 22 flash dump in `firmware/out/backup`. `firmware/out` is not in git.

After the flash, `?` still replies `Vendetta RP2350` and `!` replies `hid 1 1 mounted 1`. One burst run of 200 per case matched the tables above. Lockstep span p50 was 0.72, 1.76, 3.77 and 5.79 ms for 1, 2, 4 and 6 keys, and there was no `hid-busy`. The serial reply cannot show which build is running, so this checks that nothing regressed, not which image is on the board.

## Other findings

- A seventh held key is tracked and not sent. It shows up in the report after one of the first six is released.
- One mouse report moves at most 127 counts per axis. Both engines clamp a move step to ±16000 (`Clamp`, `engine/MacroEngine.ahk:1224`; `ENGINE_REPORT_MOVE_LIMIT`, `engine-native/src/ipc/report.h:15`), and a full-size step takes 126 reports, about 126 ms, to reach Windows. The README said ±2000.
- `?` does not say which firmware is running, so the PC cannot tell a pre-#17 board from a current one. A version on the `?` reply, after the product string like the `B` flag, would fix that on the next flash.
- `firmware/README.md` described a 16-slot keyboard and a 16-bit mouse, said the code had never been compiled, and left out `!`. It is corrected in this change.
