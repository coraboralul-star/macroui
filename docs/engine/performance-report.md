# Performance report: AHK vs native

#53. Both engines on the same PC, the same harness, and the same day, 3 runs each. AHK ran first, then native, 170 minutes in all. Native is better on jitter, trigger latency, CPU under load, and profile updates. It is level on idle CPU and on the board line. It is worse in two places: armed-idle wakeups, and one 70.9 ms stall in one run. Both are explained below, and each has a follow-up issue filed before #54.

## Machine and build

| | |
|---|---|
| CPU | 13th Gen Intel Core i7-13700KF, 16 cores, 24 logical, 3400 MHz |
| OS | Windows 10 Pro 26H2, build 26300.9711 |
| Power | High performance |
| Board | Vendetta RP2350 on COM7, the `main` firmware with the #17 retry (`docs/engine/firmware-review.md`) |
| AHK | AutoHotkey v2, `engine/MacroEngine.ahk` at `51e4ba0`. `--self-test` exit 0 |
| Native | `macroui-engine.exe` built Release (`-A x64`) from `engine-native/` at `51e4ba0`. `--self-test` printed `macroui-engine 0` |

The results hold for this PC. A slower CPU, another Windows build, or a busy desktop can move every number here.

## How it was measured

`tools/bench/run.ps1`, then `tools/bench/run.ps1 -EngineExe <native exe>`. The harness is the #39 one, described in `docs/engine/benchmarks.md`. For native it speaks the `MUI1` frames from `docs/engine/ipc-protocol.md`. The tests build the same JSON messages, the bench sends them as `command`, `output`, and `profile` frames, and it turns `report` frames into `K` lines the way `shell/ReportBoard.cs` does. Native gets `--profile profiles/default.json`, as the shell passes it.

- **Without the board**: idle, jitter, CPU, and the pipe rows use software output, so the engine calls `SendInput`. The trigger row presses F13 on the board and times the `SendInput` F14 that follows.
- **With the board**: the spam row sets `rp2350`, and every key change goes through the board as a `K` line.

Two differences from #39:

- The heavy CPU case is 11 macros, not 16. The #39 case gave macros 12 to 16 the keys `vk88` to `vk8C`. Native has no `vkXX` names, so those macros would send nothing. The editor never writes `vkXX`, so this is a bench limit, not a product gap. 11 macros use F14 to F24, one key each, on both engines.
- `profiles/default.json` is 83266 bytes now, so the padded pipe profile is 83231 bytes. It was 100845 in #39.

The AHK numbers here are slower than the #39 ones on several rows (one macro is 35 to 37 percent of a core here, 23 to 30 there). The OS build changed between the two. Compare the engines within this report, not against #39.

## Jitter

F14 down-to-down is one loop: down, wait N, up, wait 2. Native runs more loops in the same 60 s, because each loop is shorter. Milliseconds.

| Wait | Engine | Run | n | p50 | p95 | p99 | max | cpu_pct |
|---|---|---|---|---|---|---|---|---|
| 1 | AHK | 1 | 20911 | 4.61 | 4.81 | 5.10 | 7.03 | 21.23 |
| 1 | AHK | 2 | 19662 | 5.27 | 6.18 | 6.75 | 10.58 | 34.18 |
| 1 | AHK | 3 | 25548 | 5.87 | 7.28 | 8.09 | 24.90 | 36.43 |
| 1 | Native | 1 | 39016 | 3.95 | 4.73 | 5.16 | 7.36 | 16.78 |
| 1 | Native | 2 | 26639 | 3.71 | 4.38 | 5.23 | 70.94 | 12.87 |
| 1 | Native | 3 | 31090 | 3.67 | 4.23 | 4.64 | 6.44 | 15.11 |
| 5 | AHK | 1 | 11955 | 7.80 | 8.30 | 8.43 | 24.63 | 13.54 |
| 5 | AHK | 2 | 13569 | 8.11 | 9.04 | 11.33 | 46.72 | 18.87 |
| 5 | AHK | 3 | 17668 | 8.74 | 9.97 | 10.95 | 62.92 | 26.03 |
| 5 | Native | 1 | 20659 | 7.34 | 8.01 | 8.41 | 10.04 | 11.15 |
| 5 | Native | 2 | 15720 | 7.30 | 7.71 | 7.94 | 27.05 | 8.98 |
| 5 | Native | 3 | 16253 | 7.33 | 7.97 | 8.40 | 9.58 | 9.26 |
| 16 | AHK | 1 | 4977 | 19.08 | 19.33 | 20.00 | 26.56 | 7.74 |
| 16 | AHK | 2 | 6177 | 19.47 | 20.31 | 21.03 | 47.85 | 12.74 |
| 16 | AHK | 3 | 7744 | 19.86 | 21.18 | 22.13 | 46.78 | 13.23 |
| 16 | Native | 1 | 8449 | 18.34 | 19.23 | 19.62 | 20.87 | 6.30 |
| 16 | Native | 2 | 6284 | 18.22 | 18.58 | 18.88 | 23.47 | 5.07 |
| 16 | Native | 3 | 6523 | 18.27 | 18.86 | 19.20 | 20.10 | 5.46 |

