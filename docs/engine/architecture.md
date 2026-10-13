# Native architecture

#41. The language is C17, from `docs/engine/decision-language.md`. That choice stays closed. MMCSS stays off. The measurement pin in that doc is not the playback priority.

Behaviour the port must match is in `docs/engine/plan.md` and `docs/engine/ahk-analysis.md`. This file is the threads, the ordering, the clock, and the tree. It does not port the runner. The byte layout of pipe messages is #42.

`engine-native/` from this issue prints its version and exits. It does not take the mutex, install a hook, or open the pipe.

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

The serial port stays in the shell today. Playback emits one report image per publish. #42 decides whether that image stays on the pipe or the engine opens the port. This file does not move the port.

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
6. The keyboard hook returns 1 when the published map says swallow. Every other path calls `CallNextHookEx`. The mouse hook never returns 1.

Side keys are resolved here. Generic shift, ctrl, alt, and win become the left or right virtual key from the scan code and the extended bit. Right shift is scan `0x36`. Numpad Enter is virtual key `0x0D` with the extended bit, and its key id is 256. Every other key id is the virtual key, 1 through 255.

There is no `RegisterHotKey`. Software swallow and board swallow are both the return from this callback. The pass count is a bit in the same map, published with it.

The hook thread is the thread that installed the hooks, and it is the thread that reinstalls them. Reinstall runs between messages, not inside the callback. Unhook first. A handle Windows has already removed still looks valid.

The watchdog rules in the analysis stay. Idle does not re-raise. The stamps live on the hook thread. The decision runs in step 4.

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

The wait is an unnamed `CreateWaitableTimerExW` with `CREATE_WAITABLE_TIMER_HIGH_RESOLUTION`, no completion routine. Playback waits on that handle, the hook wake, and the IPC wake. It does not run a message loop. A message loop on this thread would let a send re-enter the tick.

The hook thread is already pumping, so an overdue runner does not have to spin the message queue. It still must not busy-spin. After every tick:

| State | Wait | Input wakes playback |
|---|---|---|
| Disarmed, nothing running, nothing pending | 16 ms | No |
| Armed, nothing running, nothing pending | 8 ms | Yes |
| Otherwise | Until the next deadline, clamped to 1..8 ms | Yes |

A deadline that is already due still waits 1 ms. The longest slice is 8 ms.

A requested 1 ms wait landed near 1.52 ms in the language measurement. The clock will not schedule a 1 ms edge as 1 ms. The AHK 5 ms self-test band is an AHK check. It is not a promise that the native tick matches that float.

Deadlines are absolute QPC ticks. The core compares integers the caller passes in.

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

`Alias`, `NormKey`, and `SafeKey` run at load. The tick does not look up a name. More than one macro may share a key. `trigger_head[257]` is a 1-based index into a chain built at load, or 0. The chain is walked. That is the array from the language decision, plus the one id the virtual-key space cannot hold, plus a chain because `PollTriggers` walks every macro on that key.

`applied` stores key ids. The IPC thread turns them into the names on the state message. The playback thread does not format that text. The name table is filled once at startup.

The report image is fixed and little-endian. Its fields are #42. One publish produces one image. On the software path that image becomes one `SendInput` call.

## Process

Resident startup, when a later issue replaces the version printer:

1. `SetProcessDpiAwarenessContext` to Per-Monitor V2, before any dpi query.
2. `CreateMutexW` on `Local\MacroUI.Engine`, initial owner true. If the mutex already exists, exit. Do not kill the holder and do not release its keys.
3. Start the IPC thread, the hook thread, then the playback thread.
4. No console window.

`--self-test` returns before step 2. Both engines share the mutex name. Switching engines is: release keys, quit, wait until the mutex is free, then start the other.

The board scale uses the foreground dpi from the snapshot, and the system dpi when the window has none. Per-Monitor V2 can change that number relative to the unaware AHK process. If #53 shows a difference, record it there. The process stays Per-Monitor V2.

Exit releases keys, asks the hook thread to unhook, closes the timer, the pipe, and the mutex, in that order.

## Tree

```
engine-native/
  CMakeLists.txt
  src/main.c              version printer, no Win32
  src/core/version.h
  src/core/version.c      portable, Linux CI compiles this
  src/win/win_entry.c     Windows only; timer, hooks, SendInput, pipe, mutex later
```

`engine/` stays the AHK engine until #54.

`src/core` is the interpreter, the lookup, and the compiler once those exist. It includes no `windows.h`. `src/win` is the timer, the hooks, `SendInput`, the pipe, the mutex, and the foreground queries. `src/ipc` is the later home of the portable message image (#42). The pipe call itself stays in `src/win`. `tests/` and `bench/` arrive with the harness. The language benches stay in `tools/bench/lang/`.

## Build

C17, CMake, x64, one executable, `macroui-engine`. MSVC uses the static CRT. Windows CI is MSVC on `windows-latest`. Linux CI is the image C compiler and builds the core with `src/main.c` only. clang-cl is not required.

The program prints `macroui-engine 0` and exits 0. That string is the version until a later issue replaces this entry with the resident process.

## Later issues

| Issue | Owns |
|---|---|
| #42 | Frame bytes, hello handshake, hex vectors. No shell code. The serial-port question. |
| #43 onward | Compiler, runner, real clock, hooks, output. |
| #49 | Fake-clock harness, before the first block port. |
| #54 | Native becomes the default, then AHK leaves. |

Block ideas stay out. `priority` and `variables` stay stored and ignored.
