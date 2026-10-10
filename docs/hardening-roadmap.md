# Hardening roadmap

Landed on `main` after merging `freja-test`. Do not redo an item unless its check fails.

0. `freja-test` is merged. One Press (`onceHeld`) and Repress Spam (`tapSpam`) stay, and the audit guards stay with them. Check: engine `--self-test` prints PASS, and a profile round trip still contains both block types.
1. The shell adopts a live engine (`Local\MacroUI.Engine`) instead of starting a second AutoHotkey. Check: start the engine, note its pid, open the shell, the pid is unchanged.
2. After a profile read, and after 8 s with no key events while armed, the engine reinstalls the low-level hook. A save sends `command/reload` so the engine reads the file instead of parsing the whole profile off the pipe.
3. Reload posts a debounced edit first. Tray Quit asks the page to flush, then waits up to 400 ms.
4. An empty focus does not swallow keys while `MacroShell.exe` (or its WebView host) is in front. A named game focus is unchanged.
5. A HID down that misses the board lock is queued and sent before the next report. An up still waits on the lock.
6. A non-state pipe write retries up to four times on error 232. State stays single-try.
7. No change. `WindowListJson` already skips `WinGetList` while a macro is running and a cache exists.
8. `ShowFromTray` centers the window when its bounds miss every monitor.
