# Language decision

#40. The language is C17.

The same hot path was compiled as C17 and as C++20. The gaps are under a nanosecond, the 1 ms wait matches, and the binaries are the same size. That is a tie. The pick is hot-path control and testability, which is what a tie was supposed to use.

`windows-latest` is not this measurement. Nothing here starts `engine-native/`.

## Rules

- C17. The hot path is plain functions over caller-provided arrays.
- No allocation after load. Profile load may allocate. The tick, the hook callback, the step interpreter, the key lookup, and the message encode do not.
- Key to macro is an array indexed by virtual key.
- A playback message is a fixed little-endian image. The playback thread does not format text.
- The wait is an unnamed `CreateWaitableTimerExW` with `CREATE_WAITABLE_TIMER_HIGH_RESOLUTION`, waited on its handle. There is no completion routine.
- `timeBeginPeriod` is not the playback clock. MMCSS is not registered. Pro Audio did not tighten the high-res wait, and reverting it left a later `Sleep(1)` on the 15.6 ms tick.
- Errors are return codes.
- The interpreter, the lookup, and the codec are portable C. Linux CI builds that core. The timer, the hooks, and `SendInput` stay in the Windows file.
- The hook callback returns without playing a macro. One report's inputs go out in one `SendInput` call. Own output keeps `dwExtraInfo = 0x56454E44`.
- No kernel driver.

## Machine

| | |
|---|---|
| CPU | 13th Gen Intel Core i7-13700KF, 16 cores, 24 logical, 3400 MHz |
| OS | Windows 10 Pro 26H2, build 26300.9457 |
| Power | High performance (`8c5e7fda-e8bf-4a96-9a85-a6e23a8c635c`) |
| Compiler | MSVC 19.51.36256, x64 |
| Flags | `/O2 /W4 /GL-`. C17 is `/std:c17 /TC`. C++20 is `/std:c++20 /TP /GR- /EHs-c-`. A third build left exceptions and RTTI on (`/EHsc /GR`). |
| Pin | Logical processor 2, `HIGH_PRIORITY_CLASS`, `THREAD_PRIORITY_HIGHEST` |
| clang-cl | Not installed. One backend, two language modes. A second compiler would mix an optimizer change into the language result. |

`tools/bench/lang.ps1` compiles `tools/bench/lang/hot.c` twice and links each copy with `main.c`. The hot functions are a separate translation unit, with link-time optimization off, so the driver cannot fold the work away. Checksums matched across C, C++ with exceptions off, and C++ with exceptions on:

| Work | Checksum |
|---|---|
| Interpreter | 4898975 |
| VK array and open-address hash | 99335500 |
| Byte codec | 1026469200 |
| `snprintf` codec | 111726000 |

`std::unordered_map::find` returned the same 99335500. Every CPU row reported `heap=0` for the plain functions (`PrivateUsage` across the timed batches). The map's timed `find` reported `allocs=0`. The map's `reserve` and `emplace` run before the timer.

Percentiles use the same index as `tools/bench/bench.c`: sorted sample `(n - 1) * p`, truncated. CPU rows are 20 batches, so the p95 and p99 indexes are the same sample. The max column is the slowest batch. Timer rows are 1000 waits, so p50, p95, p99, and max are different samples.

A pair of CPU results ties when the median-of-runs gap is under 1 ns, or under 10 percent, whichever bar is wider. A pair of waits ties when the p50 gap is under 0.05 ms. One nanosecond is the bar because the high-res wait below is 1.52 ms and the #39 board trigger is about 0.5 ms.

These processes are not the #39 harness. That harness measures the AHK engine. The plan says the baseline does not choose the language. The percentile, the machine, and the three-run shape are the same idea. Results stayed in `tools/bench/out/lang/`, which is gitignored.

## Interpreter

One million step dispatches per batch. The program is a repeat frame around down, wait, up, wait, then another down, wait, up, wait. Nanoseconds per dispatch:

| Lang | Run | p50 | p99 | max |
|---|---|---|---|---|
| C | 1 | 1.3034 | 1.3377 | 1.3479 |
| C | 2 | 1.3101 | 1.3407 | 1.3519 |
| C | 3 | 1.3036 | 1.3304 | 1.3869 |
| C++ | 1 | 1.7263 | 1.8365 | 1.8372 |
| C++ | 2 | 1.6639 | 1.8238 | 1.8449 |
| C++ | 3 | 1.6783 | 1.8555 | 1.8563 |

Median of runs: C 1.304 ns, C++ 1.678 ns. The gap is 0.37 ns. That is under the 1 ns bar. It is about one four-millionth of the 1.52 ms wait.

