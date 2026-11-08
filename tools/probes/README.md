# Probes

## move-probe (#97 step 0)

Can a low-level mouse hook (`WH_MOUSE_LL`, which both engines use to swallow input) stop a game from seeing mouse movement while the radial menu is open? Games usually turn the camera from Raw Input, so the probe reads Raw Input in a separate process, the way a game would.

```
cmake -S tools/probes -B tools/probes/build -A x64
cmake --build tools/probes/build --config Release
```

Each role runs as its own process: `raw <ms>` reads Raw Input, `hook <ms>` swallows every move and wheel notch, `inject <n>` / `wheel <n>` send input, `cursor <ms>` reports how far the cursor moved.

### Results (2026-10-30, Windows 11 Pro 10.0.26300)

| Run | Hook swallowed | Cursor moved | Raw Input reader got |
|---|---|---|---|
| 200 sent moves, +800 X, no hook | none | x=600 (screen edge) | 129 events, x=800 |
| 200 sent moves, +800 X, hook on | 200 of 200 | x=0 | 125 events, x=800 |
| Physical mouse, 10 s, hook on | 8,855 moves | x=48, y=−1 (before the hook was in) | 1,236 events, x=+1,591, y=−329 |
| 30 sent wheel notches, no hook | none | | 30 notches |
| 30 sent wheel notches, hook on | 30 of 30 | | 30 notches |

The hook freezes the Windows cursor, but every move and wheel notch still reaches Raw Input. A game that reads Raw Input keeps turning its camera and still sees the wheel.

Keys are different: a key the keyboard hook swallows does not reach Raw Input (see `engine-native/src/win/raw_win.h`). So the radial menu picks slots with keys by default (1 to 8, plus two step keys, Q and E unless changed), swallowed only while the menu is open. Flick stays an option, with a warning that the camera turns too. No filter driver.

## slow-probe

Opt-in experiment for a Windows practice range. While a gate is held and the foreground exe matches, the probe reads physical relative raw counts and injects the opposite of half of them with `SendInput`. `s` is exactly 1/2. It is not device DPI, not the game's sensitivity setting, and not Windows pointer speed. A fast swipe can still kick forward and pull back when the game samples the physical packet and the counter in different frames.

The macro engines do not do this. Native software output injects only the macro's own moves. The board is a second real mouse and is the wrong latency for a same-frame counter, so a board packet is ignored here and cannot feed the next one. AutoHotkey's raw handler runs on the low-level hook thread. `SendInput` from that thread can stall the hook until Windows drops it, so there is no AHK port of this probe.

Nothing is countered until the gate is held in a named window. Release, a focus change, the panic key, and process exit throw away a fractional remainder and inject nothing further. The gate is not swallowed, so the game still sees that hold. Pick a gate that is not an ability. The default does not change pointer speed, the registry, or a device setting.

```
cmake -S tools/probes -B tools/probes/build -A x64
cmake --build tools/probes/build --config Release
tools\probes\build\slow-probe.exe
```

Defaults: hold mouse 5 (`xbutton2`), foreground `Overwatch.exe`, panic `End` (Ctrl+C also leaves). `--self-test` prints `slow-probe 0` and does not register input.

```
slow-probe --gate xbutton1
slow-probe --gate mbutton
slow-probe --gate lalt --panic end
slow-probe --focus Overwatch.exe --focus AimLab_tb.exe
```

`--focus` is the file name, repeatable. Passing it replaces the default rather than adding to it. A name that is not in front is left alone. `slow-math-test` checks the half-count remainder, null and board exclusions, and arm, release, focus, panic, and exit. It does not move a mouse.

### Practice-range check

Stand still in the Soldier 76 practice range, or an aim-trainer workshop in the same `Overwatch.exe`. Leave the macro engine's own mouse moves idle. Watch the probe console.

1. Baseline. Probe running, gate up. From a marked start, one smooth horizontal swipe across a fixed landmark. Note where the crosshair ends.
2. Half. Reset to that same start and the same hand travel. Hold the gate before the swipe, then release it.
3. Read the `disarm-release` line. A healthy one looks like `phys=120,-4 counter=-60,2 sent=40 null=40 ... fail=0`. `counter` is the opposite of `phys`, near half. `null` about equal to `sent` is each injection arriving back with no device handle. Those are not counted again.

Pass on gain: the held swipe covers about half the baseline angle, and `counter` is the opposite sign of `phys` at about half the magnitude. `fail=0`.

Fail on gain: the angle matches the baseline (injection ignored, or the window was not focused), the turn is near zero or reversed (the game scales a null-handle packet differently), or two swipes of the same hand travel disagree by a lot.

Pass on smoothness: one continuous slower turn.

Fail on smoothness: the view jumps ahead and snaps back during the swipe. A very slow swipe may stair-step by single counts. That is the integer remainder, not a DPI step. Stop if `phys` and `counter` run away instead of staying near half. `null` rising about once per `sent` is the injection coming back with no device handle, and those packets are not counted again.

`focus Name match=0` means the foreground exe is not on the list, so the gate will not arm. `raw input unavailable` means registration failed and nothing will be countered.