Down-to-up is the wait step itself. Milliseconds.

| Wait | Engine | Run | p50 | p95 | p99 | max |
|---|---|---|---|---|---|---|
| 1 | AHK | 1 | 2.03 | 2.23 | 2.47 | 3.82 |
| 1 | AHK | 2 | 2.57 | 3.21 | 3.65 | 5.38 |
| 1 | AHK | 3 | 2.88 | 3.95 | 4.54 | 20.73 |
| 1 | Native | 1 | 1.67 | 2.42 | 2.77 | 5.16 |
| 1 | Native | 2 | 1.59 | 2.12 | 2.70 | 68.56 |
| 1 | Native | 3 | 1.56 | 1.97 | 2.29 | 3.31 |
| 5 | AHK | 1 | 5.26 | 5.72 | 5.84 | 18.39 |
| 5 | AHK | 2 | 5.52 | 6.14 | 7.02 | 43.15 |
| 5 | AHK | 3 | 5.75 | 6.53 | 7.13 | 34.98 |
| 5 | Native | 1 | 5.24 | 5.68 | 6.05 | 7.63 |
| 5 | Native | 2 | 5.22 | 5.42 | 5.67 | 10.99 |
| 5 | Native | 3 | 5.24 | 5.66 | 6.02 | 6.99 |
| 16 | AHK | 1 | 16.55 | 16.77 | 17.08 | 20.22 |
| 16 | AHK | 2 | 16.77 | 17.26 | 17.61 | 31.70 |
| 16 | AHK | 3 | 16.79 | 17.63 | 18.20 | 27.72 |
| 16 | Native | 1 | 16.21 | 16.76 | 17.23 | 18.81 |
| 16 | Native | 2 | 16.15 | 16.38 | 16.60 | 18.92 |
| 16 | Native | 3 | 16.17 | 16.55 | 16.86 | 19.24 |

Native lands closer to the requested wait at every size, with a tighter spread, and with less CPU: 0.79 of AHK's in run 1 and about 0.4 in runs 2 and 3 at wait 1. A 1 ms wait is about 1.6 ms on native and 2.0 to 2.9 ms on AHK.

The one exception is the max of wait-1 run 2 on native: one 70.9 ms loop, with a 68.6 ms down-to-up inside it. The p99 of that run is 5.23 ms, so it is a single stall, not a slow run. The other eight native jitter rows top out at 27.05 ms, and AHK's worst max in this report is 62.92 ms. There is nothing in the bench that shows whose thread stalled. That is follow-up #83.

## Trigger to first event

10,000 board presses of F13 per run. The time is hook F13 down to hook F14 down. SendInput F13 is 0 hits on both engines, as in #39, because both ignore injected input.

| Engine | Run | Hits | Misses | p50 | p95 | p99 | max |
|---|---|---|---|---|---|---|---|
| AHK | 1 | 9601 | 399 | 0.59 | 1.27 | 2.31 | 34.56 |
| AHK | 2 | 9578 | 422 | 0.78 | 1.51 | 2.47 | 17.75 |
| AHK | 3 | 9408 | 592 | 0.97 | 1.75 | 2.23 | 9.57 |
| Native | 1 | 10000 | 0 | 0.21 | 0.62 | 1.04 | 1.77 |
| Native | 2 | 10000 | 0 | 0.12 | 0.33 | 0.54 | 2.34 |
| Native | 3 | 10000 | 0 | 0.16 | 0.54 | 0.95 | 1.58 |

AHK's misses are all F13 seen with no F14 inside 80 ms (one run also had one press with no F13). Native did not miss once in 30,000 presses, and its median is a third of AHK's or less.

## CPU

Armed, no runner, 60 seconds. Then 1, 4, and 11 repeat macros on one shared F13 trigger. `cpu_pct` 100 is one core. Hook events is every watched key edge in the window.

