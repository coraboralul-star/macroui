# Parity

#46 and the harness slice of #49. Differences from `engine/MacroEngine.ahk` that this tree already accepts, and the checks that are not ported yet.

The fake clock is the caller. `engine_runner_advance` takes `now` in the same unit as the QPC frequency stored on the runner. `engine-runner-test` uses frequency 1000, so the self-test's millisecond numbers land on the same integers. A frequency of 10000000 is checked once, and the wait becomes `scaled_ms * freq / 1000` with the truncation in `docs/engine/architecture.md`. The runner does not read the clock.

`Advance` does not allocate. The caps are 32 runners, 16 frames, 64 holds, and 256 steps in the play arena. The arena is not reused until `engine_play_init`. Profile load replaces it with the immutable image. A full runner list, or a full arena, refuses the new start and does not stop the others. A full frame stack marks that runner done. A full hold list does the same. The AHK maps grow. These caps are the native bound.

## Ported

`engine-runner-test` covers the self-test labels for the sprint fixture, the basic key, play-once, play-mode repeat, while-held, the zero-delay tap, and busy. It also covers move, scroll, an injected `randomWait`, `nextUp`, `finish`, `goto`, `run`, `holdLoop`, `swapLoop`, `burst`, `ifShort`, `onceHeld`, `edgeHold`, and `scanWait`.

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

## burst

`burst` stores the child span in `first` and `count`, and `forMs` in `a`. `0` and a negative `a` run the children once. A positive `a` is divided by speed. The tick count truncates toward zero. It is not forced up to 1 ms. The step builder stores `0` when the file omitted `forMs`. `c` stays `swapLoop` `afterMs`.

The finger snapshot is the trigger. A reported down waits 1 ms, and that yield is not divided by speed. The children run when the trigger is up. An empty trigger, a key that was never reported, and a missing snapshot are not down, so the children run. `engine_runner_advance` has no snapshot, so it does not wait.

A positive duration sets `burst_until` on the frame from the moment the block starts. The children restart while that time is still ahead. The check is at the end of the frame, so the current pass finishes. An empty body hits the 64-step yield until the time is up.

## ifShort

`ifShort` stores the child span in `first` and `count`, `underMs` in `a`, and `minCycles` in `b`. `underMs` is compared to time since the runner started and is not divided by speed. The 1 ms yield is not divided by speed either. `0` and a negative `a` skip the children. The step builder stores `150` and `3` when the file omitted them. `c` stays `swapLoop` `afterMs`.

A reported down that is still under `underMs` waits 1 ms. Once the hold has reached `underMs`, the children are skipped and `took_short` stays clear, so later steps still run. A release before `underMs` sets `took_short` and pushes the children for `minCycles - cycles` repeats. Fewer than 1 repeat pushes nothing, and `took_short` is still set. `took_short` skips later top-frame steps except `burst` and `ifShort`. Steps inside the short children still run.

An empty trigger, a key that was never reported, and a missing snapshot are not down. `engine_runner_advance` has no snapshot, so it does not yield. `cycles` counts finished `holdLoop` passes and finished `onceHeld` passes. A short tap tops the short block up to `minCycles`. A long hold skips that block.

## onceHeld

`onceHeld` stores the child span in `first` and `count`, and `gapMs` in `a`. The step builder stores `70` when the file omitted `gapMs`. `gapMs` is divided by speed when the frame is built. `0` and a negative `a` do not wait. A positive gap waits through the normal arm path. `c` stays `swapLoop` `afterMs`.

`stopping` skips the step before the frame is pushed. The children run first. An empty trigger, a key that was never reported, and a missing snapshot count as up. `engine_runner_advance` has no snapshot, so the trigger counts as up.

A held trigger waits out the gap before the children run again. The 1 ms poll during that wait is not divided by speed. A release during the gap finishes the gap, then pops. The next block then runs. A hold that is still down when the gap ends starts the next pass without another gap. An empty child list waits 1 ms. `releasedNow` waits 1 ms before the next down. Those yields are not divided by speed.

`cycles` increments once per finished pass. `ifShort` reads that count. `took_short` skips `onceHeld` on the top frame. `holdLoop` cycles still count.

## edgeHold