## Lookup

512,000 lookups per batch. Forty-eight bindings. The array is 256 slots indexed by virtual key. The hash is an open-address table of 512 slots, same function in both languages. Nanoseconds per lookup:

| Bench | Lang | Run | p50 | p99 | max |
|---|---|---|---|---|---|
| Array | C | 1 | 0.1959 | 0.2002 | 0.2074 |
| Array | C | 2 | 0.1959 | 0.1961 | 0.2004 |
| Array | C | 3 | 0.1949 | 0.1951 | 0.1996 |
| Array | C++ | 1 | 0.1959 | 0.1996 | 0.2396 |
| Array | C++ | 2 | 0.1957 | 0.1959 | 0.2018 |
| Array | C++ | 3 | 0.1957 | 0.1959 | 0.1973 |
| Hash | C | 1 | 0.6418 | 0.6730 | 0.6730 |
| Hash | C | 2 | 0.6379 | 0.6436 | 0.6648 |
| Hash | C | 3 | 0.6406 | 0.6484 | 0.6514 |
| Hash | C++ | 1 | 0.4562 | 0.4842 | 0.5000 |
| Hash | C++ | 2 | 0.4559 | 0.4615 | 0.4682 |
| Hash | C++ | 3 | 0.4564 | 0.5658 | 0.5707 |

The array matches at 0.196 ns. The open-address hash is faster in C++ by about 0.18 ns (0.456 vs 0.641). That gap is also under 1 ns, and it points the other way from the interpreter. The array is about three times faster than the faster hash. The lookup on the hot path is the array.

`std::unordered_map<uint32_t, uint32_t>::find`, built with `reserve(128)` before the timer, same queries:

| Run | p50 | p99 | max | allocs during the timed loop |
|---|---|---|---|---|
| 1 | 1.0111 | 1.0971 | 1.1842 | 0 |
| 2 | 0.9971 | 1.0287 | 1.1598 | 0 |
| 3 | 0.9686 | 1.0607 | 1.0979 | 0 |

About 1.00 ns. About five times the array. The timed loop did not allocate. The table was allocated by `reserve` and `emplace` before the loop. This MSVC still compiled that file with `/GR- /EHs-c-` (exit 0, no warning). The flag does not keep the standard map out of the program, and it does not keep the build-time heap out.

## Messages

25,600 messages per batch. The byte path writes a 48-byte image and reads it back. The text path is `snprintf` into a stack buffer. Nanoseconds per message:

| Bench | Lang | Run | p50 | p99 | max |
|---|---|---|---|---|---|
| Bytes | C | 1 | 7.5078 | 7.5742 | 7.6250 |
| Bytes | C | 2 | 7.5508 | 7.6055 | 8.1797 |
| Bytes | C | 3 | 7.5352 | 7.6055 | 7.6211 |
| Bytes | C++ | 1 | 7.5586 | 7.6328 | 8.4453 |
| Bytes | C++ | 2 | 7.6406 | 7.6680 | 7.6680 |
| Bytes | C++ | 3 | 7.5664 | 7.6836 | 7.6953 |
| Text | C | 1 | 260.3945 | 268.2461 | 269.4023 |
| Text | C | 2 | 263.4414 | 269.9141 | 276.1758 |
| Text | C | 3 | 265.4805 | 272.2773 | 272.5312 |
| Text | C++ | 1 | 252.5000 | 256.2383 | 258.0000 |
| Text | C++ | 2 | 249.3828 | 255.3438 | 256.6562 |
| Text | C++ | 3 | 250.1211 | 253.8594 | 256.2852 |

Byte median of runs: C 7.54 ns, C++ 7.57 ns. Text: C 263 ns, C++ 250 ns, about 5 percent, inside the 10 percent bar. Text is about 33 times the byte image. The #39 profile stall was a few hundred milliseconds of parse, on top of a byte-sized report. The playback encode is the byte image.

## Wait

1,000 waits of a requested 1 ms. The high-res timer and the Pro Audio run are the language pair. `Sleep` in this table ran after Pro Audio was reverted, which is a different state. The isolated `Sleep` is the next table.

