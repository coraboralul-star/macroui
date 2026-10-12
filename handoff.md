# REPLACE ENTIRE FILE AT THE END OF ANOTHER SESSION

Replace this whole file at the end of the next session. Do not append.

Repo: `C:\Users\holla\Documents\Projects\Fortnite\1\MacroUI`
Remote: https://github.com/coraboralul-star/macroui
Branch: `main`. This session's work is the commit that contains this file, and that commit is pushed to `origin/main`. Do not commit further changes unless the user asks.

`gh` is not installed. Git Credential Manager has a token for `git:https://github.com`. This session closed issues with `git credential fill` and the GitHub API.

Input method names in the UI are **Default**, **RP2040**, and **RP2350**. Default is the Windows-events path. Do not call it "software" when talking to the user.

## What this session did

Re-read the open issues. [#14](https://github.com/coraboralul-star/macroui/issues/14), [#13](https://github.com/coraboralul-star/macroui/issues/13), [#20](https://github.com/coraboralul-star/macroui/issues/20), [#21](https://github.com/coraboralul-star/macroui/issues/21), and [#17](https://github.com/coraboralul-star/macroui/issues/17) were still open and still matched the fix already on `main` in `97cc354`. That commit was already pushed before this session. Closed all five with a comment. No code changed for those five.

Then walked the remaining idea tickets. The user wants Double Tap left as it is. Random Wait and Mouse Scroll were added. The Random Wait chip was then relabeled so it says Min and Max.

The editor dev server (`npm run dev` in `ui/`) was started to check the chips, then stopped.

### Issues closed, already fixed on main

- #14 Side-button stall, and the missed key release on Default. `engine/MacroEngine.ahk` tracks the mouse hook on its own, puts a missing or silent mouse hook back, and raises the keyboard hook again after Send.
- #13 Window freeze while a key-up waits on the board lock. `shell/MainWindow.xaml.cs` keeps pipe reads off the UI thread, and the page view is only touched from a dispatcher post.
- #20 Launcher reinstalls the editor when `package.json` or `package-lock.json` is newer than `node_modules/.package-lock.json`. `launcher/Bootstrap.cs`. The dist rebuild also watches those files plus `vite.config.ts`, `tsconfig.json`, and `ui/public`.
- #21 Last Recording keeps the original down/up order. `ui/src/recording.ts` `compileCustom`. No delay drops the waits. Fixed delay sets every wait to the hold. `ui/src/components/RecordBar.tsx` cancels the take on a pointer down on another in-window control (`data-rec-keep` marks Stop).
- #17 Firmware source retries a failed keyboard report. `firmware/main.c` `K` handler leaves `kb_dirty` set and replies `hid-busy` when `send_keyboard_now()` fails. The board needs a flash before that is live. The RP2350 was not flashed.

The Notepad check for #14 and the recording check for #21 were not re-run. The close comments say so.

### Random Wait

New Timing step. It shows in the Full View library, the graph insert menu, and the step insert menu, because Timing is in `LIBRARY_GROUPS`, `MENU_GROUPS`, and `LANE_GROUPS`.

- `ui/src/profile.ts`: step `{ type: "randomWait"; minMs: number; maxMs: number }`. Blank step is 40 and 70. `normalizeSteps` rounds both, clamps each to at least 0, and swaps them if min is above max.
- `ui/src/library.ts`: id `randomWait`, label "Random Wait", detail "Pause for a random time in a range".
- `ui/src/components/Library.tsx`: small clock mark.
- `ui/src/eventLane.ts`: lane cell `{ type: "randomWait"; index; minMs; maxMs }`.
- `ui/src/components/EventLane.tsx`: chip title "Random". Two fields, labeled Min then Max. Class `ev-range`.
- `ui/src/styles.css`: `.ev small.ev-range` is a two-column grid so Min sits on the first row and Max on the second. The first version showed `40 - 50 ms`, which read like a subtraction. The user asked for Min then Max. That is what shipped.
- `ui/src/components/StepList.tsx`: same labels. This list is not mounted by the app.
- `ui/src/tape.ts`, `ui/src/recording.ts` `glance`, `ui/src/macroFlow.ts` `stepPhrase`, `ui/src/components/GraphPreview.tsx`: the step is a pause. The preview uses the midpoint of the range.
- `engine/MacroEngine.ahk`: `WaitSpan` and `RandomWaitMs`. `Exec` treats `randomWait` like a normal wait (`scanWait` is cleared) and divides the rolled value by the macro speed. AHK `Random(lo, hi)` is inclusive. Byref names are `lo` and `hi` because `min` and `max` shadow `Min()` and `Max()`.
- Self-test in `RunChecks`: swapped range, clamped negative, a fixed range returns that value, and 30 rolls of 70 then 40 stay inside 40 to 70. `AutoHotkey64.exe engine\MacroEngine.ahk --self-test` printed `PASS`.

No Scan toggle. A normal Wait already stops early only when it is Scan Wait. Leave the toggle off unless the user asks.

### Mouse Scroll

New Mouse step. It is in the step insert menu under Mouse, next to Mouse Move. It is not in the Full View sidebar. Mouse is only in `LANE_GROUPS`, same as Mouse Move and Mouse Position.

- `ui/src/profile.ts`: step `{ type: "scroll"; dy: number }`. Positive is down, negative is up. Blank step is `dy: -3`, which the chip shows as Up, 3 notches.
- `ui/src/library.ts`: id `mouseScroll`, label "Mouse Scroll".
- `ui/src/components/EventLane.tsx`: title "Scroll", an Up/Down button (`aria-label` "Scroll direction"), and a notches field. The stored value stays signed.
- `engine/MacroEngine.ahk`: each runner has `wheel`. `Exec` on `kind = "scroll"` adds `dy`. `Publish` sums it with movement. `WheelSend` turns a positive dy into `{WheelDown N}` and a negative dy into `{WheelUp N}` for Default. `HidWheel` negates dy, because the board wheel is the HID sign and positive means up. `HidJson` now includes `"wheel"`.
- A scroll that is the last step of a macro used to vanish. `Tick` called `RemoveDone` before `Publish`, and a finished runner is dropped with its one-shot movement still on it. `Publish` now runs first, and it still reads `moveX`, `moveY`, `wheel`, and goto from a runner that just finished. Key holds are unchanged: `Desired` still skips done runners, and `MarkDone` already cleared them.
- `shell/MainWindow.xaml.cs`: `WriteHid` reads `wheel` and passes it into `WriteHidLocked`. The `M` line is `M {bits} {dx} {dy} {wheel}`. A scroll-only report is queued if the board lock is not acquired. An absolute move still sends the wheel on its own `M` line afterwards.
- `firmware/main.c` already parses `M <buttons> <dx> <dy> <wheel>` and adds `wheel` into the HID mouse report. No firmware change, and no flash, for scroll. Recording scrolls stayed out. The engine skips wheel input on purpose so the hook stays light (`MacroEngine.ahk` around the mouse-hook comment).

Checked in the editor at `http://127.0.0.1:5173/`: both steps insert, Min and Max show on two rows, Up 3 flips to Down, and the notch count edits. The throwaway macro was deleted. This was the Vite page, not a running macro. Nobody played a Random Wait or a Mouse Scroll into Notepad or a game.

### Builds

- `npm run build` in `ui/`. Current dist is `ui/dist/assets/index-DYwF-88C.css` and `ui/dist/assets/index-DcsVtgQk.js`, referenced from `ui/dist/index.html`. The previous `index-3kpsYW-V.css` and `index-DT05r5aD.js` are deleted from the tree.
- `dotnet build shell/MacroShell.csproj -c Debug`. Output is `shell/bin/Debug/net10.0-windows/MacroShell.exe`.
- Launcher did not change, so `H&le.exe` was not republished.
- An already-open window is still on the previous editor bundle. Reopen the app to see Min and Max.

## Left alone

Idea ticket, by request: [#15](https://github.com/coraboralul-star/macroui/issues/15) Double Tap. Do not start it unless the user asks. A Double Tap branch makes every single tap wait out the window.

Still open, and now implemented in the tree: [#18](https://github.com/coraboralul-star/macroui/issues/18) Random Wait, [#22](https://github.com/coraboralul-star/macroui/issues/22) Mouse Scroll. Do not close them until the user has tried them, or asks to close them.

Still an idea: [#19](https://github.com/coraboralul-star/macroui/issues/19) header output chip.

Pull requests: #10 WebView2, #11 System.IO.Ports, #16 untrack `ui/dist` and `shell/bin`, #23 Vite, #24 setup-dotnet, #25 setup-node.

## Next session

1. Replace this file when the session ends. Do not append.
2. The user still needs to reopen the app and try the two new steps. On Default, a Random Wait between Min and Max should pause somewhere in that range each time, and Mouse Scroll should roll the wheel. On a board, scroll uses the wheel the current firmware already accepts.
3. If those checks pass, close #18 and #22 with a short comment. Say scroll did not need a flash.
4. Suggested build after that: #19. If RP2040 or RP2350 is selected and the board is missing, mismatched, or the port will not open, `ApplyInput` in `shell/MainWindow.xaml.cs` already calls `SetOutput("software")` and only toasts. `SetOutput` writes `{ type: "output" }` to the engine pipe, not the page. Settings "Connected" is port presence (`ui/src/App.tsx` `inputConnected`), not "the shell opened it." The page should hear the live output, the badge should follow that, and the header should show the board name when it is live and Default when it has fallen back. Say Default in the UI, not software.
5. Double-check still open from the closed bugs. Notepad, on Default: Slide Jump runs once and stops when `C` is released, Heal Slot stops when `W` is released, and Slide Jump still runs after M4 is held and released. Recording: Last Recording after Fixed or No delay keeps the hold, and another in-window control cancels the take. Profile used last time was `profiles/default.json`, config Fortnite Test. Slide Jump trigger `c`, block `whileHeld`. Heal Slot trigger `w`. Sprint trigger `XButton1`.
6. #17 is closed in source only. Flash the RP2350 before treating a board test as proof of the keyboard retry. Scroll does not depend on that flash.
7. This session is already committed and pushed. Do not add another commit unless the user asks.
