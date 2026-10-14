# AHK baseline

Measured on this PC for #39. `windows-latest` can build and can run `--self-test`. It is not this report. Nothing here chooses C or C++.

## Machine

| | |
|---|---|
| CPU | 13th Gen Intel Core i7-13700KF, 16 cores, 24 logical, 3400 MHz |
| OS | Windows 10 Pro 26H2, build 26300.9457 |
| Power | High performance (`8c5e7fda-e8bf-4a96-9a85-a6e23a8c635c`) |
| Board | Vendetta RP2350 on COM7, 115200 8N1 |
| Engine | AutoHotkey v2, `engine/MacroEngine.ahk` |
| Self-test | PASS, exit 0. The 5 ms check is inside the engine process. It is not the jitter table. |

Each test ran 3 times, 60 seconds where the test has a duration, 10,000 trigger attempts. `cpu_pct` is process kernel+user time over wall time. 100 means one core saturated. `switches_per_s` is the sum of `\Thread(*)\Context Switches/sec` for the engine process. This Windows build has no `Context Switches/sec` counter on the Process object, so the thread counter is the wakeup proxy. A separate process cannot see `HrtWait` returns.

## How it was measured

`tools/bench/run.ps1` builds `tools/bench/bench.c` (plain C, so the harness is not the language choice) and writes one CSV. The hook, the pipe client, and the serial probe run in that process, not inside the AHK tick. Output edges are `WH_KEYBOARD_LL` timestamps from `QueryPerformanceCounter`.

