# Native architecture

#41. The language is C17, from `docs/engine/decision-language.md`. That choice stays closed. MMCSS stays off. The measurement pin in that doc is not the playback priority.

Behaviour the port must match is in `docs/engine/plan.md` and `docs/engine/ahk-analysis.md`. This file is the threads, the ordering, the clock, and the tree. The runner for the first steps is `engine-native/src/core/runner.c`. The byte layout of pipe messages is `docs/engine/ipc-protocol.md`.

`engine-native/` on Linux prints its version and exits. On Windows, #48 is the resident process: it takes the mutex, starts the three threads, and opens the pipe. `--self-test` prints the version and does not take the mutex.

## Threads

Three threads. The hook thread never waits on the other two. Each arrow has one writer and one reader.

```
hook thread                    playback thread                 IPC thread
message pump                   tick, then the high-res wait    pipe, parse, windows
    |                                  |                            |
    |-- event ring (1024) ------------>|                            |
    |<-- echo ring (64) ---------------|                            |
    |<-- swallow map (latest) ---------|                            |
    |                                  |<-- commands (64) + quit ---|
    |                                  |<-- profile image -----------|
    |                                  |-- state snapshot ---------->|
    |                                  |<-- foreground snapshot -----|
```

| Thread | Owns | Does not |
|---|---|---|
| Hook | `WH_KEYBOARD_LL`, `WH_MOUSE_LL`, their message loop, the swallow return | Play a macro, parse, walk windows, call `SendInput`, take a lock |
| Playback | Runners, the clock, `SendInput`, the report image, trigger decisions | Format text, parse JSON, call `GetForegroundWindow`, install a hook |
| IPC | The pipe, JSON, profile load, foreground, the window list | Advance a runner, call `SendInput`, install a hook |

The core is the playback work that is plain C: the step walk, the key array, the waits as integer ticks. The playback thread calls into that core and passes `now` in. The core does not read the clock and does not include Win32. Linux CI compiles that core. A later fake clock is a caller that passes ticks.

The serial port stays in the shell. Playback emits one report image per publish. On a board, that image is the `report` frame in `docs/engine/ipc-protocol.md`. The engine does not open the port.

## How they share state

Lock-free. No mutex on these paths. `Local\MacroUI.Engine` is the process lifetime mutex, not a data lock.

| Path | Shape | Writer | Reader |
|---|---|---|---|
| Keys and mouse buttons | Ring of 1024 events | Hook | Playback |
| Board echo | Ring of 64 tokens | Playback | Hook |
| Swallow and pass | Two maps, one published index | Playback | Hook |
| Commands | Ring of 64, plus a quit flag | IPC | Playback |
| Profile | Immutable image, sequence number | IPC | Playback |
| State | One snapshot, latest wins | Playback | IPC |
| Foreground | One snapshot, latest wins | IPC | Playback |

A ring is a power-of-two buffer. The writer stores the slot, then release-stores the tail. The reader acquire-loads the tail, reads the slot, then release-stores the head. A published snapshot is two copies. The writer fills the inactive copy, then release-stores the index or the sequence. The reader acquire-loads that index and uses only that copy.

Latest-wins is right for state, swallow, and foreground. It is wrong for commands. `start`, `stop`, `reload`, output mode, and arm stay in the command ring in parse order. `quit` is also an atomic flag, so a full ring cannot drop it.

If the event ring or the command ring is full, the writer sets an overflow latch and returns. The hook still returns immediately. On the next tick the playback thread calls `StopAll` and releases keys. A missed key-up is worse than a stopped macro. Echo slots that expire are dropped by the hook. A real tap after the 150 ms window is a finger.

The profile image is allocated on the IPC thread. After playback adopts it, the image is immutable. Playback stores the adopted sequence. The IPC thread frees an image only after that sequence is newer. The tick does not allocate and does not free.

## Ordering

A hook event that arrives during `Advance` is invisible until the next tick. That is the rule the single AHK thread got for free, because a low-level hook runs only while that thread pumps. Here the hook thread runs all the time, so the playback thread has to draw the line itself.

One tick, in this order:

1. If the quit flag is set, release keys and leave. The IPC thread sends the command ack when it parses `quit`, and it signals the playback wake. The release runs on this thread, so it follows the ack by at most the rest of the current wait. The mutex stays held until the release finishes. This tick does not start runners.
2. Adopt a newer profile image. Apply the command ring in order.
3. Read the foreground snapshot. This tick's focus, dpi, and goto rect are that snapshot.
4. Read the hook health stamps. A re-raise is a flag for the hook thread. This thread does not call `SetWindowsHookEx`.
5. Drain the event ring into the finger snapshot. On overflow, `StopAll` and release keys before triggers.
6. Pause, on the down edge, calls `StopAll`. It is not a trigger.
7. If armed, poll triggers from the finger snapshot.
8. Remove finished runners, then flush pending.
9. Advance each runner. One failure marks that runner done. The loop continues.
10. Flush pending again. A `run` started in step 9 is in the list and has not advanced.
11. Publish output, then remove finished runners, then write the state snapshot.
12. Publish the swallow map.

`Publish` stays before the second remove, so a one-shot move or wheel on the last step is still sent. `MarkDone` clears holds and leaves the move and the wheel for that publish.

Steps 5 through 12 do not look at the ring again. Commands and keys that arrive during those steps wait for the next tick.

Swallow is decided in the callback, from the map published at the previous step 12. The callback cannot change its mind after it returns. A focus change is visible to the callback on the wait that follows the tick that published the new map.

The foreground snapshot can be one IPC poll behind the real window. The IPC thread polls the foreground exe, dpi, and rects, and publishes on change. While armed it wakes at least every 8 ms so a focus change does not wait for a pipe message. The window list is not part of that poll.

## Hook callback

The callback reads the struct, writes at most one event, and returns. `QueryPerformanceCounter` is the stamp. `SetEvent` is the wake, and only when playback has asked to be woken.

In order:

1. Mouse move, vertical wheel, and horizontal wheel return through `CallNextHookEx`. They are not events and they do not wake playback.
2. `dwExtraInfo == 0x56454E44` returns through `CallNextHookEx` and is not a finger. That marker is on every `INPUT` this process sends, keyboard and mouse.
3. Injected input returns the same way. Keyboard flags `0x12`, mouse flags `0x03`.
4. A board echo that matches a token is consumed, passed through, and not enqueued. The token's window is 150 ms of QPC. The match is this list, not a walk of the profile.
5. Otherwise resolve the key id, enqueue, and wake if playback asked.
6. The keyboard hook returns 1 when the published map says swallow. Middle and both side buttons return 1 from that same map, and only software output sets the bit. Board output passes those clicks. Left and right always call `CallNextHookEx` and never start a macro. Move and wheel always call `CallNextHookEx`.

Side keys are resolved here. Generic shift, ctrl, alt, and win become the left or right virtual key from the scan code and the extended bit. Right shift is scan `0x36`. Numpad Enter is virtual key `0x0D` with the extended bit, and its key id is 256. Every other key id is the virtual key, 1 through 255. Mouse buttons use the name-table ids 257 through 261. Those ids are not virtual keys.

There is no `RegisterHotKey`. Software swallow and board swallow are both the return from this callback. The pass count is a bit in the same map, published with it. Middle and both side buttons use that return for software only. A board passes those clicks. Left and right are not triggers: the click always passes and the macro never starts. `SendInput` from this process is echo `0x56454E44` and is not a finger. Injected mouse flags `0x03` are not a finger either.

The hook thread is the thread that installed the hooks, and it is the thread that reinstalls them. Reinstall runs between messages, not inside the callback. Unhook first. A handle Windows has already removed still looks valid. Playback posts that thread a message whenever it publishes a raise. The keyboard hook is installed with the thread. The mouse hook is installed only while a live macro triggers on middle or a side button, and it is removed when none do. A quiet hook stays up. Silence while playing is normal, and a mouse move is not a hook event, so neither one is a reason to reinstall. The hook comes back when its handle is missing, or when the input desktop returns after the lock screen.