| Bench | Lang | Run | p50 | p95 | p99 | max |
|---|---|---|---|---|---|---|
| High-res | C | 1 | 1.5162 | 1.6100 | 1.7482 | 2.3212 |
| High-res | C | 2 | 1.5145 | 1.6394 | 1.8486 | 2.4502 |
| High-res | C | 3 | 1.5189 | 1.6094 | 1.7874 | 2.5334 |
| High-res | C++ | 1 | 1.5138 | 1.6622 | 1.8993 | 2.2635 |
| High-res | C++ | 2 | 1.5190 | 1.6499 | 1.7785 | 2.0424 |
| High-res | C++ | 3 | 1.5177 | 1.5993 | 1.7106 | 2.3302 |
| Pro Audio | C | 1 | 1.5164 | 1.6246 | 1.7171 | 2.0700 |
| Pro Audio | C | 2 | 1.5159 | 1.6231 | 1.8084 | 2.2926 |
| Pro Audio | C | 3 | 1.5149 | 1.6350 | 1.7045 | 1.9789 |
| Pro Audio | C++ | 1 | 1.5141 | 1.6222 | 1.6966 | 2.4509 |
| Pro Audio | C++ | 2 | 1.5149 | 1.6193 | 1.7227 | 2.4121 |
| Pro Audio | C++ | 3 | 1.5151 | 1.6166 | 1.6751 | 1.8153 |
| Sleep after Pro Audio revert | C | 1 | 15.7430 | 16.5227 | 16.6719 | 16.8535 |
| Sleep after Pro Audio revert | C | 2 | 15.6972 | 16.5199 | 16.6852 | 17.2779 |
| Sleep after Pro Audio revert | C | 3 | 15.7137 | 16.4952 | 16.6294 | 16.8171 |
| Sleep after Pro Audio revert | C++ | 1 | 15.7139 | 16.5664 | 16.6731 | 16.8424 |
| Sleep after Pro Audio revert | C++ | 2 | 15.7125 | 16.5550 | 16.6777 | 17.3458 |
| Sleep after Pro Audio revert | C++ | 3 | 15.7462 | 16.5462 | 16.6855 | 17.0551 |

High-res p50 is 1.516 ms in C and 1.518 ms in C++. Pro Audio is 1.516 ms and 1.515 ms. The language gap is about 0.002 ms. Pro Audio did not move the median. The p99 stays under 2 ms and the max stays under 2.6 ms.

A requested 1 ms wait lands near 1.5 ms. That matches the shape of the #39 result, where a requested 1 ms step landed near 2.2 ms down-to-up inside the AHK tick. The clock will not schedule a 1 ms edge as 1 ms.

Isolated `Sleep(1)` after `timeBeginPeriod(1)`, same pin, 50 high-res waits first, no MMCSS. Three processes, 300 waits each. `NtQueryTimerResolution` already read 1.000 ms before `timeBeginPeriod`.

| Run | p50 | p99 | max |
|---|---|---|---|
| 1 | 1.5401 | 2.8734 | 2.9172 |
| 2 | 1.5834 | 2.7802 | 2.9406 |
| 3 | 1.5658 | 2.8042 | 3.0038 |

None of the 900 waits were over 5 ms. The median is about 1.56 ms, close to the high-res timer. The tail is wider: p99 near 2.8 ms, against under 1.9 ms for the high-res timer.

One follow-up process then did 1,000 Pro Audio high-res waits and called `AvRevertMmThreadCharacteristics`. The resolution query still read 1.000 ms. The next 40 `Sleep(1)` calls averaged 15.657 ms, and all 40 were over 5 ms. That is the 15.7 ms block in the table above. Both languages hit it because both ran `Sleep` after the Pro Audio bench. It is a real measurement of that order. It is the wrong number for an untouched `Sleep(1)`.

The playback wait is the high-res timer. MMCSS stays off.

## Exceptions, RTTI, size

The same source again, compiled `/std:c++20 /EHsc /GR`. CPU only. Checksums matched. Heap stayed 0. Nanoseconds, p50:

| Bench | Run 1 | Run 2 | Run 3 |
|---|---|---|---|
| Interpreter | 1.6474 | 1.6707 | 1.6554 |
| Array | 0.1961 | 0.1939 | 0.1959 |
| Hash | 0.4572 | 0.4543 | 0.4543 |
| Bytes | 7.5742 | 7.5781 | 7.5039 |
| Text | 250.5117 | 252.7109 | 251.9609 |

This sits with the no-exception C++ numbers. Nothing in the file throws, and the linker was `/OPT:REF`.

| Image | File bytes | Size of code |
|---|---|---|
| C | 156672 | 0x18200 |
| C++ `/EHs-c- /GR-` | 156672 | 0x18200 |
| C++ `/EHsc /GR` | 156672 | 0x18200 |
| `unordered_map` | 165888 | 0x1A000 |

The hashes differ, so the three hot-path images are not byte-identical. The file size and the size of code match. The map program is the one that grew.