| Case | Engine | Run | cpu_pct | switches/s | hook events |
|---|---|---|---|---|---|
| Idle armed | AHK | 1 | 1.81 | 226 | 0 |
| Idle armed | AHK | 2 | 3.52 | 461 | 0 |
| Idle armed | AHK | 3 | 4.52 | 489 | 0 |
| Idle armed | Native | 1 | 3.15 | 734 | 0 |
| Idle armed | Native | 2 | 3.10 | 614 | 0 |
| Idle armed | Native | 3 | 3.24 | 670 | 0 |
| 1 macro | AHK | 1 | 34.82 | 9942 | 39946 |
| 1 macro | AHK | 2 | 36.65 | 9511 | 51843 |
| 1 macro | AHK | 3 | 36.25 | 9218 | 51882 |
| 1 macro | Native | 1 | 16.94 | 3435 | 77080 |
| 1 macro | Native | 2 | 12.69 | 3002 | 59835 |
| 1 macro | Native | 3 | 14.93 | 3195 | 62773 |
| 4 macros | AHK | 1 | 38.99 | 10510 | 105048 |
| 4 macros | AHK | 2 | 39.13 | 9129 | 132123 |
| 4 macros | AHK | 3 | 40.02 | 9267 | 137951 |
| 4 macros | Native | 1 | 24.33 | 6953 | 193749 |
| 4 macros | Native | 2 | 21.39 | 7228 | 201291 |
| 4 macros | Native | 3 | 21.01 | 7198 | 199586 |
| 11 macros | AHK | 1 | 39.71 | 10464 | 163507 |
| 11 macros | AHK | 2 | 38.82 | 8891 | 191713 |
| 11 macros | AHK | 3 | 41.01 | 9163 | 195432 |
| 11 macros | Native | 1 | 29.58 | 8986 | 253292 |
| 11 macros | Native | 2 | 26.32 | 11559 | 334062 |
| 11 macros | Native | 3 | 25.67 | 11540 | 330051 |

Under load, native uses less CPU while sending more. At 11 macros it sent 1.5 to 1.7 times the key edges of AHK, at about two thirds of the CPU. Its switches per second in runs 2 and 3 are higher than AHK's, but per key edge they are lower: about 2.1 per edge against about 2.8 for AHK. The extra wakeups come with the extra output.

Idle CPU is level: 3.10 to 3.24 percent on native, 1.81 to 4.52 on AHK. Idle wakeups are not. Native has 614 to 734 context switches a second against 226 to 489. That is the design in `docs/engine/architecture.md`. While armed, the playback thread waits at most 8 ms (`:152`) and the IPC thread wakes at least every 8 ms to poll the foreground window (`:85`). That is two threads at 125 wakes a second or more each, where AHK has one thread. The cost does not show up as CPU on this PC, but it is a worse number, so it is follow-up #82.

## Board

Output mode `rp2350`, the F14 spam macro for 60 s. Emit is the engine's hid message (AHK) or `report` frame (native) being read, to the last `ok` for that report. Line is each `K` line to its reply. Milliseconds.

| Case | Engine | Run | n | p50 | p95 | p99 | max | lines/s | `hid-busy` |
|---|---|---|---|---|---|---|---|---|---|
| Emit to ok | AHK | 1 | 48921 | 0.137 | 0.329 | 0.419 | 1.016 | 815 | |
| Emit to ok | AHK | 2 | 66222 | 0.149 | 0.372 | 0.766 | 1.855 | 1104 | |
| Emit to ok | AHK | 3 | 68689 | 0.150 | 0.370 | 0.628 | 1.394 | 1145 | |
| Emit to ok | Native | 1 | 79392 | 0.142 | 0.362 | 0.760 | 2.255 | 1323 | |
| Emit to ok | Native | 2 | 63224 | 0.135 | 0.315 | 0.467 | 1.300 | 1054 | |
| Emit to ok | Native | 3 | 65132 | 0.138 | 0.333 | 0.537 | 1.352 | 1086 | |
| Line to ok | AHK | 1 | 48921 | 0.132 | 0.325 | 0.413 | 1.012 | 815 | 0 |
| Line to ok | AHK | 2 | 66222 | 0.144 | 0.366 | 0.755 | 1.851 | 1104 | 0 |
| Line to ok | AHK | 3 | 68689 | 0.144 | 0.365 | 0.621 | 1.390 | 1145 | 0 |
| Line to ok | Native | 1 | 79392 | 0.140 | 0.359 | 0.758 | 2.254 | 1323 | 0 |
| Line to ok | Native | 2 | 63224 | 0.132 | 0.312 | 0.456 | 1.296 | 1054 | 0 |
| Line to ok | Native | 3 | 65132 | 0.135 | 0.330 | 0.529 | 1.350 | 1086 | 0 |

