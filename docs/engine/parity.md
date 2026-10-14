# Parity

#46 and the harness slice of #49. Differences from `engine/MacroEngine.ahk` that this tree already accepts, and the checks that are not ported yet.

The fake clock is the caller. `engine_runner_advance` takes `now` in the same unit as the QPC frequency stored on the runner. `engine-runner-test` uses frequency 1000, so the self-test's millisecond numbers land on the same integers. A frequency of 10000000 is checked once, and the wait becomes `scaled_ms * freq / 1000` with the truncation in `docs/engine/architecture.md`. The runner does not read the clock.

`Advance` does not allocate. The caps are 32 runners, 16 frames, 64 holds, and 256 steps in the play arena. The arena is not reused until `engine_play_init`. Profile load replaces it with the immutable image. A full runner list, or a full arena, refuses the new start and does not stop the others. A full frame stack marks that runner done. A full hold list does the same. The AHK maps grow. These caps are the native bound.

## Ported

`engine-runner-test` covers the self-test labels for the sprint fixture, the basic key, play-once, play-mode repeat, while-held, the zero-delay tap, and busy. It also covers move, scroll, an injected `randomWait`, `nextUp`, `finish`, `goto`, `run`, `holdLoop`, and `swapLoop`.

Windows CI runs the AHK `--self-test` as well. That script is still the whole 244-check spec. The native file is the part the runner can execute.

The `repeat` step is in this slice because those first playback checks use it.

## run and goto

`run` stores a catalog index in `a`. `engine_play_set_macros` is that catalog. A negative index, or an index past the catalog, does nothing. The target starts as `once` with `restart` false, so a live id is left alone. There is no cycle guard. The target is flushed before publish and does not advance until the next tick. `engine_runner_advance` has no catalog, so a `run` there does nothing.

`goto` stores `x` in `a`, `y` in `b`, the where code in `action`, and the duration in `count`. `0` does not wait. The compiler stores `15` when the file omitted `ms`. `engine_goto_where` maps `window` and `client`. Any other name is `screen`. The screen point is the snapshot origin plus `x` and `y`. A missing window or client rect skips the step. The first goto of a runner clears the remembered point before the delta, including when that rect is missing. Later gotos on a board use the remembered point. Software uses the snapshot every time. Publish sends the absolute point and drops the relative sum from that tick. The wheel stays. The last goto in the runner list wins.

## holdLoop and mute

`holdLoop` stores the child span in `first` and `count`, `swapAfterMs` in `a`, and the frame release stop in `b`. `0` does not swap. `b` keeps `finish` and `nextUp`. Anything else is none, and the macro's stop mode is used. `mute_at` and `mute_n` index the macro's mute table. Those entries are catalog indexes, the same kind `run` stores. The owner's own id is left out. `engine_play_advance` is the path that runs it. `engine_runner_advance` still skips it.

The finger snapshot is `engine_play_set_fingers`. A reported key that is down keeps the loop. A reported key that is up skips the loop before it starts, and ends it after the current pass. A key that was never reported still starts the loop, then the pass ends because it is not down. A missing snapshot is not a finger, so it does not set drain. An empty body waits 1 ms while the finger is down. `swapAfterMs` is compared to time since the runner started and is not divided by speed. When it is reached, and the finger is still down, the mute drops and the frame pops. The next block then runs.

Mute is a count on the play struct. The first count pauses that runner and remembers whether it was running. Nested counts do not unpause early. At 0 the runner is unpaused. A plain `whileHeld` whose trigger is up is marked done, so it does not play one more tick. `advanced` macros are left to the hold loop. A hold that was not running starts when the count hits 0 if its trigger is still down and it is `whileHeld` or its top steps contain `holdLoop`, `swapLoop`, or `onceHeld`. The trigger poll is not in this slice, so a muted id can still be started by `engine_play_start`. `engine_play_muted` is the check the poll will use.

`nextUp` sets drain on a `holdLoop` frame when the trigger is up. A down on that frame is ignored. An up still runs, and the rest of the frame is skipped. `finish` does not set drain.

## swapLoop

`swapLoop` stores the child span in `first` and `count`, `swapAfterMs` in `a`, the frame release stop in `b`, and `afterMs` in `c`. `0` does not wait. A negative `c` does not wait. The step builder stores `150` when the file omitted `afterMs`. `afterMs` is compared to time since the runner started and is not divided by speed. The 1 ms yield while the finger is still short of that time is not divided by speed either. `engine_play_advance` is the path that runs it. `engine_runner_advance` still skips it.

`stopping` skips the step. A reported key that is up skips it. An empty trigger skips it. A key that was never reported skips it. A missing snapshot is not a finger, so the step is skipped. A reported key that is down waits until `afterMs`, then pushes the same hold frame as `holdLoop`. Mute, `swapAfterMs`, drain, and an empty body stay on that frame. A release before `afterMs` skips the loop and runs the next step.

## Not executed yet

`burst`, `ifShort`, `onceHeld`, `edgeHold`, and `scanWait` have type numbers. `Advance` skips them. `then`, `ifShort`, `onceHeld`, `tapHold`, and `tapSpam` already map to those numbers. `pressHold` does not: it still expands to several steps, and that expansion is not compiled yet.

`priority` and `variables` are not read.

A name `engine_key_id` does not know is an empty key, and the step does nothing. The ids are the #44 name table, the alias targets that table already names, and tab, escape, backspace, the arrows, insert, and delete. OEM punctuation is not an id yet. A raw profile that sends one of those keys is skipped until the table has a virtual key.

Fractional milliseconds are not stored. The step record holds the integer the caller passed. The editor saves integers. A fraction in a raw file is #47's parse.

The finger snapshot is the physical key for `holdLoop`. `scanWait` is still skipped, so it does not read that snapshot yet. `NormalizePause("once")` stays off when that block is ported. Do not treat `"once"` as a lock.