A finger stays down until the hook reports the up. Windows auto-repeats only the most recently pressed key, so a held key that has stopped repeating is still held. A second down of mouse 4 or mouse 5, while that button is already down, is a missed up, and the new down lands after the rest of that batch. An up later in the same batch cancels it, so a click does not stay held. Left, right, middle, and a keyboard auto-repeat are not a missed up. GetAsyncKeyState is not a release. A key the hook swallows never shows up there, so that call would clear a trigger on the same tick it was pressed. The lock screen clears every finger. Returning from it reinstalls the hooks and leaves a key down until the hook says otherwise. A piped profile is parsed on the IPC thread. AutoHotkey parses a profile on the hook thread and reinstalls both hooks once after that parse. Native leaves the hooks up across a profile update. The live config is chosen like AutoHotkey: case-insensitive file name, first `focusExe` match, and an empty `focusExe` falls back to `activeId`. A change of that config reseeds triggers. The chosen name, id, focus, and foreground exe are written to `profiles/engine.log`. An empty trigger button is not a key. It does not arm, it does not swallow, and it does not match a finger. Mouse 4 and mouse 5 share one path. The button number is the high word of the hook data, or the low word when the high word is 0.

The watchdog rules in the analysis stay. Idle does not re-raise. The stamps live on the hook thread. The decision runs in step 4. That decision is `engine_hook_watch` in `engine-native/src/core/hook.c`. The hook thread reads the raise flags between messages and unhooks before it installs. `engine-native/src/win/hook_win.c` is that install. The resident process starts that thread. The playback thread does not call it.

## Playback priority

| | Priority | Affinity |
|---|---|---|
| Process | `NORMAL_PRIORITY_CLASS` | None |
| Hook thread | `THREAD_PRIORITY_ABOVE_NORMAL` | None |
| Playback thread | `THREAD_PRIORITY_NORMAL` | None |
| IPC thread | `THREAD_PRIORITY_BELOW_NORMAL` | None |

The hook thread blocks in `GetMessage`. `ABOVE_NORMAL` is so a parse on the IPC thread does not delay the callback inside `LowLevelHooksTimeout`. The playback thread blocks on the timer. Its priority does not turn a 1 ms wait into 1 ms. The IPC thread sits below both so a profile parse yields.

The language bench pinned logical processor 2 and used `HIGH_PRIORITY_CLASS` with `THREAD_PRIORITY_HIGHEST`. That was the measurement. Playback does not do it.

MMCSS stays off. The playback thread does not call `AvSetMmThreadCharacteristicsW`. `timeBeginPeriod` is not the clock and is not called.

Integrity level stays where the process started. No kernel driver.

## Clock

The wait is an unnamed `CreateWaitableTimerExW` with `CREATE_WAITABLE_TIMER_HIGH_RESOLUTION`, no completion routine. Playback waits on that handle and the IPC wake. It also waits on the hook wake when the table says input wakes playback. It does not run a message loop. A message loop on this thread would let a send re-enter the tick.

The hook thread is already pumping, so an overdue runner does not have to spin the message queue. It still must not busy-spin. After every tick:

| State | Wait | Input wakes playback |
|---|---|---|
| Disarmed, nothing running, nothing pending | 16 ms | No |
| Armed, nothing running, nothing pending | 8 ms | Yes |
| Otherwise | Until the next deadline, clamped to 1..8 ms | Yes |

A deadline that is already due still waits 1 ms. The longest slice is 8 ms.

A requested 1 ms wait landed near 1.52 ms in the language measurement. The clock will not schedule a 1 ms edge as 1 ms. The AHK 5 ms self-test band is an AHK check. It is not a promise that the native tick matches that float.

Deadlines are absolute QPC ticks. The core compares integers the caller passes in. The pace decision and the rounding line are `engine-native/src/core/clock.c`. The wait is `engine-native/src/win/clock_win.c`. The hook handle is waited only when the table says input wakes playback. The IPC handle is waited on every slice, including the 16 ms disarmed slice.

## Rounding

AutoHotkey v2 `Round` with the places argument omitted is half away from zero. `Round(0.5)` is 1, `Round(2.5)` is 3, `Round(-1.5)` is -2. It is not half to even.

The tick compare is integer. The division by `speed` happens once, when the wait is armed, in double, because that is what AHK divides:

```
scaled = floor((ms / speed) + 0.5)
if scaled < 1: scaled = 1
delta  = scaled * qpc_freq / 1000
```

`ms` is non-negative. `speed` was already raised to 0.05 when the runner was built. `floor` is the positive half of `Round`. `delta` is 64-bit integer division, toward zero. That truncation is the accepted difference from AHK's float `now + milliseconds`. The compare does not round again.

