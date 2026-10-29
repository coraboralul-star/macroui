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