A later run (#53) can pass `--engine-exe` and use the same pipe, hook, and serial probe.

The jitter and CPU macros are `playMode: repeat`, `repeatCount: 200000`, steps `down F14`, `wait N`, `up F14`, `wait 2`. The trigger is F13 from the board (HID usage `0x68`). `SendInput` cannot start a macro: `ReadKey` drops `LLKHF_INJECTED`. That row is 0 hits on purpose. The trigger-latency number is the board row: hook time of the hardware F13 down to hook time of the F14 down. Serial time is before that gap.

The real `profiles/default.json` is parsed only while disarmed. The tick-gap profile is a padded bench profile of the same byte size that keeps the running macro id, so the saved binds never go live.

## Jitter

F14 down-to-down is one loop (wait N, then the 2 ms gap). F14 down-to-up is the wait step itself.

Down-to-down, milliseconds:

| Wait | Run | n | p50 | p95 | p99 | max | cpu_pct |
|---|---|---|---|---|---|---|---|
| 1 | 1 | 19021 | 5.01 | 5.78 | 6.25 | 7.83 | 31.50 |
| 1 | 2 | 19980 | 4.71 | 5.25 | 5.52 | 6.59 | 31.88 |
| 1 | 3 | 19564 | 4.80 | 7.39 | 12.43 | 56.10 | 25.30 |
| 5 | 1 | 11569 | 8.29 | 8.99 | 9.48 | 11.35 | 21.93 |
| 5 | 2 | 11712 | 8.18 | 8.59 | 8.90 | 11.77 | 19.79 |
| 5 | 3 | 11717 | 8.26 | 10.84 | 15.38 | 38.16 | 19.31 |
| 16 | 1 | 4968 | 19.24 | 19.95 | 20.39 | 21.97 | 10.98 |
| 16 | 2 | 4984 | 19.08 | 19.55 | 19.94 | 20.68 | 9.95 |
| 16 | 3 | 4916 | 19.05 | 20.33 | 22.16 | 74.54 | 7.36 |

Down-to-up, milliseconds:

| Wait | Run | n | p50 | p95 | p99 | max |
|---|---|---|---|---|---|---|
| 1 | 1 | 19022 | 2.34 | 3.00 | 3.37 | 5.20 |
| 1 | 2 | 19981 | 2.14 | 2.62 | 2.83 | 3.96 |
| 1 | 3 | 19564 | 2.15 | 3.74 | 6.79 | 26.43 |
| 5 | 1 | 11569 | 5.68 | 6.19 | 6.54 | 7.87 |
| 5 | 2 | 11712 | 5.65 | 5.97 | 6.20 | 7.01 |
| 5 | 3 | 11717 | 5.70 | 6.77 | 9.63 | 24.32 |
| 16 | 1 | 4968 | 16.62 | 17.14 | 17.53 | 18.30 |
| 16 | 2 | 4984 | 16.54 | 16.94 | 17.23 | 18.09 |
| 16 | 3 | 4916 | 16.53 | 17.17 | 18.16 | 35.89 |

Runs 1 and 2 sit on the timer. Run 3 has the same median and a long tail. A requested 1 ms wait lands near 2.2 ms. The clock will not schedule a 1 ms edge and the following up in the same tick.

## Trigger to first event

10,000 attempts each run.

| Case | Run | Hits | Misses | p50 | p95 | p99 | max |
|---|---|---|---|---|---|---|---|
| SendInput F13 | 1 | 0 | 10000 | | | | |
| SendInput F13 | 2 | 0 | 10000 | | | | |
| SendInput F13 | 3 | 0 | 10000 | | | | |
| Board F13 | 1 | 9290 | 710 | 0.62 | 1.13 | 1.49 | 4.99 |
| Board F13 | 2 | 9668 | 332 | 0.50 | 0.83 | 1.00 | 4.34 |
| Board F13 | 3 | 9674 | 326 | 0.52 | 0.84 | 1.03 | 4.22 |

SendInput is a miss every time. The engine records a physical key and ignores an injected one. Run 1's board misses were 232 with no hardware F13 and 478 with F13 but no F14 inside 80 ms. Runs 2 and 3 missed only on the output side (332 and 326). When the edge arrives, the hook gap is about half a millisecond.

## CPU

Armed, no runner, 60 seconds. Then 1, 4, and 16 repeat macros on one shared F13 trigger.

| Case | Run | cpu_pct | switches/s | hook events |
|---|---|---|---|---|
| Idle armed | 1 | 3.10 | 490 | 0 |
| Idle armed | 2 | 2.71 | 344 | 0 |
| Idle armed | 3 | 3.57 | 378 | 0 |
| 1 macro | 1 | 28.82 | 8729 | 40359 |
| 1 macro | 2 | 30.08 | 8674 | 39717 |
| 1 macro | 3 | 22.64 | 8331 | 40968 |
| 4 macros | 1 | 35.35 | 10443 | 132725 |
| 4 macros | 2 | 31.98 | 10404 | 141778 |
| 4 macros | 3 | 37.31 | 10452 | 131013 |
| 16 macros | 1 | 42.21 | 12581 | 248497 |
| 16 macros | 2 | 41.18 | 12908 | 276998 |
| 16 macros | 3 | 42.37 | 12747 | 253169 |

Idle is a few percent of one core, a few hundred context switches a second. Sixteen repeat macros stay under half of one core. The hook-event column is every watched key edge in the window, not a separate latency sample.

## Board

Output mode `rp2350`. Time from the engine's hid message being read to the board's last `ok` for that report, and from each `K` line to its `ok`.

| Case | Run | n | p50 | p95 | p99 | max | lines/s |
|---|---|---|---|---|---|---|---|
| Emit to ok | 1 | 45620 | 0.132 | 0.258 | 0.341 | 0.589 | 760 |
| Emit to ok | 2 | 45066 | 0.131 | 0.259 | 0.340 | 0.782 | 751 |
| Emit to ok | 3 | 44932 | 0.129 | 0.262 | 0.337 | 0.593 | 749 |
| Line to ok | 1 | 45620 | 0.128 | 0.254 | 0.337 | 0.585 | 760 |
| Line to ok | 2 | 45066 | 0.127 | 0.255 | 0.336 | 0.780 | 751 |
| Line to ok | 3 | 44932 | 0.126 | 0.259 | 0.333 | 0.589 | 749 |

The serial reply is about 0.13 ms at the median, about 750 lines a second while the spam macro runs. Engine CPU during that minute was 23.69, 24.35, and 22.63 percent of one core.

## Pipe

Ack time is the round trip, including `JSON.Parse` and `WindowListJson`, because the state broadcast is sent before the ack. The gap is the F14 down-to-down interval that crosses that ack while a wait-5 macro is running. The padded profile is 100845 bytes. Reload and the full file run while disarmed. The file wrap of `profiles/default.json` is 100880 bytes. Those macros were not armed.

| Case | Run | ack ms | gap ms |
|---|---|---|---|
| Padded profile, macro running | 1 | 466 | 469 |
| Padded profile, macro running | 2 | 420 | 424 |
| Padded profile, macro running | 3 | 394 | 401 |
| Reload `default.json`, disarmed | 1 | 517 | |
| Reload `default.json`, disarmed | 2 | 540 | |
| Reload `default.json`, disarmed | 3 | 504 | |
| Full profile message, disarmed | 1 | 557 | |
| Full profile message, disarmed | 2 | 516 | |
| Full profile message, disarmed | 3 | 591 | |

A profile of this size blocks the tick for a few hundred milliseconds. The running macro's usual interval stays near 8 ms on either side of that hole. The plan already moves the window list and the foreground lookup off the playback thread.