The same half-away rule is used where the analysis calls `Round` on a coordinate, a wheel notch, or a raw millisecond. A negative coordinate uses `ceil(x - 0.5)`.

These are not divided by `speed`: `ifShort` `underMs`, `swapLoop` `afterMs`, `move`, `scroll`, the OS key delay and repeat, the 5 ms edge poll, and the 1 ms yield. A wait that comes from those constants still uses the `delta` line, with `scaled` already known.

`burst` `forMs` is divided by speed. The duration becomes ticks with truncation toward zero. It does not use the wait minimum of 1. `0` or less runs the children once.

`onceHeld` `gapMs` is divided by speed when the frame is built. A positive gap then uses the wait minimum of 1. `0` or less does not wait. The 1 ms poll, the empty-body yield, and the `releasedNow` yield are not divided by speed.

`edgeHold` `armMs` and `gapMs` are divided by speed at the wait. `holdMs` and `restMs` are divided when the frame is built, with the wait minimum of 1. The 5 ms poll, the key delay, and the key repeat are not divided by speed. The runner uses the dry-mode delay of 400 ms and repeat of 33 ms. It does not call `SystemParametersInfo`.

`scanWait` `ms` is divided by speed at the wait, through the same arm path as `wait`. `0` still waits 1 ms.

`pressHold` does not divide. It stores a wait of 18 and `holdMs` on the steps it writes. Those waits use the arm path. `holdMs` defaults to 200 when the file omitted it. That default is not the editor's 80.

`randomWait` is inside the inclusive min and max after the bounds are swapped and clamped at 0. The test injects the roll. It is not an exact-interval check.

## Data

Profile load may allocate. The tick, the callback, the step walk, the key lookup, and the report image do not.

A compiled step is a fixed record. Child lists are a start index and a count in the same array. A step holds no pointer and no string. `run` stores a macro index resolved at load. A missing target is a no-op. There is no cycle guard.

```
type, action     uint8
key              uint16 key id
a, b             int32
first, count     uint32 child span
```

The numeric `type` values arrive with the compiler. The kinds are the ones the analysis already lists.

Key id 0 is empty. Ids 1..255 are the virtual key after side resolution. Id 256 is Numpad Enter. Mouse buttons are five slots beside that array: left, right, middle, X1, X2. They are not virtual keys.

`Alias`, `NormKey`, and `SafeKey` run at load. The tick does not look up a name. More than one macro may share a key. `trigger_head` has `ENGINE_KEY_ID_MAX + 1` slots, which is 262. That covers virtual keys, Numpad Enter, and the five mouse ids. Each entry is a 1-based index into a chain built at load, or 0. The chain is profile order. The tick walks it.

`applied` stores key ids. The IPC thread turns them into the names on the state message. The playback thread does not format that text. The name table is filled once at startup.

The report image is fixed and little-endian. Its fields are the `report` payload in `docs/engine/ipc-protocol.md`. One publish produces one image. The bytes are `engine-native/src/ipc/report.c`. The diff that fills it is `engine-native/src/core/output.c`. On the software path that image becomes one `SendInput` call in `engine-native/src/win/output_win.c` and is not written to the pipe. A board mode is the same image, written as a `report` frame. The shell writes the `K` / `M` lines. Playback calls `SendInput` while the mode is software, and again to release keys on exit. A board exit queues the ups and does not call `SendInput`.

## Process

