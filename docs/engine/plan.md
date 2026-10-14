# Engine rewrite plan

This is the working plan for the native engine. GitHub issues #37-#54 are the task list. This file is the corrected order and the behaviour rules. When an issue body disagrees with this file, follow this file and update the issue to match.

`engine-native/` is the empty C17 program from #41. It prints `macroui-engine 0` and exits. It is not the resident engine. Phase 0 started at #38.

## Already closed

These are done on `main`. Do not reopen them, and do not port the old behaviour.

| Issue | What it was | Where the fix lives |
|---|---|---|
| #13 | Board lock could stall the UI, and a missed key-down was dropped | Shell board path. Keep "a down may queue, an up waits on the lock". |
| #14 | Hook watchdog only re-raised the keyboard hook | Engine. Keep the mouse re-raise. |
| #17 | A key-up after `hid-busy` could be lost | Firmware retry. Keep it. |
| #26 | Fixed / no-delay recording skipped Random Wait | Editor, `ui/src/recording.ts`. The engine only sees the steps the editor emits. |
| #27 | Scroll direction flipped at 0 notches | Editor. The engine receives `dy`. |
| #34 | Deleted macros left stale Run targets | Editor save checks. The engine no-ops a missing target. |
| #35 | An empty Repeat crashed the editor | Editor. The engine treats a missing `steps` array as empty. |

No open issue in this rewrite is a leftover bug from that list. Block ideas (#15, #18, #22, #28, #29-#33, #36) stay out of scope. The step format must not block them.

## Order

| Phase | Issues | Done when |
|---|---|---|
| 0 Analysis and decisions | #38, #39, #40, #41 | Analysis doc exists, AHK numbers are in from the Windows PC, language is chosen from those numbers, architecture doc and an empty native project build in CI. |
| 1 Spec | #42 | Byte layout, hello handshake, and hex test vectors. No shell code. |
| 2 Core engine | #43-#48, and the harness slice of #49 | Fake-clock harness is in CI before the first block port. Clock, hooks, output, compiler, profile load, and process lifecycle match AHK. |
| 3 Integration | rest of #49, then #50-#53 | Trace diffs are empty or listed. Shell can switch engines. Performance report is in. |
| 4 Cutover | #54 | Native is the default, then AHK is removed after a tagged release that still has it. |

Phase 0 owns #41 (architecture and an empty project). Phase 1 is #42: `docs/engine/ipc-protocol.md` and `docs/engine/ipc-vectors.txt`. The shell code for that spec is #50.

The #49 fake-clock harness lands before the first block port in #46. The trace corpus finishes in phase 3. Neither issue waits for the other to be fully closed.

## Behaviour the port must match

- Play modes: `once`, `repeat` (uses `repeatCount`), `whileHeld`, `toggle`, `onRelease`.
- `advanced` compiles `blocks`. Otherwise the runner uses `steps`. A `tapHold` or `tapSpam` block forces the trigger mode to `whileHeld`.
- `busy` ignores a second press. `enabled` false stops the runner. Macro `releaseStop` is `nextUp` or `finish`.
- `speed` scales waits, with a floor of 0.05.
- `scanWait` ends the runner when the trigger finger is up.
- `run` starts the target as `once` and does not restart it if it is already running. There is no cycle guard. Do not add one during the port.
- `exclusive` calls `StopAll`. `priority` is stored and ignored. Leave it ignored.
- `variables` is stored and ignored. `pressHold` is legacy: the editor rewrites it, the engine still compiles a raw file that contains it.
- `activeId` is already on the state snapshot. Command acks do not echo an id.
- Output mode is the shell `output` message (`software` / `rp2040` / `rp2350`). `profiles/shell.json` currently stores only the close mode.
- Profile path today is `A_ScriptDir\..\profiles\default.json`. The shell will pass `--profile` with the full path. Do not keep the relative default for the new exe.
- Own output is tagged `dwExtraInfo = 0x56454E44`.
- Panic is the Pause key. It calls `StopAll`.
- Publish before dropping a finished runner.
- Delay modes (recorded / fixed / no delay) stay in the editor.

## Decisions already made

- No kernel driver. Low-level hooks only.
- Do not raise the process integrity level.
- Both engines share `Local\MacroUI.Engine`. A second process exits if the mutex is held. Do not copy `#SingleInstance Force` (that kills the running engine and releases keys).
- Switching engines is: release keys, quit, wait until the mutex is free, then start the other.
- Hot path uses integer QPC ticks. The rounding rule that replaces AHK `Round` after dividing by `speed` is written in the architecture doc, or listed as an accepted parity diff.
- `WindowListJson` and foreground lookup do not run on the playback thread.
- The native process is Per-Monitor V2 DPI aware.
- `randomWait` is compared as "inside min/max", or the test injects the roll. It is not an exact-interval benchmark.
- Jitter, trigger latency, and board timing are measured on the Windows PC, from a separate process. The board probe is the serial line, not a timer inside AHK. `windows-latest` can build and can run `--self-test`. It is not the jitter report.

## What needs the Windows PC

| Work | PC required |
|---|---|
| #38 analysis doc | No. It is a reading of `engine/MacroEngine.ahk`. |
| #49 first slice: run the existing AHK `--self-test` in CI, and a fake-clock harness | No, once the analysis rules exist. |
| #39 baseline numbers | Done on this PC. `docs/engine/benchmarks.md`. |
| #40 language micro-benchmarks | Done on this PC. `docs/engine/decision-language.md`. C17. |
| #41 architecture and empty project | No. `docs/engine/architecture.md`. CI builds `engine-native/`. |
| #42 protocol spec | No. `docs/engine/ipc-protocol.md`. |
| #53 comparison | Yes, same PC, same harness. |
| Hooks, SendInput, and the board | Yes. Linux CI covers the core with a fake clock. |

## Next step

Output is #45. The report image is `engine-native/src/ipc/report.c`. The diff, the release, and the mode change are `engine-native/src/core/output.c`. Software is one `SendInput` in `engine-native/src/win/output_win.c`, tagged `dwExtraInfo = 0x56454E44`. The shell writes the `K` / `M` lines from that same image in `shell/ReportBoard.cs` and keeps the port. `main` does not call them. #46 is next: the block compiler and the runner. The #49 fake-clock harness lands before the first block port. The language stays C17. MMCSS stays off. `timeBeginPeriod` stays off. Playback priority is the table in `docs/engine/architecture.md`. The language-bench pin stays a measurement. The hooks stay `engine-native/src/core/hook.c` and `engine-native/src/win/hook_win.c`.
