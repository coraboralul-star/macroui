# AHK engine analysis

Reading of `engine/MacroEngine.ahk` (4532 lines) and `engine/JSON.ahk`, for the native port (#38). Line numbers match this file. This is not a timing measurement. Sentences marked **Guess** are not measured.

`docs/engine/plan.md` wins where it already decided the port will differ. Those spots are called out. Nothing here picks C or C++, and nothing here describes an `engine-native/` tree.

The script is one AutoHotkey v2 thread. Hooks, the clock, playback, the pipe, and JSON parsing all share `Engine.Tick`.

## Already decided for the port

The script does these. The plan says the port does not copy them.

| Current script | Port |
|---|---|
| `#SingleInstance Force` kills the previous AHK process and `OnEngineExit` releases keys | Share `Local\MacroUI.Engine`. A second process exits if the mutex is held. It does not kill the process that holds it. |
| Profile path is `A_ScriptDir\..\profiles\default.json` | The shell passes `--profile` with the full path. Do not keep the relative default. |
| `WindowListJson` and `ActiveExe` run on the tick thread | They do not run on the playback thread. |
| No DPI awareness call | The native process is Per-Monitor V2. |
| `Round` after dividing by `speed` | Integer QPC ticks. The rounding rule is written in the architecture doc, or listed as an accepted parity diff. |

Switching engines, from the plan: release keys, quit, wait until the mutex is free, then start the other.

## Index

Every top-level function and class in `MacroEngine.ahk`. Methods of `Runner` and `Engine` are listed under the section that owns them.

| Name | Line | Section |
|---|---|---|
| `OnEngineExit` | 38 | Startup |
| `Field` | 60 | Values and names |
| `AsNum` | 66 | Values and names |
| `AsBool` | 79 | Values and names |
| `SafeKey` | 86 | Values and names |
| `SendToken` | 98 | Values and names |
| `SendPiece` | 104 | Values and names |
| `SendMouseButton` | 114 | Values and names |
| `SendName` | 140 | Values and names |
| `HookSpec` | 152 | Hooks |
| `TriggerHook` | 162 | Hooks |
| `InstallPhysical` | 196 | Hooks |
| `PhysKeyProc` | 208 | Hooks |
| `PhysMouseProc` | 219 | Hooks |
| `IsMouseButton` | 228 | Values and names |
| `Alias` | 232 | Values and names |
| `NormKey` | 260 | Values and names |
| `SideKey` | 268 | Values and names |
| `CompileBlocks` | 289 | Compiler |
| `NextSwapMs` | 356 | Compiler |
| `HasHoldLoop` | 367 | Compiler |
| `NormalizeIgnore` | 381 | Compiler |
| `NormalizePause` | 388 | Compiler |
| `BlockReleaseStop` | 397 | Compiler |
| `class Runner` | 404 | Runner |
| `class Engine` | 1229 | Split below |
| `Check` | 3227 | Self-test |
| `HasVal` | 3236 | Self-test |
| `SelfTest` | 3245 | Self-test |
| `J` | 3257 | Self-test |
| `RunChecks` | 3261 | Self-test |

`class JSON` is in `engine/JSON.ahk`, not in `MacroEngine.ahk`. The pipe and the profile loader both call it. See JSON.

### `Runner` methods

All in [Runner](#runner).

`__New` (405), `AskStop` (467), `NoteHoldRelease` (475), `StopMode` (491), `MarkDone` (505), `ArmMute` (525), `ReleaseFrameMute` (533), `ArmPause` (542), `ReleaseFramePause` (550), `Pause` (559), `Advance` (570), `Exec` (703), `TickOnce` (938), `PushHoldFrame` (1022), `ArmWait` (1040), `DropHold` (1045), `HoldShouldSwap` (1054), `HoldShouldStop` (1071), `TickEdge` (1090), `TickSpamEdge` (1136), `TickSpam` (1176), `AcceptWatch` (1192), `RepeatHold` (1208), `Clamp` (1220).

`HoldShouldSwap` is never called. The live swap check is inline in `Advance`.

### `Engine` methods

| Methods | Section |
|---|---|
| `Loop` (1291), `Tick` (1615), `Now` (1499), `Pump` (1517), `HrtWait` (1521), `Soonest` (1555), `NeedFineClock` (1573), `SyncPeriod` (1583), `Pace` (1594) | Clock and tick |
| `LoadDisk` (1715), `ApplyLiveConfig` (1681), `PickLive` (1655), `ReapOrphans` (2154) | Profile |
| `SetArmed` (1748), `FindMacro` (1762), `HasRunner` (1772), `PrepareBasic` (1782), `StartRunner` (1812), `StopRunner` (1834), `HasPending` (1845), `PauseRunner` (1852), `UnpauseRunner` (1861), `AskStop` (1870), `StopAll` (1876), `FlushPending` (2137), `RemoveDone` (2143) | Play modes |
| `MuteIds` (1882), `UnmuteIds` (1901), `HoldReplay` (1942), `IsMuted` (1963), `SameKey` (1970), `MacrosOnWatch` (1978), `MacroTrigger` (2006), `PassKey` (2015), `AddPass` (2038), `DropPass` (2045), `RebindKey` (2056), `EnsurePass` (2067), `StartOncePlay` (2077), `ArmOnceLock` (2085), `ReleaseOnceLock` (2109), `OnceOpen` (2125), `OnceDone` (2129), `SpendOnce` (2133) | Mute and once-lock |
| `PollTriggers` (2386), `TriggerAction` (2507), `HasTapHold` (2452), `ScanTapHold` (2459), `HoldsOnRelease` (2472), `ScanHoldsOnRelease` (2479), `SeedTriggers` (2357), `PollPanic` (2378) | Play modes |
| `NotePhysical` (1302), `BuildVkMap` (1312), `RaisePhysicalHook` (1333), `CheckPhysicalHook` (1347), `NeedsKeyRaise` (1385), `NeedsMouseRaise` (1395), `MouseHookNeeded` (1401), `RaiseMouseHook` (1410), `SyncMouseHook` (1422), `ReadKey` (1436), `Hooked` (1460), `ReadMouse` (1469), `SyncHooks` (2531), `UnhookKey` (2918), `ClearHookBindings` (2926) | Hooks |
| `FocusOk` (2167), `ExeStem` (2185), `FocusMatch` (2193), `ShellInFront` (2205), `ActiveExe` (2225), `ExeFromHwnd` (2242), `SwallowTriggers` (2271), `SyncSwallow` (2275), `WatchingHold` (2286) | Focus |
| `FingerDown` (2305), `SentDown` (2320), `SentSince` (2335), `KeyDown` (2342), `Sending` (2020), `HoldingOut` (2025), `Echoing` (2032) | Hooks |
| `KeyDelayMs` (2586), `KeyRepeatMs` (2599) | Runner (`edgeHold` repeat) |
| `Desired` (2614), `Publish` (2628), `Emit` (2705), `MoveAbs` (2746), `NudgeMouse` (2751), `HidWheel` (2759), `WheelSend` (2763), `HidJson` (2791), `HidScale` (2796), `HidKeys` (2812), `FlushApplied` (2823), `PassBoard` (2832), `ClicksSwapped` (2937), `OutName` (2945), `ReleaseAll` (2956), `QueueHidEcho` (2889), `MatchHidEcho` (2898) | Output |
| `WaitSpan` (2772), `RandomWaitMs` (2782) | Runner (`randomWait`) |
| `RememberMouse` (2836), `GotoWhere` (2842), `LocalToScreen` (2849), `MousePos` (2870), `CursorDelta` (2881), `WindowListJson` (3001) | Profile, windows, and goto |
| `StateSig` (2967), `StateJson` (2981), `Broadcast` (3052), `OpenPipe` (3064), `EnsurePipe` (3083), `DropClient` (3098), `PollPipe` (3108), `SendRaw` (3139), `Handle` (3165), `Log` (3222) | Pipe |

`WatchingHold` is never called.

## Startup

### What it does

Directives at the top of the script, then either the self-test or the resident process.

`#Requires AutoHotkey v2.0`. `#SingleInstance Force`. `SendMode("Input")`. `CoordMode` mouse and pixel are `Screen`. `SendLevel(1)`. `SetKeyDelay(-1, -1)`, `SetMouseDelay(-1)`, `SetDefaultMouseSpeed(0)`, `SetStoreCapsLockMode(false)`. `ListLines(false)`. `A_HotkeyInterval := 0` so a fast loop does not open the "too many hotkeys" dialog. `#Include JSON.ahk`.

If the first argument is `--self-test`, the script calls `SelfTest()` and `ExitApp` with that code. That path returns before the mutex, `OnExit`, the pipe, and `Loop`.

Otherwise it creates the mutex, hides the tray icon, clears key history, calls `InstallKeybdHook()` (AutoHotkey's own keyboard hook, separate from the low-level hook), calls `InstallPhysical()`, registers `OnEngineExit`, loads the profile, opens the pipe, and enters `Engine.Loop`. There is no window.

`OnEngineExit` closes the mutex handle, calls `ReleaseAll` (stop runners, then key-ups for whatever is still in `applied`), turns off every hotkey in `Engine.hooks` for both the down and up spec, unhooks the keyboard and mouse low-level hooks, calls `timeEndPeriod(1)` if the fine period is on, and closes the timer and pipe handles.

### State it owns

`Engine.instanceMutex`. The rest of the static fields on `Engine` are created as the class body runs, before `Loop`. `outputMode` starts as `"software"`. `armed` starts false. `echo` is `0x56454E44`.

### Win32

`CreateMutexW` with name `Local\MacroUI.Engine`, initial owner true. The script stores the handle and does not check whether the mutex already existed.

On exit: `CloseHandle` for the mutex, timer, and pipe; `UnhookWindowsHookEx` for each hook that is set; `timeEndPeriod(1)` when `periodOn` is set.

The shell (`EngineMutexHeld` in `shell/MainWindow.xaml.cs`) calls `Mutex.OpenExisting("Local\\MacroUI.Engine")` and will not start a second AutoHotkey while that name exists. The script's own backstop is still `#SingleInstance Force`.

### Quirks the port must keep

`#SingleInstance Force` kills the previous instance of this script. `OnEngineExit` then releases every key in `applied`. The comment at line 24 says a second process would hit that and drop held keys. The native engine must not copy it. Both engines share the mutex. If it is already held, the new process exits. It does not terminate the holder.

Exit still releases keys, unhooks, and ends the 1 ms timer period. A quit from the pipe sends the command ack and then `ExitApp`, which runs `OnEngineExit`.

`--self-test` must stay a process that does not take the resident mutex. `RunChecks` does install a real `F24` hotkey and does call `timeBeginPeriod` for the 5 ms clock check.

### Where time likely goes

Startup parses the profile on this thread before `Loop` (`LoadDisk`). **Guess:** that cost matters once per launch, not per tick. The mutex and hook install are one-shot.

## Values and names

### What it does

`Field` reads a `Map` key or returns the default (`""` if omitted).

`AsNum` returns a number unchanged, parses a non-empty string with `Number`, and returns the default for anything else. A failed parse does not throw.

`AsBool` is true for boolean true, numeric 1, or the strings `"true"` and `"1"` after trim and lower-case.

`SafeKey` keeps letters and digits, a fixed punctuation set, and rewrites `"|"` to `"\"`. Anything else becomes `""`.

`Alias` plus `NormKey` are the one name used everywhere. `"shift"`, `"ctrl"`, and `"alt"` become the left key. `"RShift"` stays right. `"ins"` / `"del"` become `Insert` / `Delete`. `F1` style names fold to `f1`. A single character is lower-cased. `"|"` becomes `"\"`.

`SideKey` turns a generic virtual key plus scan code and the extended bit into `LShift` / `RShift`, `LCtrl` / `RCtrl`, `LAlt` / `RAlt`, `LWin` / `RWin`. Right shift is scan `0x36`. Right ctrl and right alt are the extended generic VK.

`IsMouseButton` is `LButton`, `RButton`, `MButton`, `XButton1`, `XButton2`.

`SendName` rewrites `LCtrl` / `RCtrl` to `LControl` / `RControl` because that is what `Hotkey` and `Send` accept.

`SendToken` builds `{key down}` or `{key up}`. A backtick is doubled twice in the source string so `Send` does not treat it as an escape. `SendPiece` sends mouse buttons itself and returns `""` for them; other keys return a token for one combined `Send`.

### State it owns

None, except `SendMouseButton` writes `Engine.echo` into the `INPUT` extra-info field.

### Win32

`SendMouseButton` builds one mouse `INPUT` and calls `SendInput` with `dwExtraInfo = 0x56454E44`. Flags are the usual down/up pairs for left, right, middle, and X buttons. X button data is 1 or 2. The struct size is 40 bytes on 64-bit and 28 on 32-bit.

Keyboard tokens go through AutoHotkey `Send`, which itself calls `SendInput` under `SendMode("Input")`.

### Quirks the port must keep

A bad number must not escape `Advance`. The comment on `AsNum` says a throw there used to skip `Publish` and leave keys held.

`"|"` is the backslash key. Bare `"shift"` is `LShift`. Left and right modifiers stay distinct. `SideKey` is how the low-level hook tells them apart when Windows reports the generic VK.

A backtick key is escaped for `Send`. Own mouse buttons carry the echo marker so the physical hook can ignore them. The comment says a sent Mouse 4 must not look like the finger letting go, or a Z hold ends immediately.

`SendLevel(1)` is part of echo suppression. `TriggerHook` drops events that `Echoing` says are ours.

### Where time likely goes

These are string and map operations on the tick. **Guess:** they are noise next to `SendInput`, the window walk, and a profile parse.

## Clock and tick

### What it does

`Engine.Loop` is `while true`: set `busy`, `Tick` inside `try`, log the message on throw, clear `busy`, `Pace`. `Engine.busy` is this flag. It is not the macro `busy` field.

`Now` is milliseconds from the first `QueryPerformanceCounter`, origin subtracted, times 1000, divided by the frequency. The first call stores frequency and origin. The value is a float.

`Tick` order:

1. Clear the foreground cache (`frontKnown`, `focusCache`) and `sendNow`.
2. `Now`, then `PollPipe`, `CheckPhysicalHook`, `ApplyLiveConfig`, `PollPanic`, `SyncSwallow`.
3. `SyncHooks` if `hooksDirty`.
4. `PollTriggers` only if `armed`.
5. `RemoveDone`, `FlushPending`, `Now` again.
6. `Advance` each runner. A throw marks that runner done and the loop continues.
7. `FlushPending` again.
8. `Publish`, then `RemoveDone`, then `Broadcast(false)`.

`Soonest` looks at runners that are not done. A runner with no `waitUntil`, or a `waitUntil` already due, makes the result `now`. Otherwise the nearest `waitUntil` or pulse time, starting from `now + 15`. Any pending runner makes the result `now`.

`Pace` calls `SyncPeriod`, then waits:

- Not armed, no runners, nothing pending: `HrtWait(16)` and input does not cut the wait short.
- Armed, but nothing running or pending: `HrtWait(8, true)`.
- Otherwise wait until `Soonest`, clamped to 1..8 ms, with `wakeOnInput` true. A due or overdue wait still waits 1 ms. The comment says an overdue wait still has to pump hooks, or Windows queues them and they all fire at once.

`NeedFineClock` is true when `pending` is non-empty or any runner is not done. `SyncPeriod` calls `timeBeginPeriod(1)` only while that is true, and `timeEndPeriod(1)` when it becomes false.

`HrtWait` under 0.3 ms only calls `Pump` (`Sleep(0)`). Otherwise it creates a high-resolution waitable timer once (`CreateWaitableTimerExW`, flags `2`, access `0x1F0003`). If that fails, it `Sleep`s for at least 1 ms. The wait loop sets a relative due time (`SetWaitableTimer`, negative 100-nanosecond units) and `MsgWaitForMultipleObjects` on that handle. The wake mask is `0x04FD`. The comment calls that `QS_ALLINPUT` minus `QS_MOUSEMOVE`. Timeout is `Round(remain) + 2` ms, at least 1. After every return it `Pump`s. If the result is the input-queue wake and `wakeOnInput` is set, it returns. Any other result returns. Input with `wakeOnInput` false keeps looping until the deadline.

`Pump` is `Sleep(0)`. The comment says this drains queued input so low-level hooks cannot sit unprocessed and then fire in a burst, and that mouse-move must not keep the thread awake.

### State it owns

`qpcFreq`, `qpcOrigin`, `hTimer`, `periodOn`, `busy`. `Tick` also clears `sendNow`, `frontKnown`, and `focusCache` at the start of every pass.

### Win32

`QueryPerformanceFrequency`, `QueryPerformanceCounter`. `CreateWaitableTimerExW`, `SetWaitableTimer`, `MsgWaitForMultipleObjects`, `Sleep`, `Sleep(0)`. `timeBeginPeriod(1)` / `timeEndPeriod(1)` in `winmm`.

### Quirks the port must keep

One thread. A throw in `Tick` is logged and the loop continues. A throw in one `Advance` kills that runner only, then `Publish` still runs. The comment says the old behaviour skipped `Publish` and froze every other runner with its keys down.

`Publish` runs before the second `RemoveDone`. See Output.

The fine timer period is on only while work exists. Idle does not leave `timeBeginPeriod` held.

Never busy-spin. The shortest playing wait in `Pace` is 1 ms, and the longest slice is 8 ms, even if the next step is further out (`Soonest` may say 15; `Pace` caps at 8). Mouse-move is not a reason to wake.

`SetTimer` is not used. The comment at the top says it cannot keep a 5 ms wait at 5 ms. The self-test arms a 5 ms `wait` with `timeBeginPeriod(1)` and expects the runner to finish between 4.5 and 9 ms (`"5 ms wait landed"`).

`Now` is a float in milliseconds. The plan moves the hot path to integer QPC ticks and defers the rounding rule. Do not invent that rule in this doc.

### Where time likely goes

**Guess:** a playing tick is dominated by whatever `Tick` calls before `Advance` (pipe parse, hook check, foreground process query) plus `SendInput` or a hid write, not by `QueryPerformanceCounter` itself. `HrtWait` calls `Now` at least twice per slice. The 1 ms floor means a 5 ms wait is a handful of wakes, which matches the self-test window. **Guess:** `timeBeginPeriod(1)` is why that window is reachable; the `Sleep` fallback is not.

## Compiler

### What it does

`CompileBlocks` walks an array of block maps and pushes internal steps. A non-array returns `[]`. A non-map entry is skipped. An unknown `type` is skipped. The editor comment in `ui/src/profile.ts` says the engine skips kinds it cannot compile.

| Block `type` | Internal step |
|---|---|
| `whileHeld` | `holdLoop` with child `steps`, `mute`, `releaseStop`, and `swapAfterMs` |
| `ifShort` | `ifShort` with `underMs` (default 150) and `minCycles` (default 3) |
| `swapAfter` | `swapLoop` with `afterMs` (default 150), `mute`, `releaseStop`, `swapAfterMs` |
| `then` | `burst` with `forMs` (default 0) |
| `repeat` | `repeat` with `count` (default 1) and `releaseStop` |
| `wait` | `wait` |
| `steps` | `repeat` with `count` 1 and `releaseStop` |
| `onceHeld` | `onceHeld` with `gapMs` (default 70) |
| `tapHold` or `tapSpam` | `edgeHold`. `spam` is true only for `tapSpam`. Defaults: key `z`, `armMs` 5, `gapMs` 80, `holdMs` 18, `restMs` 18 |
| `pressHold` | See the legacy section |

`NextSwapMs` reads the next block. If it is `swapAfter`, it returns that block's `afterMs` (default 150). Otherwise 0. That value is stored on the current `holdLoop` or `swapLoop` as `swapAfterMs`.

`HasHoldLoop` is true if any top-level compiled step is `holdLoop`, `swapLoop`, `onceHeld`, or `edgeHold`. The comment says those loop themselves, so one pass of the parent lets After release run once spam stops.

`NormalizeIgnore` keeps `"macro"` and `"both"`. Anything else is `"off"`.

`NormalizePause` returns `"block"` only for the string `"block"`. Boolean true, numeric 1, `"once"`, and everything else become `"off"`. `RunChecks` locks this: `"once is not a lock"`, `"block stays a mute"`.

`BlockReleaseStop` keeps `"finish"` and `"nextUp"`. Anything else is `""`.

Child `steps` that are not an array become `[]`.

### State it owns

None. It returns new `Map`s. The runner stores those maps on its stack.

### Win32

None.

### Quirks the port must keep

`pressHold` is still compiled. See the legacy section. The editor rewrites it before save.

`pauseWatch: "once"` does not arm the once-lock. The once-lock helpers still exist and run only if a pause mode actually normalizes to `"once"`, which `NormalizePause` never produces. A raw file with `"once"` behaves as `"off"`. `"block"` mutes macros whose trigger is in the watch list.

`swapAfterMs` on a hold block is the next block's time, not its own.

Unknown block types disappear at compile time. Missing `steps` is an empty list (#35's engine side: the editor used to crash; the engine already treats a missing array as empty).

### Where time likely goes

Compile runs when a runner is constructed, not every tick. **Guess:** cheap next to playback. A profile reload that restarts many runners compiles each one on the tick thread.

## Runner

### What it does

`Runner.__New` copies the macro and builds the stack.

`speed` comes from the macro, default 1, and is raised to 0.05 if lower. See the speed section.

`releaseStop` is `"finish"` only when the macro says `"finish"`. Anything else, including a missing field, is `"nextUp"`.

`mode` is the override if one was passed, otherwise `playMode`, default `"once"`.

Top-frame `repeatsLeft`:

- `repeat`: `Max(1, Floor(repeatCount))`, default 1.
- `whileHeld` or `toggle`: 0, which `Advance` treats as forever.
- anything else, including `once` and `onRelease`: 1.

If `advanced` is true and `blocks` is a non-empty array, the stack gets `CompileBlocks` output. If `HasHoldLoop`, `repeatsLeft` on that top frame is 1 even for `whileHeld` / `toggle`. The comment says hold loops loop themselves, and play once must not restart the whole graph. Otherwise the top frame is the macro `steps` array. A missing or non-array `steps` becomes `[]`.

`Advance` clears this tick's move, wheel, and goto, drops pulses whose time has come, and then runs up to 64 steps.

A `waitUntil` in the future returns immediately unless `HoldShouldStop`. That stop marks the runner done, except an `edgeSpam` frame, which clears the wait and keeps going so the next block can run. The comment says spam yields on release so After release can run.

After the wait, `scanWait` is cleared. `stopping` with stop mode `nextUp` and `sawUp` already true marks done.

At the end of a frame's steps, `Advance` dispatches:

- `edgeHold` + spam: `TickSpamEdge`. True means stop this tick. False means the frame was popped; keep going.
- `edgeHold`: `TickEdge`, then return.
- `onceHeld`: `TickOnce`. Same true/false contract.
- `holdLoop`: count a cycle. If `stopping`, drop mute and pop. If the trigger is still down, pop when `swapAfterMs` has elapsed, otherwise restart at index 1. An empty step list waits 1 ms. If the trigger is up, drop mute and pop.
- `burstUntil` still in the future: restart the frame.
- empty steps: pop.
- `repeatsLeft` 0: if `stopping`, mark done; else restart. This is the infinite case.
- otherwise decrement `repeatsLeft`, restart if it is still above 0, else pop.

A frame with `drain` and `sawUp` skips the rest of its steps.

If 64 iterations produce no wait, `Advance` arms a 1 ms wait.

`MarkDone` sets `done`, clears pause, releases frame mutes and pause locks, and clears holds, pulses, echo downs, and the stack. It does not clear `moveX`, `moveY`, `wheel`, or `hasGoto`. `Publish` still reads those.

`AskStop` sets `stopping`. If the current stop mode is `nextUp` and `sawUp` is already set, it marks done immediately.

`StopMode` walks the stack from the top and returns the first frame `releaseStop` that is `"finish"` or `"nextUp"`. Otherwise it returns the macro's `releaseStop`.

`NoteHoldRelease`, called at the start of `Advance`, sets `drain` on `holdLoop` frames when the trigger is up and the stop mode is `nextUp`.

`Pause` sets `paused` and clears holds, pulses, echo, move, wheel, and goto. It does not mark done. `Desired` skips paused runners, so the next `Publish` releases their keys.

`ArmWait` sets `waitUntil` to `now + Max(1, Round(ms))` and returns `"wait"`. A computed wait of 0 still waits 1 ms.

### `Exec` step types

A non-map step returns `""`. If `tookShort` is set and this is the top frame, every kind except `burst` and `ifShort` is skipped. The comment says a short-tap branch is exclusive of later Run-once, hold, and wait blocks, and After release still runs.

| `type` | Behaviour |
|---|---|
| `randomWait` | `RandomWaitMs(minMs, maxMs) / speed`, then `ArmWait`. Clears `scanWait`. |
| `wait` | `ms / speed`, then `ArmWait`. Clears `scanWait`. |
| `scanWait` | If the trigger name is non-empty and `FingerDown` is false, `MarkDone` and return `"wait"`. Otherwise set `scanWait` and `ArmWait` for `ms / speed`. |
| `key` / `mouse` | Name from `key` or `button`, through `NormKey`. Empty name is a no-op. `down` sets `holds` and `pressedNow`, clears `sawUp`, records `Engine.lastSent`, drops a pulse. A `down` on a draining frame is ignored. `up` clears hold, pulse, and `pressedNow`, sets `sawUp` and `releasedNow`. If this tick also pressed that key, the up is stored as a pulse at `now + 1` and the runner waits 1 ms, so the down is published before the up. The comment says a down and an up in the same tick would never be sent. `stopping` plus `nextUp` then marks done. `tap` (any other action) sets a pulse at `now + Max(1, Round(holdMs / speed))` with `holdMs` default 1, and records `lastSent`. |
| `move` | Adds `Clamp(Round(x))` and `Clamp(Round(y))` to this tick's relative move. Clamp is -16000..16000. Not divided by speed. |
| `scroll` | Adds `Round(dy)` to this tick's wheel. Not divided by speed. Sign is positive for down. See Output. |
| `goto` | Target `x`, `y`, duration `ms` default 15, divided by speed. `where` via `GotoWhere`. The first goto of this runner clears `Engine.mouseAtOk`. Converts to screen, adds `CursorDelta` to the relative move, and sets `hasGoto` / `gotoX` / `gotoY` plus `RememberMouse`. `Publish` sends the absolute point and drops that relative sum. A failed `LocalToScreen` skips the step. A positive duration waits. |
| `repeat` | Pushes the child steps with `repeatsLeft` = `Floor(count)`, minimum 0. Count 0 is infinite. A missing `steps` array becomes `[]`. Copies `releaseStop` onto the frame (may be `""`). |
| `edgeHold` | Pushes a frame that loops itself. See below. Always waits 5 ms at the start. Holds `edgeKey`, and also the trigger if it is a different key. |
| `onceHeld` | Ignored if `stopping`. Pushes a `onceHeld` frame with `gapMs / speed` (negative becomes 0). |
| `holdLoop` | Ignored if `stopping`. Also ignored if `physDown` has the trigger and `KeyDown` says it is up (the finger already released before the loop started). Otherwise `PushHoldFrame`. |
| `swapLoop` | Ignored if `stopping` or the finger is already up the same way. If the trigger is not down, skip. If `heldMs` is still under `afterMs`, wait 1 ms and return `"again"` (the step index is rewound). Otherwise `PushHoldFrame`. |
| `ifShort` | If the trigger is still down and `heldMs` is under `underMs`, wait 1 ms and return `"again"`. If `heldMs` has reached `underMs`, skip the children. Otherwise set `tookShort` and push the children for `minCycles - cycles` repeats, or skip if that is under 1. |
| `burst` | Waits 1 ms and returns `"again"` while the trigger is down. The comment says After release runs only once the trigger is up, and a down with no matching up stays held until this block's up. `forMs` 0 or less pushes the children once. A positive `forMs / speed` sets `burstUntil`. |
| `run` | See the run section. |
| anything else | No-op. |

`TickOnce` phases are `run`, `decide`, and `gap`. The first pass of a held trigger waits out the gap before it will run the children again. A release during the gap finishes the gap, then pops. An empty child list waits 1 ms instead of spinning. If this tick already released a key (`releasedNow`), it waits 1 ms before the next down. The comment says an up in this same tick must be sent before the next down.

`TickEdge` marks done if `stopping` or the trigger is up. Phase `arm` drops the edge key and the trigger hold, then waits `edgeGap / speed`. Phase `gap` goes back to `down`, restores the hold, and resets the OS repeat timer. A rising edge on a watched key that `AcceptWatch` allows enters `arm` and waits `edgeArm / speed`. Otherwise `RepeatHold` and a 5 ms wait.

`TickSpam` toggles the edge key hold on `edgeHoldFor` / `edgeRestFor` boundaries. Those two were already divided by speed when the frame was built. `TickSpamEdge` on trigger-up drops holds, releases mute and pause, pops the frame, and returns false so `Advance` continues into the next block. A watched rising edge enters `quiet`, drops the key, and waits `edgeGap / speed`.

`AcceptWatch`: mode `"off"` always accepts. `"both"` rejects a repeat of that watch key inside `ignoreMs`. `"macro"` rejects inside the window only when `SentDown` or `SentSince` says the engine produced that key. A reject does not refresh the lock time. An accept stores `now`.

`RepeatHold` synthesizes extra downs for a keyboard key that is still in `holds`, using `KeyDelayMs` then `KeyRepeatMs`. Mouse buttons are not repeated. The extra down is `echoDown`, which `Publish` sends again even though the key is already applied. That is the Windows key-repeat behaviour the self-test calls `"tap hold repeats z like a windows hold"`.

`PushHoldFrame` copies mute ids except this runner's own id, pushes a `holdLoop` frame, and arms the mute.

`HoldShouldStop` is true for a `scanWait` when `stopping` or the trigger finger is up (`FingerDown`), and for an `edgeHold` frame when `stopping` or `KeyDown` says the trigger is up. Other frames do not stop early inside a wait.

### State a runner owns

`macro`, `id`, `name`, `speed`, `holds`, `pulses`, `echoDown`, `stack`, `waitUntil`, `scanWait`, `done`, `paused`, `moveX`, `moveY`, `wheel`, `hasGoto`, `gotoX`, `gotoY`, `didGoto`, `releaseStop`, `stopping`, `sawUp`, `releasedNow`, `mode`, `trigger`, `cycles`, `heldMs`, `tookShort`, `startedAt`, `pressedNow` (rebuilt each `Advance`).

Frame fields are plain objects. The ones the control flow reads: `steps`, `index`, `repeatsLeft`, `releaseStop`, `holdLoop`, `drain`, `mute`, `muteOn`, `pauseOnce`, `pauseOn`, `swapAfterMs`, `burstUntil`, `edgeHold`, `edgeSpam`, `edgeKey`, `edgePrev`, `edgePhase`, `edgeArm`, `edgeGap`, `edgeIgnore`, `edgeIgnoreMs`, `edgeLock`, `edgeRepeatAt`, `edgeHoldFor`, `edgeRestFor`, `edgeSpamOn`, `edgeSpamAt`, `onceHeld`, `oncePhase`, `onceFirst`, `onceGap`, `onceReady`.

`Engine` owns the runner list, `pending`, mute counts, and the once-lock maps. Those are under Play modes and Mute.

### Win32

None directly. `KeyDelayMs` / `KeyRepeatMs` call `SystemParametersInfoW`: `SPI_GETKEYBOARDDELAY` (`0x16`) and `SPI_GETKEYBOARDSPEED` (`0x0A`). Delay is clamped to 0..3 and stored as `250 * (delay + 1)` ms. Speed is clamped to 0..31 and stored as `Max(16, Round(1000 / (2.5 + speed * (27.5 / 31))))`. In `dry` mode (the self-test) the values are fixed at 400 and 33, and the real call is skipped. Both are cached on `Engine` for the process lifetime.

`goto` reaches Win32 through `LocalToScreen` and `MouseGetPos`. See Profile, windows, and goto.

### Quirks the port must keep

`repeatsLeft` 0 means forever. `Floor` of `repeatCount` below 1 becomes 1 only for play mode `repeat`. A `repeat` step may still push 0.

The 64-step cap yields 1 ms. Empty hold-loop bodies yield 1 ms. `ArmWait` is never under 1 ms after `Round`.

Same-tick down+up becomes a 1 ms pulse. Same-tick up before the next `onceHeld` down waits 1 ms.

`tookShort` suppresses later top-frame steps except `burst` and `ifShort`.

`holdLoop` / `swapLoop` / `onceHeld` / `edgeHold` are why a `whileHeld` macro with those blocks uses a top `repeatsLeft` of 1.

`ifShort` and `swapLoop` compare `heldMs` (`now - startedAt`) to their thresholds with no `/ speed`. Waits inside the children are scaled. **Guess:** the threshold is "how long the finger has been down", so it stays on the wall clock. The code fact is that those two compares are not scaled.

`edgeHold` arm and gap are divided by speed at the wait. Spam hold and rest are divided when the frame is built. OS key delay and repeat are not divided by speed.

`NormalizePause("once")` is `"off"`. Do not revive `"once"` as a lock.

`HoldShouldSwap` is dead. Do not wire it up unless the inline `Advance` check is the thing being ported. That check pops a `holdLoop` once `heldMs` reaches `swapAfterMs` while the trigger is down, after releasing the mute.

A missing `run` target is a no-op. There is no cycle guard. See the run section.

### Where time likely goes

**Guess:** `Advance` itself is map lookups and a short loop. The expensive work it causes is `Publish` (`SendInput` or a pipe write) and, for `goto`, a foreground window query. `AcceptWatch` and `Desired` walk maps every tick a runner is in those states. **Guess:** that stays cheap at the macro counts this editor saves. The 5 ms `edgeHold` poll is a deliberate wake, not a spin.

## Play modes

### What it does

`PollTriggers` walks the live macro list. Empty id is skipped. For each macro it stores the new `KeyDown` in `prevDown`.

Muted ids are skipped after that store, so the edge is not lost across the mute. The unmute comment says `Advance` would otherwise send one more tick of a plain while-held macro whose key came up during the mute. `UnmuteIds` marks that runner done when the macro is not `advanced`, the trigger is non-empty, and `KeyDown` is false.

`enabled` defaults to true when the field is missing. `enabled` false, or `FocusOk` false, calls `StopRunner` (immediate `MarkDone`, not `AskStop`).

If the once-lock state is `"done"` and a runner exists: a forced `whileHeld` (tap hold, below) that is no longer down gets `AskStop`. If no runner exists, the trigger key is passed through (`EnsurePass`) and the macro is not started.

`HasTapHold` forces the mode used for this decision to `whileHeld` when any block is `tapHold` or `tapSpam`. The cache is `macroTapHold`, filled by `SeedTriggers`. This is the trigger decision, not `Runner.mode`. The normal start path calls `StartRunner(macro)` with no override, so the runner keeps the saved `playMode` and the repeat count that goes with it. The edge frame itself ends when the trigger comes up (`TickEdge` / `TickSpamEdge`).

Advanced macros whose forced mode is not `onRelease` start on the down edge (`StartOncePlay` if a once-lock is open, otherwise `StartRunner`). On the up edge, if a runner exists and `HoldsOnRelease` is true, they `AskStop` instead of `StopRunner`.

`HoldsOnRelease` is false when any block is `whileHeld`, `swapAfter`, `onceHeld`, or `tapSpam`. The comment says those already exit on trigger-up, and stopping the whole runner would skip Run-once and After-release blocks after them. It is true when a `tapHold` is present without those, or when `playMode` is `whileHeld` or `toggle`.

Other macros use `TriggerAction`:

| Mode | Start | Stop |
|---|---|---|
| `whileHeld` | down edge | whenever the key is up, including a release that was missed while already running |
| `toggle` | down edge if not running | down edge if running. Release is ignored |
| `onRelease` | up edge | never from `TriggerAction` |
| `once`, `repeat`, and any other string | down edge | never from `TriggerAction` |

The comment on the advanced branch says playback `onRelease` means start when the key comes up. That is not the editor stop rule (`nextUp` / `finish`).

`StartRunner`:

- `PrepareBasic` first.
- Empty id returns.
- If a runner or pending runner with that id exists and `restart` is false, return.
- If one exists and the mode is `whileHeld` or `toggle`, return (no restart).
- If `busy` is true and one exists, return.
- If one exists, `StopRunner` (that id only).
- If `exclusive` is true, `StopAll`.
- Push `Runner(macro, mode)` onto `pending`.

`FlushPending` moves `pending` onto `runners`. `Tick` flushes before `Advance` and again after, so a `run` step can start a runner that `Advance`s later in the same tick only if it was flushed... it is flushed after the whole `Advance` loop, so a `run` started during `Advance` waits until the next tick to `Advance`. It is in `runners` before `Publish` only after the second flush, which is before `Publish`. The new runner has not `Advance`d yet, so it adds no keys on the tick it was created.

`StopRunner` marks matching runners done and drops that id from `pending`. `StopAll` marks every runner done and clears `pending`. `AskStop` only sets `stopping` on runners already in the list. It does not look at `pending`.

`SetArmed(false)` calls `StopAll`, rebinds hooks, seeds triggers, removes done runners, publishes, and broadcasts. `SetArmed(true)` does the same without `StopAll`.

`PrepareBasic` runs only when `basic` is true. `LButton` and `RButton` are left unchanged. The self-test says left click is not a macro trigger. Any other mouse button becomes down, wait 18, up, wait `gapMs` (default 18, negative clamped to 0). A key trigger (`kind` `"key"`) gets the same shape with `key` steps. Anything else is unchanged.

`SyncHooks` (when armed) hooks a trigger only if the macro is enabled and has steps or advanced blocks. It skips an empty button, `Pause`, `LButton`, and `RButton`.

### State it owns

`runners`, `pending`, `prevDown`, `macroTapHold`, `macroHoldRel`, `armed`. Mute and once-lock maps are in the next section. `PollPanic` uses `panicWas`.

### Win32

`PollPanic` calls `GetKeyState("Pause", "P")`. `KeyDown` / `FingerDown` may call `GetKeyState` too. See Hooks.

### Quirks the port must keep

Play modes are `once`, `repeat` (`repeatCount`), `whileHeld`, `toggle`, `onRelease`.

`busy` ignores a second press. It does not queue it.

`enabled` false stops the runner immediately.

Macro `releaseStop` is `nextUp` unless the field is `finish`. Block `releaseStop` can override per frame. `nextUp` stops at the next key-up (or drains a hold loop). `finish` plays through.

A `tapHold` or `tapSpam` block forces the trigger decision to `whileHeld`. `tapSpam` does not `AskStop` the whole runner on release (`HoldsOnRelease` is false), so later blocks still run. `tapHold` does `AskStop`, and `TickEdge` also marks done when the trigger is up.

`whileHeld` and `toggle` do not restart if they are already running. `once` and `repeat` do, unless `busy` or `restart` is false.

`exclusive` calls `StopAll` before the new runner is pushed, so the new one survives.

`onRelease` on an advanced macro uses `TriggerAction`, not the advanced down-edge path, because the `whileHeld` force happens before that branch and `onRelease` is excluded only when the mode is still `onRelease`. A `tapHold` block rewrites the mode to `whileHeld` first, so an advanced macro that is both `onRelease` and `tapHold` takes the advanced down-edge path. **Guess:** that combination is unused. The order in `PollTriggers` is the fact.

Pause key: edge into down calls `StopAll`. It is not a macro trigger (`SyncHooks` skips it). Key-repeat of Pause does not call `StopAll` again until it comes up and goes down.

### Where time likely goes

`PollTriggers` walks every live macro every armed tick and calls `KeyDown`. **Guess:** with the hook up this is a couple of map lookups per macro, not a Win32 call. `SeedTriggers` and `SyncHooks` run when the live config or swallow state changes, not every tick.

## speed, scanWait, and run

### speed

Floor 0.05 in `Runner.__New`. Waits divide by `speed` before `ArmWait` / `Round`: `wait`, `scanWait`, `randomWait`, `goto` duration, `burst` `forMs`, `onceHeld` `gapMs`, `edgeHold` arm and gap, spam hold and rest, and `tap` `holdMs`.

Not scaled: `ifShort` `underMs`, `swapLoop` `afterMs`, `move`, `scroll`, OS key delay and repeat, the 5 ms edge poll, the 1 ms yield.

`randomWait` rolls an integer in the inclusive range after `WaitSpan` swaps reversed bounds and clamps each end at 0. Equal bounds return that number. The plan says the port compares this as "inside min/max", or the test injects the roll. It is not an exact-interval benchmark.

### scanWait

At the start of the step, a non-empty trigger that is already up (`FingerDown`) ends the runner. Otherwise the step waits `ms / speed` with `scanWait` set. During that wait, `HoldShouldStop` ends the runner when the finger is up or `stopping` is set. `FingerDown`, not `KeyDown`. The self-test covers a scan that runs the keys first, exits if the trigger lets go, continues if it stays down, and can run with no keys around it.

### run

`Exec` for `run` calls `FindMacro` on `macroId`. A missing target returns `""` and does nothing (#34's engine side: the editor save checks are separate; the engine no-ops).

A found target is `StartRunner(target, "once", false)`. The override is `"once"`, so the target's own `playMode` is not used. `restart` false means: if that id is already in `runners` or `pending` and not done, return. There is no set of ids on the stack and no depth limit. A -> B -> A does not start a second A while A is still the running id. It also does not report a cycle. Do not add a cycle guard during the port.

`FindMacro` scans the live `profile["macros"]` list only. After `ApplyLiveConfig`, that list is the picked config's macros, not every config.

### State, Win32, time

No extra state or Win32. **Guess:** `run` and `scanWait` are not where the tick spends time.

## exclusive, priority, variables, pressHold

### exclusive

`StartRunner` calls `StopAll` when `exclusive` is true, after the same-id restart check and before `pending.Push`. Other runners are marked done. Their keys drop on the following `Publish` because `MarkDone` cleared their holds and they are removed before `Publish` only if `RemoveDone` already ran. On the trigger path, `RemoveDone` runs after `PollTriggers` and before `Advance`, so an exclusive start drops the others before `Publish` of this tick. The new runner is still pending until `FlushPending`, then `Advance`s, then `Publish` sees it.

### priority

Macro field `priority` is in the editor profile (`ui/src/profile.ts`). `MacroEngine.ahk` never reads it. The whole macro map sits on the runner and inside `profile`, so the field is stored and ignored. Leave it ignored.

### variables

Profile field `variables`. The only mention in the engine is the empty object written when `LoadDisk` creates a missing default file. Nothing reads it. Leave it ignored.

### pressHold

Legacy block. `ui/src/profile.ts` rewrites a saved `pressHold` into a `steps` block: optional key down, wait 18, key up, then a `scanWait` of `holdMs` (default 80 in the editor). The engine still compiles a raw file that contains `pressHold`:

- Child steps, if they are an array, are pushed as raw step maps. They are not compiled again.
- If `key` is non-empty and there are no children, it pushes key down, wait 18, key up.
- It always pushes `scanWait` with `holdMs`, default 200. That default is not the editor's 80.

So a raw `pressHold` and an editor-rewritten `pressHold` do not match unless the file was saved by this editor. The port keeps the engine compile, because a raw file can still contain the old type.

### Win32 and time

None of these add Win32 calls. **Guess:** no measurable time.

## Mute and once-lock

### What it does

`MuteIds` reference-counts ids. The first count pauses that runner and remembers whether it was running (`resume`). `UnmuteIds` unpauses at count 0. If it is still running, a plain (not advanced) `whileHeld` whose key is up is marked done. If it is not running, it restarts when `resume` was set or `HoldReplay` says the key is still down and the macro is `whileHeld` or has a `whileHeld` / `swapAfter` / `onceHeld` block. The comment says a hold whose key is still down should start when the mute lifts, even if it was not running when the mute began.

Mute ids come from `holdLoop` / `swapLoop` frames and from `edgeHold` when `pauseWatch` normalizes to `"block"`. `MacrosOnWatch` collects other macros whose trigger button matches the watch list.

`pauseWatch` `"once"` does not take this path. `ArmOnceLock`, `OnceOpen`, `OnceDone`, `SpendOnce`, and `EnsurePass` are the once-lock. `Exec` only fills `pauseOnce` when the normalized mode is `"once"` or `"block"`, and `"once"` never comes out of `NormalizePause`. The self-test starts a `tapHold` with `pauseWatch: "once"` and expects the watched macro to keep running with no lock (`"once leaves tracked macros running with no lock"`). `"block"` mutes and keeps swallowing the watched key (`"block keeps swallowing the tracked key"`).

`PassKey` / `AddPass` / `DropPass` change the hotkey prefix for a key so it is not swallowed (`HookSpec` uses `~$*` when the pass count is above 0). That rebinds immediately.

### State it owns

`muted` (id -> count and resume), `onceLock`, `oncePass`, `passKeys`.

### Win32

Rebind uses AutoHotkey `Hotkey`, which registers the OS hotkey. No direct `DllCall` in these methods.

### Quirks the port must keep

Mute is a count, not a bool. Nested mutes must not unpause early.

A while-held macro released during the mute must not play one more tick after unmute.

A held key that never started, because it was muted, starts when the mute lifts if `HoldReplay` matches.

`"once"` is not a lock. `"block"` is a mute.

### Where time likely goes

**Guess:** only when a hold frame starts or ends, not every tick. `IsMuted` is a map lookup per macro inside `PollTriggers`.

## Hooks

### What it does

Two layers.

AutoHotkey `Hotkey` via `SyncHooks` / `HookSpec` / `TriggerHook`. Software with swallow uses `$*`, which eats the key. Board mode (`outputMode` not `"software"`), a focus mismatch, or a positive pass count uses `~$*`, which lets the key through. Backtick is escaped. Down and up are both bound. `TriggerHook` is the callback.

The low-level hooks are `WH_KEYBOARD_LL` (13) and `WH_MOUSE_LL` (14), installed with `SetWindowsHookExW` on this module, thread id 0.

`InstallPhysical` builds the VK map, creates both callbacks, and installs the keyboard hook only. `rawOk` is true when that handle is non-zero. The mouse hook is installed later by `SyncMouseHook` when some hooked trigger is a mouse button.

`PhysKeyProc` updates `hookTick` and calls `ReadKey`. Returning 1 swallows the key. That happens only when `PassBoard` and `Hooked` and `SwallowTriggers` all say so: board output, this key is a trigger, and focus says swallow. Software swallow is the `$*` hotkey, not this return value.

`PhysMouseProc` ignores `WM_MOUSEMOVE` (`0x200`), `WM_MOUSEWHEEL` (`0x20A`), and `WM_MOUSEHWHEEL` (`0x20E`). The comment says handling them cuts the mouse rate. Other messages update `mouseHookTick` and call `ReadMouse`. The mouse hook never swallows.

`ReadKey` ignores injected input (`flags & 0x12`, which is the injected and lower-IL injected bits) and any event whose `dwExtraInfo` is `echo`. Side keys use `SideKey`. Numpad Enter is VK `0x0D` with the extended bit. Otherwise the VK map from `GetKeyName`. `MatchHidEcho` consumes a board echo. `NotePhysical` stores the finger. `NotePhysical` also stores the lower-cased name when it differs.

`ReadMouse` ignores injected flags `0x03` and the echo extra-info. It maps left, right, middle, and X buttons into `physDown`.

`TriggerHook` returns immediately in board mode so a Pico tap is not a second finger-down. It strips `$*~` and an `up` suffix. Mouse buttons are ignored while the mouse hook handle is set, because the hotkey also fires for buttons the engine sends. `Echoing` drops the event. If `rawOk` and the key is not a mouse button, it returns without writing `physDown`. The comment says the low-level hook already recorded the real finger, and a late hotkey would overwrite a real release.

`Echoing` is true when `sendNow` contains the key (down or up), or when `applied` contains it and this event is not an up. The comment says downs being output are echo, and ups still count unless this `Send` is in flight, so a real release can stop a hold. `Tick` clears `sendNow` at the start of the next tick. The comment on `Emit` says `Send` puts AutoHotkey's hook in front of ours, so `$*` can swallow the trigger key-up. `Emit` calls `RaisePhysicalHook` after a software send that included keys.

`RaisePhysicalHook` unhooks, then hooks again, so this hook is last installed and sees the event first. The comment says otherwise the macro hook swallows the real key-up and the tap stays stuck. It runs after new hotkeys are added, and after every software key `Send`.

`BuildVkMap` calls `GetKeyName` for VK 1..255, then forces the modifier, space, and enter entries.

### Watchdog (#14, current behaviour)

`CheckPhysicalHook` runs every tick.

Windows can remove a low-level hook when this thread stalls and leave the handle looking valid. `rawOk` would stay true while keys stop arriving. The mouse hook dies the same way. The fix re-raises both.

If the keyboard hook handle is 0, it logs and reinstalls keyboard and mouse. If `hookSuspect` is set (a profile parse just ran), it clears the flag and reinstalls both now, instead of waiting out the silence. If nothing is armed and nothing is running or pending, it returns. Otherwise, after 8000 ms of keyboard silence it reinstalls the keyboard hook, unless a mouse hook is required and the mouse hook has seen input in those 8000 ms. The comment says mouse clicks keep this thread alive, so an idle keyboard hook can still be installed. The mouse hook is reinstalled when it is required and has been silent for 8000 ms. `RaiseMouseHook` unhooks first. The comment says a handle Windows already removed still looks valid, and installing a second time would leave the dead one in place. If no mouse trigger remains, `SyncMouseHook` unhooks the mouse hook.

`NeedsKeyRaise` / `NeedsMouseRaise` are pure. The self-test locks: an idle engine does not re-raise, a quiet armed keyboard hook does, mouse activity keeps the keyboard hook, a quiet mouse hook is put back, a live mouse hook stays, and no mouse trigger means no mouse re-raise.

### Echo marker

`Engine.echo` is `0x56454E44` (the bytes spell `VEND`). `SendMouseButton` puts it on mouse input. Keyboard `Send` does not go through that struct; keyboard echo is `sendNow` / `applied` / `Echoing`, plus the low-level injected-flag check.

Board output is a real HID device, so those reports are not marked injected. `QueueHidEcho` records each hid down and up for 150 ms. `MatchHidEcho` drops the first matching name and direction. The self-test says the echo matches once and is then consumed. A second physical tap of the same key inside the window is a real finger.

### Focus swallow

`SwallowTriggers` is `FocusOk` of an empty map, so it uses the profile `focusExe`.

`FocusOk`: a macro `focusExe`, otherwise the profile `focusExe`. Empty means "not the editor": `ShellInFront` is false. The comment says an empty focus matches every window, including this editor, so typing a macro name would start the macro and swallow the key. `ShellInFront` is true for `MacroShell.exe`, and for `msedgewebview2.exe` when `GetAncestor` with flag 3 (`GA_ROOTOWNER`) is `MacroShell.exe`.

A non-empty exe uses `FocusMatch`. Stems drop a trailing `.exe`. Equal stems match. `want-` as a prefix of `have`, or the reverse, matches, so `Dawnwalker.exe` matches `Dawnwalker-Win64-Shipping.exe`. `notepad.exe` does not match `notepad++.exe`. The result is cached for the rest of the tick.

`SyncSwallow` clears every hotkey when swallow flips, then sets `hooksDirty`. `HookSpec` uses `~$*` when not swallowing, so typing works. Macros stay off because `PollTriggers` requires `FocusOk`.

`PickLive` is the config side of the same match. First `focusExe` that matches the foreground wins. Empty `focusExe` is the fallback. If several are empty, the one whose id equals `activeId` replaces an earlier fallback. No empty fallback returns `""`, and `ApplyLiveConfig` then sets macros to `[]`. No `configs` array keeps the flat macro list and sets `liveId` to `"-"`.

### Panic

`PollPanic` on the Pause key, edge to down, `StopAll`. `GetKeyState("Pause", "P")`.

### State it owns

`hooks`, `hookFold`, `physDown`, `rawOk`, `keyProc`, `mouseProc`, `hKeyHook`, `hMouseHook`, `hookTick`, `mouseHookTick`, `hookSuspect`, `vkName`, `swallowKnown`, `swallowOn`, `sendNow`, `applied` (also output), `hidEcho`, `passKeys`, `frontExe`, `frontKnown`, `focusCache`.

### Win32

`GetModuleHandle`, `SetWindowsHookExW`, `UnhookWindowsHookEx`, `CallNextHookEx`. `GetKeyName` and `Hotkey` and `GetKeyState` are AutoHotkey wrappers. `GetForegroundWindow`, `GetAncestor`, `GetWindowThreadProcessId`, `OpenProcess` (`PROCESS_QUERY_LIMITED_INFORMATION`, `0x1000`), `QueryFullProcessImageNameW`, `CloseHandle`. `WinGetProcessName` / `WinGetTitle` sit on the same kind of query. **Guess:** those AutoHotkey commands are the usual window and process calls, not a second mechanism.

Keyboard hook swallow returns 1 from the low-level proc only on the board path. Software swallow is the hotkey.

### Quirks the port must keep

Own output is tagged `dwExtraInfo = 0x56454E44` on mouse `SendInput`. Injected keyboard and mouse input is ignored. Board echoes are matched for 150 ms and consumed once.

The keyboard hook is re-raised after hotkeys are installed and after every software key send, and it is unhooked before it is installed again. The mouse hook is part of the watchdog (#14). Do not go back to re-raising only the keyboard hook.

Movement and the wheel are not triggers.

`LButton`, `RButton`, and `Pause` are not trigger hooks. Left click is not turned into a basic macro.

The low-level hook owns the finger while it is up. A late `$*` hotkey must not overwrite `physDown`. `sendNow` stays set until the next tick.

Empty focus does not swallow while the shell or its WebView2 host is in front. A set focus swallows only while it matches. Board mode passes keys through the hotkey (`~$*`) and the low-level hook eats the real trigger when focus matches, so a Pico key is not swallowed by the hotkey and is not counted as the finger.

Pause is panic. It calls `StopAll`.

`WatchingHold` is dead code. Do not call it from the tick.

### Where time likely goes

**Guess:** the hook procs are the hot path for input. They do a few integer reads and map writes. `MatchHidEcho` walks an array. **Guess:** that list stays short because entries expire after 150 ms. The stall that matters is the tick not pumping, which is why the watchdog exists. `RaisePhysicalHook` unhook/rehook on every software key emit is **Guess** a real cost on the software path, and it is there so the key-up is not lost. `GetKeyName` for all 255 VKs runs once at install.

## Output

### What it does

`Desired` is the union of `holds`, plus pulses whose release time is still in the future, for runners that are not done and not paused.

`Publish` sums `moveX`, `moveY`, and `wheel` from every runner that is not paused, including done runners. A `hasGoto` sets the absolute point (the last such runner wins) and clears the flag. Relative move is then cleared on the runner. Relative totals clamp to -16000..16000. If any runner had a goto, the summed relative dx and dy are not sent. The absolute point replaces them, including other runners' relative moves from that same tick.

Downs are keys in `desired` that are not in `applied`, plus `echoDown` repeats that are still desired and are not mouse buttons. Ups are keys in `applied` that are no longer desired. `applied` and `lastSent` update before the send.

`dry` pushes a record onto `sent` and does not call `Emit`. The self-test uses this.

Order against runners: `Tick` calls `Publish` and only then `RemoveDone`. The comment says a scroll or move on the last step is a one-shot, and removing the runner first would throw it away. `MarkDone` clears holds but not the move or the wheel, so the last one-shot is still in the sum, and the keys show up as ups because they are no longer desired.

Software `Emit` (`outputMode` `"software"`):

- Builds one `{Blind}...` string of key tokens and `WheelSend`, calls `Send`, leaves `sendNow` set, and `RaisePhysicalHook`.
- `WheelSend`: positive `dy` is `{WheelDown n}`, negative is `{WheelUp n}`, zero is `""`.
- Relative mouse is `MouseMove` relative, speed 0, then `NudgeMouse` if a remembered point exists.
- An absolute goto on software sends the keys and the wheel with no relative move, then `MoveAbs` (`MouseMove` absolute) and `RememberMouse`.

`OutName` applies `swapClicks` for software only: `LButton` and `RButton` swap. Middle is unchanged. `ClicksSwapped` accepts 1, true, `"true"`, and `"1"`.

Board `Emit` (`rp2040` or `rp2350`, anything else was forced back to `"software"` in `Handle`):

- Does nothing when there is no down, up, relative move, wheel, or absolute flag.
- `HidWheel` is `-Round(dy)`. The comment says step `dy` is positive for down and the board wheel is the HID sign, positive for up. The self-test locks both directions. This is the engine. #27 (scroll flipping at 0 notches) was an editor fix; the engine uses the `dy` it is given.
- Relative move is `HidScale`d: foreground `GetDpiForWindow`, or `A_ScreenDPI`, and if that is not 96 the deltas are multiplied by `dpi/96` and rounded. Absolute goto is not scaled here.
- `HidJson` is one object. Button names stay logical. The comment says the shell applies `swapClicks` when it maps HID bits. The self-test says hid JSON keeps `LButton` even when swap is on.
- Each down and up is queued in `hidEcho`.

`FlushApplied` emits ups for everything in `applied` and clears it. `Handle` does this when the output mode changes, before the new mode is stored. `ReleaseAll` stops runners, removes done ones, and emits the same ups. `OnEngineExit` calls `ReleaseAll`.

The shell, not the engine, writes serial lines. `WriteHid` in `shell/MainWindow.xaml.cs`:

- A report that includes an up waits up to 1500 ms for the board lock.
- A report with no up waits 40 ms.
- If that wait loses and the report has keys or a wheel, the JSON is queued, capped at 16, oldest dropped. The comment says a dropped up leaves the key held, so ups wait, and a down that loses the race is queued and sent ahead of the next report.
- `WriteHidLocked` writes `K {usage} 1` for downs, then `M {bits} {dx} {dy} {wheel}` when the mouse changed, then `K {usage} 0` for ups, then a wheel or button `M` line if those still need sending. Absolute goto is `SteerMouse`: up to 64 relative `M` steps, each axis clamped to -127..127, after a Per-Monitor DPI conversion in the shell.

#17, current firmware behaviour: `K` calls `send_keyboard_now`. On failure it replies `hid-busy` and leaves `kb_dirty` set. `send_reports` retries while the keyboard endpoint is ready. The shell's `BoardLine` reads the reply and ignores `hid-busy` / timeout. The retry that keeps a key-up is the firmware dirty flag, not a second write from the shell. Mouse reports that fail put the 8-bit slice back onto the accumulators. Keep this. Do not reopen the issue.

### State it owns

`applied`, `sendNow`, `lastSent`, `hidEcho`, `outputMode`, `mouseAtX`, `mouseAtY`, `mouseAtOk`, `sent` (dry), `delayMs`, `repeatMs`.

### Win32

Software: `SendInput` via `Send` and `SendMouseButton`, `MouseMove`. Board path from the engine is only the pipe write plus `GetDpiForWindow`. The serial port is the shell. `GetDpiForWindow` on a process that never set DPI awareness is the script's current call. The port is Per-Monitor V2, so do not copy "DPI awareness is not set" as the native process mode. Do keep the hid scale rule's result, or list a parity diff if Per-Monitor V2 changes the numbers. **Guess:** the scale exists so a relative move matches the foreground window's DPI when AutoHotkey itself is at 96.

### Quirks the port must keep

Publish before dropping finished runners.

A down may queue on the board lock. An up waits (#13).

`hid-busy` retries a lost key-up in the firmware (#17).

Echo marker `0x56454E44` on engine-sent mouse input.

`swapClicks` swaps the software send names and does not swap the hid JSON names.

Wheel sign flips only on the way to the board.

`sendNow` survives until the next tick, and the physical keyboard hook is re-raised after a software key send.

### Where time likely goes

**Guess:** software `Send` plus `RaisePhysicalHook` is the main playback cost on the Default path. **Guess:** `HidJson` string building is small. The board lock wait is in the shell, on the pipe reader, and a 1500 ms up-wait is the stall #13 moved off the UI thread. **Guess:** `GetDpiForWindow` every relative board move is a small call. It is not cached.

## Pipe

### What it does

Pipe name `\\.\pipe\MacroUI`. One instance. Message mode. 1 MB out buffer, 1 MB in buffer, 50 ms default timeout. `OpenPipe` uses `CreateNamedPipeW` with open mode `0x3` (`PIPE_ACCESS_DUPLEX`) and pipe mode `0x7` (`PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_NOWAIT`). Connect is polled. A waiting client, or error 535 (`ERROR_PIPE_CONNECTED`), marks the pipe up. No overlapped flag is set.

`PollPipe` calls `EnsurePipe`. The first successful `ConnectNamedPipe`, or error 535 (`ERROR_PIPE_CONNECTED`), sends `hello` and a forced state broadcast. Each tick reads at most 16 messages. `PeekNamedPipe` then `ReadFile` into a 1 MB buffer, NUL-terminated, decoded as UTF-8, passed to `Handle`. Peek failure or a read error other than 232 (`ERROR_NO_DATA`) drops the client (`DisconnectNamedPipe`, and the next tick connects again).

`SendRaw` appends a newline if the JSON has none, writes UTF-8 without the terminator, and retries. State messages get one try. Anything else gets four. Error 232 retries. Any other error drops the client. The comment says message mode takes the whole write or fails, state is sent again next tick, and an error or ack is not, so those get the extra tries.

`Handle` parses JSON. Bad JSON acks `ok: false` and `error: "bad json"`. A non-map is ignored. Unknown `type` is ignored with no ack.

`Broadcast` sends state when forced, or when `StateSig` changed and the pipe is up. The signature is armed, foreground exe, running ids, applied keys, and `liveId`. The window list is not part of the signature.

`StateJson` running entries are `{id, name}` for runners that are not done. `held` is the `applied` key names. `activeId` is the profile `activeId` when `liveId` is `"-"`, otherwise `liveId`. `front` is `ActiveExe`. `windows` is `WindowListJson`.

`Log` appends a timestamped line to `engine\engine.log`. Failures in the log write are swallowed.

### State it owns

`hPipe`, `pipeOn`, `pipeRetryAt` (declared, never read), `lastState`, `lastSig`, `readBuf`, `profile` (replaced by a profile message).

### Win32

`CreateNamedPipeW`, `ConnectNamedPipe`, `DisconnectNamedPipe`, `PeekNamedPipe`, `ReadFile`, `WriteFile`.

### Quirks the port must keep

One client. Message mode. 1 MB read buffer.

`activeId` is already on the state snapshot. Command acks do not echo an id. The command ack is `{"v":1,"type":"ack","for":"command","ok":true}` for `start`, `stop`, `reload`, and `quit`. An unknown action in a command message also falls through to that ack. `quit` sends the ack and then `ExitApp`. The ack line after the `if` does not run if `ExitApp` ends the thread, which is what AutoHotkey does.

Profile ack is `for: "profile"` and does not echo an id. It is sent even when the `profile` field was not a map (nothing is applied in that case).

`output` does not ack. An unknown type does not ack.

`hello` is `{"v":1,"type":"hello","armed":true|false}` and is followed by a forced state.

A corrupt profile on disk does not throw out of startup. `LoadDisk` keeps the last good profile (or an empty map) and sends `{"v":1,"type":"error","where":"engine","code":"profile-read","detail":"the saved profile could not be read"}`. The comment says a throw here used to kill the process, outside `Loop`'s try, and the shell kept relaunching it.

### Where time likely goes

**Guess:** `JSON.Parse` of a profile, or of a large pipe message, on this thread is a stall. That stall is why `hookSuspect` reinstalls both hooks after a profile load. The shell comment says it stopped piping the whole profile for that reason and now sends `reload`, which still parses the file on this thread inside `LoadDisk`. State and hid JSON are built with string concatenation. **Guess:** state is small; the window list inside it is the large one, and it is only rebuilt when a state send happens to call `StateJson`. `Log` hits the disk only on errors.

## Profile, windows, and goto

### What it does

`LoadDisk` sets `profilePath` to `A_ScriptDir\..\profiles\default.json`. A missing file creates the `profiles` directory and writes `{"version":1,"name":"Default","variables":{},"macros":[]}`. A parse failure keeps the previous map. It then sets `liveId` to a character that will not match, sets `hookSuspect`, and `ApplyLiveConfig`. The plan says the new process takes `--profile` and does not keep this relative path.

`ApplyLiveConfig` every tick. No `configs` array: `liveId` becomes `"-"`, triggers are re-seeded, orphans are reaped, hooks are marked dirty, once. With configs, `ActiveExe` plus `PickLive`. A changed id copies that config's macros and `focusExe` onto the profile and sets `profile["activeId"]` to the config id. A pick that is not a map clears macros. `ReapOrphans` marks runners done whose ids are not in the new macro list.

`WindowListJson` is called from `StateJson`, which runs on the tick when a state message is built. Cache key is `winJson` / `winAt`. TTL is 30 seconds while any runner is not done, otherwise 5 seconds. If a runner is active and `winJson` is already non-empty, it returns the cache even past the TTL. The walk uses `WinGetList`, `WinGetProcessName`, and `WinGetTitle`, one object per distinct exe. The foreground exe is appended if it was not in the list. `Broadcast` skips `StateJson` when the signature is unchanged, so the TTL is only checked on a send. Idle, with a stable signature, the list is not refreshed.

`ActiveExe` runs from `ApplyLiveConfig` every tick, and again from `StateSig` / `StateJson` / `FocusOk`. `Tick` clears `frontKnown` first, so the Win32 query happens once per tick and then the cache is used. Empty `WinGetProcessName` falls through to `ExeFromHwnd`.

`GotoWhere` keeps `"window"` and `"client"`. Anything else, including `""`, is `"screen"`.

`LocalToScreen` for `"screen"` returns the coordinates unchanged. `"client"` uses `WinGetClientPos` of `"A"`. `"window"` uses `WinGetPos` of `"A"`. The screen point is origin plus x, y. A throw returns false and the goto step does nothing. There is no DPI conversion in this function.

`MousePos` uses the remembered point only when the output is a board and `mouseAtOk` is set. Software always uses `MouseGetPos`. The first goto of a runner clears `mouseAtOk`, so the first board delta is from `MouseGetPos`. Later gotos in that runner use the remembered point on a board. `CursorDelta` is target minus that position. `Publish` does not send the delta when any runner set `hasGoto` this tick. It sends the absolute point instead.

DPI awareness is not set. No `SetProcessDpiAwareness` and no manifest call in this script. `HidScale` and `A_ScreenDPI` are the only DPI reads. The port is Per-Monitor V2 anyway.

### State it owns

`profile`, `profilePath`, `liveId`, `winJson`, `winAt`, `frontExe`, `frontKnown`, `focusCache`, `mouseAtX`, `mouseAtY`, `mouseAtOk`.

### Win32

`GetForegroundWindow`. `GetWindowThreadProcessId`, `OpenProcess`, `QueryFullProcessImageNameW`, `CloseHandle`. `GetDpiForWindow` in `HidScale`. AutoHotkey `WinGetList`, `WinGetProcessName`, `WinGetTitle`, `WinGetPos`, `WinGetClientPos`, `MouseGetPos`, `FileRead`, `FileAppend`, `DirCreate`. **Guess:** the window list is `EnumWindows` plus a process-name query per window. That is the usual implementation of `WinGetList`, not something this reading verified by disassembly.

### Quirks the port must keep

The behaviour of `PickLive`, including the `activeId` tie-break among empty `focusExe` entries, and "no fallback means nothing runs".

Orphans are stopped when the live macro list no longer contains their id.

`goto` `where` is `screen`, `window`, or `client`. Window and client are the active window, origin plus the saved point.

The plan already moves the window list and the foreground lookup off the playback thread, replaces the relative profile path with `--profile`, and sets Per-Monitor V2. Keep the results those functions produce. Do not keep "they run on the tick" or "DPI awareness is not set".

### Where time likely goes

**Guess:** `ActiveExe` every tick is a foreground-window and process-name query, and it is on the playback thread today. **Guess:** `WindowListJson`'s first fill, or a fill after the cache is empty, walks every top-level window and is the large stall in `StateJson`. Later ticks reuse the string. A profile `JSON.Parse` of `default.json` is the other stall, which is why `hookSuspect` exists. **Guess:** `LocalToScreen` is cheap beside that, and it only runs on a goto step.

## Delay modes

Recorded, fixed, and no-delay are editor behaviour. The engine never sees a delay mode.

`ui/src/eventLane.ts` `liveSteps` builds steps from a recording. `"recorded"` inserts a `wait` of the gap between events. `"fixed"` inserts a `wait` of the fixed milliseconds. `"none"` inserts no wait. `applyDelayMode` in the same file, given `"none"`, drops `wait`, `scanWait`, and `randomWait`, and turns a tap into a down plus an up with no hold. `"fixed"` rewrites `wait` / `scanWait` to that many milliseconds, turns `randomWait` into a `wait` of that many milliseconds, and sets tap `holdMs`. #26 was the editor dropping Random Wait on those paths. It is closed. The engine only sees the steps that come out.

`ui/src/recording.ts` `compileRecording` is the other editor path: `"played"` keeps the recorded gaps as `wait` steps, `"custom"` either strips waits or replaces them with `holdMs`. The plan names `recording.ts` for #26. The three mode names currently live in `eventLane.ts`. Both files are editor code. Neither mode name is read by `MacroEngine.ahk`.

#27, scroll at 0 notches, is `scrollDy` in `eventLane.ts`. The engine adds `Round(dy)` and does not invent a sign.

## JSON

`engine/JSON.ahk`, class `JSON`. Used by `LoadDisk`, `Handle`, `J` in the self-test, `StateJson`, and `HidKeys`.

### What it does

`Parse` skips a leading UTF-8 BOM (`U+FEFF`), skips spaces, tabs, CR, and LF, and parses one value. It does not require the text to end there.

Objects become `Map`s. Arrays become `Array`s. `true` and `false` are the AutoHotkey booleans (and `true` compares equal to 1, which `AsBool` and `ClicksSwapped` rely on). `null` becomes `""`. Numbers with `.`, `e`, or `E` are floats. Other numbers are integers. Duplicate object keys overwrite.

`Quote` escapes backslash, quote, CR, LF, and tab. Any other character below `0x20` becomes `\u00XX`. The comment says window titles reach the state message, and any other control character makes the JSON invalid, so the shell drops the pipe.

Strings accept `\n`, `\r`, `\t`, `\uXXXX` as one `Chr`, and any other escape as the character itself. There is no surrogate-pair handling.

### State it owns

None.

### Win32

None.

### Quirks the port must keep

`null` is an empty string, not a missing key. `Field` then returns that empty string rather than the caller's default, because the key is present.

A bad pipe payload acks `"bad json"` and does not throw out of `Tick`. A bad profile file is the `LoadDisk` catch, not this class's problem to report.

Control characters in state JSON are escaped. The self-test says `"control chars are escaped in json"` and `"newline keeps the short escape"`.

`AsNum` still has to accept numeric strings, because a field can be a string. The self-test says `"bad number falls back instead of throwing"` and `"a step with a bad number still plays the rest"`.

### Where time likely goes

**Guess:** `Parse` is a character loop with no depth limit. A large `default.json` on `reload` is the expensive call, and it runs on the tick thread. `Quote` walks the string again when a title contains a control character. **Guess:** ordinary state JSON does not.

## Self-test

### What it does

`SelfTest` deletes `engine\self-test-result.txt` if it can, runs `RunChecks`, writes `PASS` or `FAIL n` to stdout and that file, and returns 0 or 1. An exception becomes one failed check named with the message and line.

`Check` appends `FAIL {label}` on a false condition and returns the new fail count.

`HasVal` is an array membership test used by the checks.

`J` replaces single quotes with double quotes and `JSON.Parse`s. The fixtures are not strict JSON in the source.

`RunChecks` is 244 `Check` calls. It is the behaviour spec. It uses `Engine.dry` so playback does not call `SendInput`. It does touch a real `F24` hotkey and, at the end, the real QPC clock with `timeBeginPeriod(1)`.

There are almost no comments inside `RunChecks`. The rules are the check labels plus the comments in the engine above them. The port keeps both.

### Comment rules

These are the comments in `MacroEngine.ahk` and `JSON.ahk` that constrain the port. Wording is tightened. The line is the comment, not every line it covers.

| Line | Rule |
|---|---|
| 18 | Do not use `SetTimer` for the clock. It cannot hold a 5 ms wait. |
| 24 | Do not kill the running engine to become the single instance. That releases held keys. |
| 69 | A bad number in a step must not throw out of `Advance`. That skips `Publish` and leaves keys held. |
| 97 | Backtick is escaped for `Send`. |
| 112 | Mouse buttons the engine sends carry the echo extra-info. A sent Mouse 4 is not the finger letting go. |
| 148 | Software `$*` swallows the trigger. Board `~$*` lets the Pico keys through. The low-level hook still eats the real keyboard trigger, or a Pico `b` is swallowed by the B macro. Unmatched focus uses `~$*` so typing works. |
| 160 | The hotkey also records the finger, so a release is seen if the low-level hook is behind. |
| 163 | In board mode the low-level hook owns trigger keys. A Pico tap is not another finger-down. |
| 183 | The mouse hook owns side buttons. The hotkey also fires for buttons the engine sends. |
| 186 | `SendLevel` can fire the hotkey after our own `Send`. That must not start another macro. |
| 189 | Once the low-level hook has recorded the finger, a late hotkey must not overwrite a real release. |
| 220 | Do not handle mouse move or the wheel in the mouse hook. |
| 259 | One key name. Old `"shift"` files are `LShift`. Left and right stay distinct. |
| 267 | Generic VK plus scan and the extended bit pick left versus right. |
| 373 | `holdLoop`, `swapLoop`, `onceHeld`, and `edgeHold` loop themselves. One parent pass lets After release run once spam stops. |
| 451 | Because those loop themselves, play once must not restart the whole graph. |
| 586 | Spam yields on release so the next block, including After release, can run. |
| 707 | A short-tap branch skips later top-frame steps except `burst` and `ifShort`. |
| 755 | A down and an up in the same tick are held apart by one tick, or the down is never sent. |
| 912 | After release runs only once the trigger is up. A down with no up stays held until this block's up. |
| 1014 | An up in this tick is sent before the next `onceHeld` down. |
| 1331 | Re-raise the physical hook after hotkeys are installed, or the macro hook swallows the real key-up. |
| 1344 | A stalled thread loses its low-level hooks while the handle still looks valid. Re-raise the keyboard hook and the mouse hook. |
| 1356 | After a profile parse, put both hooks back immediately. |
| 1384 | Mouse clicks mean the thread is alive. Do not treat a quiet keyboard hook as dead in that window. |
| 1408 | Unhook before reinstalling. A second install leaves the dead hook in place. |
| 1434 | Ignore injected `SendInput`. Match Pico HID against `hidEcho` instead of counting it as the finger. |
| 1513 | Pump hooks while waiting. Do not wake on mouse-move. Do wake on key and button input when `Pace` asks. |
| 1606 | Never busy-spin. An overdue wait still pumps. |
| 1635 | One runner that throws is marked done. The other runners still `Publish`. |
| 1647 | `Publish` before `RemoveDone`. The last step's move or scroll is a one-shot. |
| 1654 | First matching `focusExe` wins. An empty `focusExe` is the fallback, preferring `activeId`. |
| 1721 | A corrupt profile must not kill the process. Keep the last good one and report `profile-read`. |
| 1920 | Unmute must not let a plain while-held macro send one more tick after its key came up during the mute. |
| 1940 | A hold whose key is still down starts when the mute lifts, even if it was not running when the mute began. |
| 2030 | Output downs are echo. An up still counts unless this `Send` is in flight. |
| 2172 | Empty focus matches every window except the editor, or typing the macro name would start it and swallow the key. |
| 2192 | `Dawnwalker.exe` and `Dawnwalker-Win64-Shipping.exe` match. The stem rule is general, not a special case for that game only. `notepad` does not match `notepad++`. |
| 2209 | A WebView2 host in front counts as the shell when its root owner is `MacroShell.exe`. |
| 2269 | Swallow triggers only while focus matches. Otherwise the user can type, and `FocusOk` keeps macros off. |
| 2304 | `FingerDown` is the real finger, including keys this macro is not hooked to. |
| 2346 | While the physical hook is up, triggers use it. `GetKeyState` mixes in keys the macro is sending. |
| 2423 | Playback `onRelease` starts on key-up. It is not the `nextUp` / `finish` stop rule. |
| 2497 | A while-held or spam block already exits on trigger-up. Do not `AskStop` the whole runner, or blocks after it never run. |
| 2735 | Leave `sendNow` set until the next tick, and re-raise the physical hook after `Send`, or a while-pressed macro never sees the release. |
| 2758 | Scroll `dy` is positive for down. The board wheel is the opposite sign. |
| 2816 | Hid JSON uses logical button names. The shell applies `swapClicks`. |
| 3149 | A message-mode write is all or nothing. State is retried by the next tick. An error or ack gets a few tries of its own. |
| JSON.ahk 164 | Control characters in quoted strings are escaped, or the shell drops the state message. |

### What `RunChecks` locks beyond those comments

Grouped by the label text in `RunChecks`. A port that keeps the comments and fails these labels is still wrong.

- Hooks: letter and backtick specs, board passes, software swallows, unmatched focus does not swallow, the editor window does not swallow, no focus restriction swallows, ctrl uses `LControl`.
- Hid echo is consumed once. Hid JSON keeps logical names and can carry `abs` / `ax` / `ay`.
- `PickLive`: foreground miss uses the empty focus, the exe picks that config, a shipping exe matches the stem, no fallback means nothing runs.
- Scroll down and up, zero sends nothing, board wheel sign.
- Random wait swaps a reversed range, clamps negatives to 0, returns a fixed range unchanged, and stays inside the range.
- `swapClicks` swaps left and right for software, including JSON `true`, and leaves middle alone. Swap off leaves left.
- Basic side button clicks that button. The gap is the wait after an 18 ms hold. Left click is not a macro trigger. Play once finishes after one press. `repeatCount` 3 runs three times and then stops. The sprint fixture's `repeat` step has count 0, and that runner taps space again at t50. `Advance` treats `repeatsLeft` 0 as forever. The label `"infinite repeat"` itself only checks that the parsed count is 0.
- While pressed holds, and releases on stop. A recorded while-held sends its own trigger, ignores that send while the finger is down, and stops when the finger lets go. A sent mouse button is not a physical release. The mouse hotkey ignores buttons this macro sends. A real mouse release still ends the hold.
- A zero-delay press is sent and released on the next tick.
- Advanced: short tap tops up `minCycles` then runs `then`. A long hold skips the extra cycles and runs `then`. `busy` ignores a second press. A quick tap takes the short path and does not run the hold loop. Holding past the window starts the hold loop.
- Mute: the target pauses, a muted macro does not start, release restores only what was running, a key that stayed down does not start a second copy, a key that came up during the pause stays stopped, a key that stayed down starts when the mute lifts and sends again.
- Tap hold: presses z and the trigger, stays down while the watch is up, repeats z on the Windows repeat timer, waits out the arm, releases, presses again while the trigger is held, and does not press again if the trigger lets go during the gap.
- Spam: starts with the click and the trigger, lets the click up, clicks again, goes quiet for a tracked press, clicks again after the pause, stops when the trigger lets go, and a let-go during the pause runs After release.
- Ignore lock: `"macro"` still takes a physical press and skips a sent key. `"both"` skips a physical press inside the window and takes one after it.
- `"once"` is not a lock and leaves tracked macros running. `"block"` mutes, leaves other macros alone, keeps swallowing, and unmutes on release.
- A sent shift is not a physical press. `sendNow` blocks a late shift hotkey. The low-level hook owns the finger while a send hotkey fires. One macro sending shift does not start the shift macro.
- Watchdog predicates, as in the Hooks section.
- `nextUp` releases the first key and skips the rest. `finish` plays through. A block `releaseStop` overrides the macro. Play once without a hold loop does not use on-release. A while-held graph does not stop the whole runner on release, so later blocks run.
- Swap-after: a quick tap never reaches the second loop. Holding past the time does.
- Playback on release ignores the press and starts on the up edge.
- Scan wait: keys before the wait run, the wait exits if the trigger lets go, and it continues if the trigger stays.
- `onceHeld`: one press shoots after the gap, a fast release does not shoot early, holding through the chord does not shoot, letting go during the gap still waits the full gap, and a hold starts the next cycle without another gap.
- After release does not send the matching up while the trigger is held, and does send it when the trigger comes up.
- A 5 ms wait with `timeBeginPeriod(1)` finishes between 4.5 and 9 ms.

`HasVal` has no separate rule. It is the helper those checks use.

### Win32

The clock check uses `timeBeginPeriod` / `timeEndPeriod` and `Now` / `Pace`. The hotkey check uses `Hotkey` on `F24`. The rest is in-process.

### Where time likely goes

Not on the playback tick. The 5 ms check is the one place the script states a number for the clock. #39 measures the real machine. This file does not.

## Closed issues as current behaviour

Do not reopen these. Do not port the old behaviour.

| Issue | Current behaviour the port keeps |
|---|---|
| #13 | Shell board path. A down may queue on the lock (40 ms, queue capped at 16). An up waits (1500 ms). |
| #14 | Engine watchdog re-raises the mouse hook as well as the keyboard hook. |
| #17 | Firmware leaves the keyboard report dirty on `hid-busy` and retries from `send_reports`. |
| #26 | Editor. Fixed and no-delay handle `randomWait` before the engine sees the steps. The engine only runs `wait` and `randomWait`. |
| #27 | Editor. A 0-notch scroll does not flip sign. The engine receives `dy` and, on the board, negates it. |
| #34 | Editor save checks. The engine no-ops a `run` whose id is not in the live list. |
| #35 | Editor. The engine treats a missing `steps` array as empty. An empty `repeat` does not throw. |

## Pipe messages

Feeds #42. Fields the engine does not read are ignored, not rejected.

### In

| `type` | Fields the engine reads | Reply |
|---|---|---|
| `profile` | `profile`, and only if that value is a `Map` | `{"v":1,"type":"ack","for":"profile","ok":true}` always, when the type matched. No id. |
| `output` | `mode`: `rp2040`, `rp2350`, otherwise `software` | None. A change flushes applied keys, clears hooks, forgets the mouse point, and rebinds. |
| `command` | `action`: `start` (`SetArmed(true)`), `stop` (`SetArmed(false)`), `reload` (`LoadDisk`), `quit` (ack, then `ExitApp`). Any other action still takes the ack path. | `{"v":1,"type":"ack","for":"command","ok":true}`. No id. |
| anything that is not JSON | | `{"v":1,"type":"ack","ok":false,"error":"bad json"}`. No `for`. No id. |
| JSON that is not an object, or an unknown `type` | | None. |

The shell sends `profile` at connect (`SendProfileEnvelope`), `output` after that, and forwards page `command` messages. A page save is turned into `{"v":1,"type":"command","action":"reload"}` rather than a piped profile. The engine still accepts a piped `profile`.

### Out

| `type` | When | Body |
|---|---|---|
| `hello` | Client connects | `v`, `type`, `armed` |
| `state` | Signature change, or forced after hello, profile, arm, and load | `v`, `type`, `armed`, `running` (`id`, `name`), `held`, `activeId`, `front`, `windows` (`exe`, `title`) |
| `ack` | As above | `v`, `type`, optional `for`, `ok`, optional `error` |
| `error` | Profile file could not be read | `v`, `type`, `where` `"engine"`, `code` `"profile-read"`, `detail` |
| `hid` | Board emit | `v`, `type`, `down`, `up`, `x`, `y`, `wheel`, and when absolute: `abs` true, `ax`, `ay` |

`activeId` is on every state snapshot. Command acks do not echo an id. Neither do profile acks.

The shell turns `hid` into `K {usage} {0|1}` and `M {bits} {dx} {dy} {wheel}`. The engine does not write those lines.

## Ignored profile fields

| Field | Where it lives | Engine |
|---|---|---|
| `variables` | Profile | Stored on the profile map. Written as `{}` only when creating a missing default file. Never read. |
| `priority` | Macro | Stored because the macro map is stored. Never read. Leave it ignored. |
| `pressHold` | Block, legacy | Not ignored. The editor rewrites it on save. `CompileBlocks` still handles a raw file. See the legacy section. |

Also stored and unused: profile `version`, and the profile `name`. Macro `name` is copied onto the runner and sent in `state`. Do not invent behaviour for the unused profile fields.

Fields the engine does read: `id`, `enabled`, `playMode`, `repeatCount`, `releaseStop`, `busy`, `exclusive`, `advanced`, `basic`, `gapMs`, `speed`, `blocks`, `steps`, `trigger`, `focusExe`, `activeId`, `configs`, `macros`, `swapClicks`, and macro `name`.

## Where time likely goes

Not measured. #39 is the measurement, from a separate process, on the Windows PC. `windows-latest` is not that report.

**Guess, in the order this reading would look first:**

1. Profile `JSON.Parse` on the tick thread during `reload` / `LoadDisk`, and any piped `profile`. This is the stall `hookSuspect` exists for.
2. `WindowListJson` when the cache is empty: a walk of every window, from `StateJson`, on the tick. Steady state reuses the string, and an active runner never refreshes a non-empty cache.
3. `ActiveExe` once per tick: foreground window and process image. Smaller than the window walk, and it does not wait for a cache expiry.
4. Software `Send` plus `RaisePhysicalHook` (unhook and rehook) on every key emit.
5. The shell board lock. An up can wait up to 1500 ms off the engine thread. That is not AHK time, and it is still part of a hid report's latency.
6. `HrtWait` / `timeBeginPeriod`. The 1..8 ms slice is the playback quantum. The 5 ms self-test is the only number in the script (4.5 to 9 ms).
7. Map lookups in `PollTriggers`, `Desired`, and `Publish`, and string builds for `HidJson` and `StateJson`. **Guess:** these show up only after the calls above are gone.

`QueryPerformanceCounter` itself is not the suspect. **Guess:** it is cheap beside a window walk or `SendInput`.