Resident startup on Windows (#48). Linux still prints the version and exits.

1. `SetProcessDpiAwarenessContext` to Per-Monitor V2, before any dpi query.
2. `CreateMutexW` on `Local\MacroUI.Engine`, initial owner true. If the mutex already exists, exit 2. Do not kill the holder and do not release its keys.
3. Start the IPC thread, the hook thread, then the playback thread.
4. No console window. The Windows exe is built with the Windows subsystem.

`--self-test` prints `macroui-engine 0` and returns before step 2. `--bench` returns before step 2 and does not measure. `--profile` stores the path. The IPC thread opens that path before it creates the pipe. There is no relative default, and a missing file is not created. A missing value or any other argument exits 1 before the mutex. Both engines share the mutex name. Switching engines is: release keys, quit, wait until the mutex is free, then start the other.

`ERROR_ALREADY_EXISTS` closes only the new handle. It does not call `ReleaseMutex`. AutoHotkey still does not check that error. `#SingleInstance Force` only sees another AutoHotkey script, so launching `MacroEngine.ahk` by hand can still run beside the native process. The shell's `EngineMutexHeld` will not start a second engine while the name is taken. `FindEngineProcess` adopts `AutoHotkey64` for AHK and `macroui-engine` for the native engine. Do not run both engines.

The hook thread is `engine_win_hook_run`. The playback thread adopts a newer profile image, reads the foreground snapshot the IPC thread published, and picks the live config. It applies the command ring, drains hook events, and, while armed, polls triggers and advances runners. It does not query a window. Software output calls `SendInput` on this thread. A board report is queued for the IPC thread. The disarmed idle slice stays 16 ms. Quit releases applied keys, then the thread leaves. The IPC thread creates `\\.\pipe\MacroUI` with the flags in `docs/engine/ipc-protocol.md`. It reads `--profile` before the pipe loop and queries the foreground image, owner, dpi, and rects on that thread. On connect it writes `hello`, then one `state`. Later publishes write `state` when the snapshot or the window list changes, and write `report` when playback queued one. `command` `quit` writes the command ack, then sets the quit flag. A first byte of `{` gets `ack` `bad frame` and is not applied, so a JSON quit does not stop the process. `start` arms. `stop` disarms and calls `StopAll`. `reload` re-reads `--profile` here. `profile` publishes a JSON object and applies nothing else. `output` is queued with `start` and `stop`. A corrupt profile keeps the last good image, or an empty one, and stores the detail `the saved profile could not be read`. A failed `reload` sends that `error` frame, then the command ack. A failed load before a client is connected does not wait to deliver the error.

The board scale uses the foreground dpi from the snapshot, and the system dpi when the window has none. Per-Monitor V2 can change that number relative to the unaware AHK process. If #53 shows a difference, record it there. The process stays Per-Monitor V2.

Exit releases keys, asks the hook thread to unhook, closes the timer, the pipe, and the mutex, in that order. Software ups go through `SendInput`. A board mode clears applied and does not call `SendInput`. The port stays in the shell. `timeBeginPeriod` is not called, so there is no `timeEndPeriod`. An unhandled exception runs that same order and exits. A hidden top-level window and a console control handler set the quit flag for session end and close. There is no crash dump. `TerminateProcess` cannot run the exit order, so a hard kill can leave software keys down. The shell's kill stays a last resort.

Thread priorities are the table above. The process stays `NORMAL_PRIORITY_CLASS`. MMCSS stays off. Integrity level stays where the process started.

## Tree

```
engine-native/
  CMakeLists.txt
  src/main.c              Linux prints the version. Windows calls win_entry
  src/core/version.h
  src/core/version.c      portable, Linux CI compiles this
  src/core/process.h
  src/core/process.c      args, boot order, exit order, quit and ack bytes
  src/core/clock.h
  src/core/clock.c        pace, half-away rounding, tick deltas
  src/core/hook.h
  src/core/hook.c         events, echo, swallow map, watchdog decision
  src/core/output.h
  src/core/output.c       applied diff, send plan, board plan
  src/core/runner.h
  src/core/runner.c       steps, the runner, publish before remove
  src/core/command.h
  src/core/command.c      frames, the command ring, trigger poll, state bytes
  src/core/json.h
  src/core/json.c         profile JSON, AHK parse quirks
  src/core/profile.h
  src/core/profile.c      compile, live pick, image swap
  src/ipc/report.h
  src/ipc/report.c        156-byte report image
  src/win/win_entry.c     resident process, mutex, pipe, exit
  src/win/clock_win.h
  src/win/clock_win.c     high-res wait
  src/win/hook_win.h
  src/win/hook_win.c      low-level hooks
  src/win/output_win.h
  src/win/output_win.c    one SendInput from the report image
  tests/clock_test.c      portable clock checks
  tests/clock_wait_test.c Windows wait check
  tests/hook_test.c       portable hook checks
  tests/hook_win_test.c   Windows hook struct and install check
  tests/output_test.c     portable report, diff, and line plan
  tests/output_win_test.c Windows SendInput fill
  tests/runner_test.c     fake clock, the basic steps, run, goto, holdLoop, swapLoop, burst, ifShort, onceHeld, edgeHold, scanWait, and the pressHold expansion
  tests/process_test.c    args, boot order, exit order, quit frame
  tests/process_win_test.c mutex, a second instance, quit, key release
  tests/profile_test.c    profile compile, live pick, corrupt file
  bench/trace.c           same profile and script, fake clock, no SendInput
  bench/profiles/         one profile per block, combos, and a shaped profile
  bench/cases/            the input script for each profile
  bench/expected/         native output. Windows CI diffs AHK against it
  bench/order.txt         case order
  bench/approved.txt      stems that may differ. No stems means AHK must match
```

`engine/` stays the AHK engine until #54.

`src/core` is the interpreter, the lookup, and the compiler. It includes no `windows.h`. The clock math, the hook decision, the output plans, the runner, and the profile compiler are there. `src/win` is the timer, the hooks, `SendInput`, the pipe, the mutex, and the foreground query. `src/ipc` holds the report image. The other frames stay in `docs/engine/ipc-protocol.md` until a later issue encodes them. The pipe call itself stays in `src/win`. `tests/clock_test.c` is the #43 clock check. `tests/hook_test.c` is the #44 hook check. `tests/output_test.c` is the #45 report check. `tests/runner_test.c` is the fake-clock harness and the first #46 steps. `tests/profile_test.c` is the #47 profile check. `bench/` is the trace corpus. `engine-trace` plays it on the fake clock and does not call `SendInput`. Linux CI checks that output against `bench/expected`. Windows CI runs AHK `--trace` and diffs it. The diffs are empty. The language benches stay in `tools/bench/lang/`.

## Build

C17, CMake, x64, one executable, `macroui-engine`. MSVC uses the static CRT and `/experimental:c11atomics`, because that is what turns on `stdatomic.h`. Windows CI is MSVC on `windows-latest`. Linux CI is the image C compiler and builds `src/main.c`, `src/core`, and `src/ipc`. It does not build `src/win`. clang-cl is not required.

Linux prints `macroui-engine 0` and exits 0. Windows does that for `--self-test` only. With no arguments it is the resident process above. The version string stays `macroui-engine 0`.

## Later issues

| Issue | Owns |
|---|---|
| #42 | Frame bytes, hello handshake, hex vectors. Written in `docs/engine/ipc-protocol.md`. The port stays in the shell. |
| #43 | Timing loop and clock. `src/core/clock.c` and `src/win/clock_win.c`. |
| #44 | Hooks, trigger bits, and the watchdog. `src/core/hook.c` and `src/win/hook_win.c`. |
| #45 | Output. `src/ipc/report.c`, `src/core/output.c`, `src/win/output_win.c`, and `shell/ReportBoard.cs`. |
| #46 | Block compiler and runner. `src/core/runner.c` runs the basic steps, `goto`, `run`, `holdLoop`, `swapLoop`, `burst`, `ifShort`, `onceHeld`, `edgeHold`, and `scanWait`. Mute is on that runner. `pressHold` expands and is not a step. |
| #47 | Profile load is `src/core/json.c` and `src/core/profile.c`. The IPC thread reads `--profile`. Playback adopts the image and picks the live config. `reload` is #70. |
| #48 | Process lifecycle is the Windows entry in `src/win/win_entry.c`. Single instance is `Local\MacroUI.Engine`. Exit releases keys, then unhooks, then closes the timer, the pipe, and the mutex. The shell adopts the native pid. |
| #49 | Fake-clock harness is `tests/runner_test.c`. Windows CI runs the AHK `--self-test`. The trace corpus is `bench/`. Diffs against AHK are empty. |
| #50 | Shell switch. `shell/EngineLink.cs` speaks JSON to AutoHotkey and frames to the native engine. One engine at a time. Two native start failures fall back to AutoHotkey. |
| #70 | Native applies `start`, `stop`, `reload`, `profile`, and `output`. `start` arms. Triggers start runners. State carries running rows, held names, and windows. `swapClicks` swaps software buttons. The once-lock pass list is consulted, and `pauseWatch` `"once"` does not fill it. |
| #54 | Native becomes the default, then AHK leaves. |

Block ideas stay out. `priority` and `variables` stay stored and ignored.
