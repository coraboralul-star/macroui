# Parity

#46 and the harness slice of #49. Differences from `engine/MacroEngine.ahk` that this tree already accepts, and the checks that are not ported yet.

The fake clock is the caller. `engine_runner_advance` takes `now` in the same unit as the QPC frequency stored on the runner. `engine-runner-test` uses frequency 1000, so the self-test's millisecond numbers land on the same integers. A frequency of 10000000 is checked once, and the wait becomes `scaled_ms * freq / 1000` with the truncation in `docs/engine/architecture.md`. The runner does not read the clock.

`Advance` does not allocate. The caps are 32 runners, 16 frames, 64 holds, and 256 steps in the play arena. The arena is not reused until `engine_play_init`. Profile load replaces it with the immutable image. A full runner list, or a full arena, refuses the new start and does not stop the others. A full frame stack marks that runner done. A full hold list does the same. The AHK maps grow. These caps are the native bound.

## Ported

`engine-runner-test` covers the self-test labels for the sprint fixture, the basic key, play-once, play-mode repeat, while-held, the zero-delay tap, and busy. It also covers move, scroll, an injected `randomWait`, `nextUp`, and `finish`.

Windows CI runs the AHK `--self-test` as well. That script is still the whole 244-check spec. The native file is the part the runner can execute.

The `repeat` step is in this slice because those first playback checks use it. `run` and `goto` are not.

## Not executed yet

`holdLoop`, `swapLoop`, `burst`, `ifShort`, `onceHeld`, `edgeHold`, `scanWait`, `goto`, and `run` have type numbers. `Advance` skips them. `whileHeld`, `swapAfter`, `then`, `ifShort`, `onceHeld`, `tapHold`, and `tapSpam` already map to those numbers. `pressHold` does not: it still expands to several steps, and that expansion is not compiled yet.

`priority` and `variables` are not read.

A name `engine_key_id` does not know is an empty key, and the step does nothing. The ids are the #44 name table, the alias targets that table already names, and tab, escape, backspace, the arrows, insert, and delete. OEM punctuation is not an id yet. A raw profile that sends one of those keys is skipped until the table has a virtual key.

Fractional milliseconds are not stored. The step record holds the integer the caller passed. The editor saves integers. A fraction in a raw file is #47's parse.

There is no finger snapshot here, so `NoteHoldRelease` and `scanWait` are not wired. `NormalizePause("once")` stays off when that block is ported. Do not treat `"once"` as a lock.