`tapHold` and `tapSpam` both map to `edgeHold`. `spam` is set only for `tapSpam`. The step stores the edge key, `armMs` in `a`, `gapMs` in `b`, `holdMs` in `d`, `restMs` in `e`, and `ignoreMs` in `f`. `c` stays `swapLoop` `afterMs`. The watch keys are a span in `first` and `count`. Those steps are not executed. The step builder stores key `z`, `armMs` 5, `gapMs` 80, `holdMs` 18, and `restMs` 18 when the file omitted them. At most 8 watch keys are copied. A later key in that span is not watched.

The push holds the edge key, and the trigger when it is a different key, then waits 5 ms. That poll is not divided by speed. `stopping` or a trigger that is up marks a `tapHold` runner done, including during that wait. An empty trigger does not. A missing snapshot and a key that was never reported count as up. `engine_runner_advance` has no snapshot, so a non-empty trigger counts as up.

A watched rising edge waits `armMs`, drops the edge key and the trigger hold, then waits `gapMs`. Both are divided by speed at the wait. `0` and a negative value still wait 1 ms. When the gap ends with the trigger still down, the edge key is held again. The trigger hold is not put back. A release during the gap marks the runner done and does not press the key again.

`RepeatHold` sets an extra down on a keyboard key that is already held. The first wait is 400 ms and the repeat is 33 ms, the dry self-test numbers. They are not divided by speed. The runner does not call `SystemParametersInfo`. Mouse buttons are not repeated. The extra down is passed as the `extra` list to `engine_output_publish`. That is the report image from #45. This slice does not call `SendInput`.

`tapSpam` toggles the edge key on `holdMs` and `restMs`. Those were divided by speed when the frame was built. A watched rising edge drops the key and waits `gapMs`. A release drops the holds, releases the pause-block mute, pops, and lets the next block run. `stopping` marks the runner done.

`AcceptWatch` keeps `"macro"` and `"both"`. `"once"` and every other ignore name are off. `"both"` rejects a repeat of that watch key inside `ignoreMs`. `"macro"` rejects inside the window only when a live runner holds or pulses that key, or the key was sent at or after the lock time. A reject does not refresh the lock time. An accept stores `now`. `ignoreMs` is not divided by speed.

`NormalizePause("once")` is off. Do not treat `"once"` as a lock. `"block"` mutes other catalog macros whose trigger is one of the watch keys, and pauses a runner that is already playing. The owner's own id is left out. At most 8 of those macros are muted. Releasing the frame unmutes them. The mute replay list does not include `edgeHold`.

`took_short` skips `edgeHold` on the top frame. `engine_has_hold_loop` already includes it.

## scanWait

`scanWait` stores `ms` in `a`, the same field as `wait`. `ms` is divided by speed when the wait is armed. `0` and a negative `a` still wait 1 ms through that arm path.

A non-empty trigger that the finger snapshot says is up marks the runner done and returns a wait. An empty trigger does not. The check is the finger snapshot, the same one `holdLoop` and `onceHeld` read. It is not a separate edge-hold key check. A missing snapshot and a key that was never reported count as up. `engine_runner_advance` has no snapshot, so a non-empty trigger counts as up.

While `scan_wait` is set, `stopping` or a trigger finger that is up ends the runner during the wait. `scan_wait` is set only when the step runs, and it is cleared after the wait finishes. A plain `wait` and `randomWait` clear it when they arm.

`took_short` skips `scanWait` on the top frame. There is no extra case for it.

## Not executed yet

`pressHold` still expands to several steps, and that expansion is not compiled yet. `then`, `ifShort`, `onceHeld`, `tapHold`, and `tapSpam` already map to their numbers.

`priority` and `variables` are not read.

A name `engine_key_id` does not know is an empty key, and the step does nothing. The ids are the #44 name table, the alias targets that table already names, and tab, escape, backspace, the arrows, insert, and delete. OEM punctuation is not an id yet. A raw profile that sends one of those keys is skipped until the table has a virtual key.

Fractional milliseconds are not stored. The step record holds the integer the caller passed. The editor saves integers. A fraction in a raw file is #47's parse.

The finger snapshot is the physical key for `holdLoop`, the trigger for `onceHeld`, the trigger and watch keys for `edgeHold`, and the trigger for `scanWait`.