## Why C17

Speed does not pick. C was 0.37 ns faster per step. C++ was 0.18 ns faster per open-address probe and about 13 ns faster per `snprintf`. The wait, the array, and the byte codec match. Binary size matches. Leaving exceptions on did not move this program.

Hot-path control picks C. A heap call in this path is a `malloc` in the function. The C++ build can match that, and it did, when the source stays in the C subset. The standard map is the path that does not: it was five times the array, it allocates while the table is built, and `/EHs-c-` still compiled it. A review rule of "no exceptions, no RTTI, no allocation after load" is how a C++ hot path stays honest. This compiler will not enforce it.

Testability picks the same way. The interpreter, the lookup, and the codec are integer C. Linux CI compiles that with `-std=c17` once the Windows timer is in the other file. The benchmark keeps them together so both language modes compile one source. The engine split is the core file and the Windows file. There is no extra flag to forget, and no standard container that allocates while still type-checking under the no-exception flag.

Maintainability is second in the brief. Handle lifetime on the cold path is the real C++ argument, and it lost this tie on purpose. The tie break was hot-path control and testability.

## Patterns

Sources for the shapes above. Not code to copy.

Low-level hook. [LowLevelKeyboardProc](https://learn.microsoft.com/en-us/windows/win32/winmsg/lowlevelkeyboardproc) runs on the thread that installed `WH_KEYBOARD_LL`, and that thread needs a message loop. The callback has to finish inside `LowLevelHooksTimeout` under `HKCU\Control Panel\Desktop`. From Windows 10 1709 the cap is 1000 ms. From Windows 7 a timeout removes the hook with no notice. The callback reads `KBDLLHOOKSTRUCT` and returns. Playback, profile parse, and window walks stay off that stack. AutoHotkey's own hook is C++ and does the filter in the callback: [hook.cpp](https://github.com/AutoHotkey/AutoHotkey/blob/3db7b02f0a06a110294da1f21c2ab28a1efe9920/source/hook.cpp). Kanata's default Windows path is the same hook and reads the virtual key: [scan code vs VK](https://github.com/jtroo/kanata/discussions/1575), [config](https://jtroo.github.io/config.html). Interception is a kernel driver behind `DeviceIoControl`: [interception.c](https://github.com/oblitum/Interception/blob/master/library/interception.c). The plan already says no kernel driver.

`SendInput`. [SendInput](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-sendinput) inserts the `INPUT` array serially. Other input is not mixed into that array. One report is one call. UIPI can block the call without a distinct error. The plan already says the process integrity level stays where it is.

Timer. [CreateWaitableTimerExW](https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-createwaitabletimerexw) takes `CREATE_WAITABLE_TIMER_HIGH_RESOLUTION` (`0x2`) on Windows 10 1803 and later. The high-res object is unnamed, and it does not take a completion routine. Wait on the handle. [timeBeginPeriod](https://learn.microsoft.com/en-us/windows/win32/api/timeapi/nf-timeapi-timebeginperiod) is per process since Windows 10 2004. It changes the scheduler tick and it keeps the CPU out of deeper idle. It is the wrong playback clock. The isolated `Sleep(1)` tail above is the reason, and the post-MMCSS 15.6 ms result is the other reason.

MMCSS. [AvSetMmThreadCharacteristicsW](https://learn.microsoft.com/en-us/windows/win32/api/avrt/nf-avrt-avsetmmthreadcharacteristicsw) and the [scheduler service](https://learn.microsoft.com/en-us/windows/win32/procthread/multimedia-class-scheduler-service). Pro Audio is the high class, priorities 23-26. On this PC the high-res median did not move, and `AvRevertMmThreadCharacteristics` left `Sleep` on the 15.6 ms tick while the resolution query still read 1 ms. The playback thread does not join that class.

Pipe and shared memory. [Named pipes](https://learn.microsoft.com/en-us/windows/win32/ipc/named-pipes) and [named shared memory](https://learn.microsoft.com/en-us/windows/win32/memory/creating-named-shared-memory). A shared section still needs an event to wake the other side. The #39 profile of 100845 bytes blocked the tick for a few hundred milliseconds, and the ack included `WindowListJson`. The board line was about 0.13 ms. The stall is the parse and the window list, which the plan already takes off the playback thread. This decision does not change the transport.

## Left alone

#41 is the architecture doc and the empty project. The thread split and the folder layout belong there. clang-cl was not on this PC, so there is no clang-cl number. The CI compiler for the portable core is part of that project, not part of this choice.