The board line is level. Medians are 0.13 to 0.15 ms on both. Line p99 is 0.41 to 0.76 ms on AHK and 0.46 to 0.76 on native. Native run 1 has the highest line p99 (0.758 against AHK's 0.755) and the highest max (2.25 ms). Emit to ok is within 0.006 ms of the line time on both, so the engine adds nothing that shows here. The line itself is the bench's serial write and the board's reply, the same code for both engines. Native run 1 also pushed the most lines, 1323 a second. Engine CPU during the minute was 11.8 to 13.0 percent on native and 26.3 to 28.4 on AHK. There was not one `hid-busy` in 391,580 lines.

## Pipe

Ack is the round trip. Gap is the F14 down-to-down interval that crosses that ack while a wait-5 macro runs. The padded profile is 83231 bytes. Reload and the full file (83266 bytes) run while disarmed. Milliseconds.

| Case | Engine | Run | ack | gap |
|---|---|---|---|---|
| Padded profile, macro running | AHK | 1 | 370.8 | 376.0 |
| Padded profile, macro running | AHK | 2 | 609.8 | 618.0 |
| Padded profile, macro running | AHK | 3 | 713.2 | 721.3 |
| Padded profile, macro running | Native | 1 | 2.8 | 7.3 |
| Padded profile, macro running | Native | 2 | 6.9 | 7.5 |
| Padded profile, macro running | Native | 3 | 7.1 | 7.8 |
| Reload `default.json`, disarmed | AHK | 1 | 456.8 | |
| Reload `default.json`, disarmed | AHK | 2 | 729.4 | |
| Reload `default.json`, disarmed | AHK | 3 | 760.6 | |
| Reload `default.json`, disarmed | Native | 1 | 8.9 | |
| Reload `default.json`, disarmed | Native | 2 | 9.5 | |
| Reload `default.json`, disarmed | Native | 3 | 10.0 | |
| Full profile message, disarmed | AHK | 1 | 479.9 | |
| Full profile message, disarmed | AHK | 2 | 765.3 | |
| Full profile message, disarmed | AHK | 3 | 754.4 | |
| Full profile message, disarmed | Native | 1 | 10.5 | |
| Full profile message, disarmed | Native | 2 | 10.6 | |
| Full profile message, disarmed | Native | 3 | 9.7 | |

This is the reason for the rewrite. A profile update stops an AHK macro for 376 to 721 ms. On native the running macro's loop stays at its usual 7.3 ms across the update, because the IPC thread parses and playback adopts the image on a later tick. The pipe-gap rows over all native runs never exceeded 8.04 ms.

## Conclusions

- **Jitter**: native is better at every wait size on p50, p95, and p99, with less CPU. One 70.9 ms stall in one run is the only max worse than AHK's. Follow-up #83.
- **Trigger to first event**: native is better. The median is a third of AHK's or less, and there were 0 misses against 399 to 592.
- **CPU under load**: native is better. 11 macros take 26 to 30 percent of a core against 39 to 41, while sending more.
- **Idle**: CPU is level. Wakeups are higher on native because two threads each wake every 8 ms while armed. Follow-up #82.
- **Board**: level. The line is board and serial time, the same for both.
- **Profile updates**: native is better by about two orders of magnitude.
- The board mouse dpi difference that `docs/engine/architecture.md:230` asked about did not come up. The bench macros do not move the mouse, so it was not measured.

#54 step 1 can go ahead once the two follow-ups are triaged. Neither one blocks a day-to-day trial: one is a wakeup count with no CPU cost here, and the other did not repeat in eight other jitter rows.

## Reproducing

From the repo root, with the app closed and a harmless window focused (the board types F13 to F24):

```powershell
cmake -S engine-native -B engine-native/build -A x64
cmake --build engine-native/build --config Release
powershell -NoProfile -ExecutionPolicy Bypass -File tools\bench\run.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File tools\bench\run.ps1 -EngineExe engine-native\build\macroui-engine.exe
```

The results are `tools/bench/out/results.csv` and `tools/bench/out/results-native.csv`, which are not in git. One full pass of both took 170 minutes on this PC.
