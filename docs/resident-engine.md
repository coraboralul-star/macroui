# Resident engine

Clean-room notes from the local SteelSeries GG 120 install, mapped onto the current MacroUI split (`ui/`, `shell/`, `engine/`). This is not a source recovery. Nothing here should be copied from SteelSeries assets, certificates, device specs, or handler scripts.

## Evidence and limits

Observed on this machine, 5 Oct 2026:

- Install `C:\Program Files\SteelSeries\GG`, `version.json` says `120.0.0`. `SteelSeriesGGEZ.runtimeconfig.json` targets `net8.0` plus `Microsoft.AspNetCore.App`.
- Login start is `HKLM\...\Run\SteelSeriesGG` launching `SteelSeriesGGEZ.exe -dataPath=... -dbEnv=production -auto=true`. The update service `SteelSeriesGGUpdateServiceProxy` was installed and stopped. GG is a login process, not a running Windows service.
- Process tree while it was already running: `explorer.exe` -> `SteelSeriesGGEZ.exe` -> `SteelSeriesGG.exe -dbEnv=production -auto=true --hosted` and `apps\engine\SteelSeriesEngine.exe` -> `apps\engine\prism\SteelSeriesPrism.exe`.
- `coreProps.json` (both the GG and Engine 3 copies) named three loopback addresses. Owning listeners: `address` on `SteelSeriesGG.exe` (HTTP/1.1, `GET /` is 404, `GET /game_event` is 405), `encryptedAddress` on `SteelSeriesEngine.exe` (HTTP/1.0, bare `GET /` is 400), `ggEncryptedAddress` on `SteelSeriesGGEZ.exe` (plain HTTP got an empty reply). A second GGEZ port answered `Server: Kestrel` with 404. Prism listened on its own loopback port and also answered HTTP/1.0 400.
- A string scan of `SteelSeriesEngine.exe` found `go1.24.3`. That identifies the toolchain. It is not a disassembly of the scheduler.
- Managed type and method names below come from PE metadata (`System.Reflection.Metadata`) on `GG.*.dll`, `SteelSeriesGGEZ.dll`, and `SteelSeries.GG.DeviceModuleInterface.dll`. Those assemblies were not loaded and method bodies were not decoded.

Not established:

- REA could not produce an Evidence bundle. Native analysis selected Ghidra, which is unavailable on this Windows host, and Hopper is macOS-only. The managed CLI routed the same DLLs through that native open and failed. Treat the metadata names as a local inventory, not as REA Evidence IDs.
- Focus debounce interval, websocket frame bytes, and the Go macro/lighting scheduler were not recovered. Do not invent them.
- Logitech G HUB was only seen as a login command: `lghub_system_tray.exe --minimized`. That is a second resident-tray example, not a studied engine.

The public GameSense contract (SteelSeries gamesense-sdk) says games read `address` from `%ProgramData%\SteelSeries\SteelSeries Engine 3\coreProps.json` and `POST` JSON to `/game_event`, `/bind_game_event`, and related paths. The local 405 on `GET /game_event` matches that POST-only path. In this GG 120 layout that public port is owned by `SteelSeriesGG.exe`, with the Go engine on a different loopback port.

## Engine backend

`SteelSeriesEngine.exe` is `github.com/steelseries/engine`, built with Go 1.24.3 (build info in the binary). Top-level packages include `gamesense`, `game_events`, `loadouts`, `device`, `db`, `config`, `gg_events`, `prism`, and `utils`. Names below are symbol names. Method bodies were not decoded.

Its own listener is the `encryptedAddress` port. Plain `GET /` is HTTP/1.0 400. The same port over TLS answers HTTP/1.1 200 with `Server: web.go` and `{"error":{"message":"Nothing here"}}`. `GET /game_event` on that TLS port is 404 `Page not found`. The public GameSense path is the other process. A websocket upgrade on plain HTTP was rejected with the same 400. The engine does have a websocket fan-out of its own: `event_distribution` has `readMessages`, `writeMessages`, a pong handler, and `BroadcastToGroup`.

What the engine actually runs:

- **Active config follows the foreground app.** `application_detector` has `ApplicationChangeDetected`. `loadouts` has `InitializeLoadoutsDetection` and `handleAppChange`. `db` has `FindAllActiveConfigurations`, `GetActiveConfigSettingsForDevice`, and `LastActiveConfigurationFor`. `configuration` has `GetLastActiveConfiguration`, `ForceDeployLastActiveConfiguration`, and `callEventHookCallbacksActiveConfigChanged`. `GetForegroundWindow` is present in this binary. `gg_events` registers foreground-app change events toward GG.
- **A macro is a binding on that config, not a nested script.** `commandbindings/binding_execution` has `macroModel.StartRecording` / `StopRecording`, `RecordSingleKeyPress`, `RecordTriggerAndBindMacro`, `createKeyTriggerMacroBinding`, `getActiveConfigForDevice`, `recordMacroToggle`, `textMacro`, `cooldownTimer`, `tactileCooldownTimer`, `dispatchGameEvent`, `deployConfigs`, and `execLoop`. No symbol in this pass was named priority, exclusive, or queue.
- **Device playback is a native sequencer beside the Go process.** `SSEdevice.dll` exports `Sequencer_StartSequence`, `Sequencer_StopSequence`, and a `Syncer_` set: base layer, idle layer, trigger layer, `SetTriggerModes`, `Trigger`, and Prism lock/unlock. `HIDDLL.dll` is HID open/read/write. `SSEMouseTracker.dll` is initialize, callback, uninitialize. `InputLib.dll` (loaded with the GG host) is `SendCommand`, `SetCallback`, and `HandleUnivTrig`.
- **GameSense handlers are data in a per-game sandbox.** `game_events` has `GameEventHandler` and `GameSandbox`. `apps/engine/lisp/load-game-integration.lsp` loads shared handler libraries, then loads each game file into its own sandbox, then loads user config last. The shared dispatcher is an event object with a value, a repeat limit, and a forked update loop. Those files are copyrighted. Do not copy them. The latest engine SQL squash writes `loadout_presets` rows whose settings name button mappings, a second actuation map, rapid-tap pairs, release mode, and hall thresholds. That is onboard key behavior stored as config, not a software step list.

For MacroUI this confirms steps 1 and 3 in the plan: a resident engine, and the engine itself switches the active config when the foreground app changes. It does not confirm a priority queue. `execLoop` plus cooldowns is the closest execution model, and it is simpler than `Runner`. Keep MacroUI's blocks. Do not port golisp, loadout JSON, or the sequencer.

## A. Comparison

| | SteelSeries GG 120 | MacroUI today |
| --- | --- | --- |
| What stays up | `SteelSeriesGGEZ.exe` starts at login with `-auto=true` and hosts the other processes. `SteelSeriesEngine.exe` is a child, not the editor. | `H&le.exe` opens the WPF shell. The shell starts `engine/MacroEngine.ahk`. `OnClosed` sends `command/quit` and then kills the AHK process. |
| Editor | .NET host (Kestrel, SQLite, sub-app process manager) plus a browser frontend. Sub-app metadata has separate offline and online frontend addresses. | React editor in WebView2. `ui/src/bridge.ts` posts JSON to the shell. The shell forwards most of it down the pipe. |
| Runtime | Go engine (`go1.24.3`) in `apps/engine`, plus a Prism child. Input callbacks show up as `InputLib` events (`OnSSECommandBinding`, `OnKeyboardShortcut`, HID recorder), not as UI work. | One AHK thread. Low-level hooks, the QPC clock, playback, and the named pipe all share `Engine.Tick`. |
| Focus | `FocusWindowEventsService` raises `FocusWindowChanged` on `FocusThread`. `ForegroundAppDetectionService` debounces that and the host can emit `ForegroundAppChangeMessage`. | `Engine.FocusOk` checks the foreground exe on each tick and caches it for that tick. `SwallowTriggers` releases the trigger keys when the focus exe does not match. |
| Configs | Per-app activation is a host concern. Device modules also have onboard profile slots (`IDeviceModuleOnboardProfileInterfaceV1`). That slot format is out of scope. | The editor has `configs`, `activeId`, and per-config `focusExe` (`writeActive`, `switchConfig`). The engine never reads `configs`. It runs the flat `macros` array the UI last copied out. |
| IPC | Public GameSense HTTP is `SteelSeriesGG.exe`. The Go engine's own port is TLS (`Server: web.go`). It also fans events out over a websocket (`event_distribution`). The .NET host has a separate websocket list. | One message-mode pipe, `\\.\pipe\MacroUI`, one client. UI to shell is WebView2 `postMessage`. State is a snapshot (`armed`, `running`, `held`, `front`, `windows`) pushed when the signature changes. `hello` replays the last snapshot. |
| Playback | `binding_execution` records a key trigger and runs it from `execLoop`, with cooldowns and `textMacro`. Lighting is `SSEdevice.dll` sequences (base, idle, trigger). Game handlers are golisp in a per-game sandbox. No priority or exclusive symbol turned up. | `Runner` already models once, repeat, while-held, toggle, on-release, mute-while-held, short-press, and nested blocks. `exclusive` calls `StopAll`. `priority` is stored by the UI and ignored by the engine. `pending` only hands a runner to the next tick. |
| Footprint | Several processes, a full ASP.NET host, device-module plugins, and optional apps (sonar, moments, engineApps). | One hidden AHK process and the shell. `timeBeginPeriod(1)` is held only while a runner or pending work exists. Hooks are installed for bound triggers. |

MacroUI is already the right shape: editor, host, runtime. GG's extra processes exist because it also drives firmware, lighting, audio apps, and a store. Copy the lifetime and the focus rule, not the process farm.

## B. Target architecture

Keep the three folders.

- `engine/MacroEngine.ahk` is the resident runtime. It owns hooks, the clock, mute, and playback. It keeps running when the window closes. Pipe disconnect already only drops the client (`DropClient`); `quit` is what exits. Stop sending `quit` from a normal close.
- `shell/` is the host. It owns the window, the profile file, the board serial port, and the pipe client. Closing the window hides or exits the UI. It does not kill a live engine. On the next open it reconnects; `PumpPipe` already retries, and `TryLaunchEngine` already starts AHK only after the pipe is missing.
- `ui/` stays the editor. It does not decide which config is live while a game is in front. It edits `configs` and displays `state`.

The pipe JSON is the seam. A later runtime can replace the AHK file if it speaks the same messages. Do not add Kestrel, SignalR, or a Go service for that.

AHK limits, stated plainly:

- There is one script thread. A large `JSON.Parse` inside `PollPipe` sits on the same thread as the hooks. The existing wait path (`MsgWaitForMultipleObjects`, hook pump, QPC) is the right way to sleep. It cannot isolate a second input thread.
- One pipe instance, polled from the tick, is enough for one shell. It is a poor multi-client bus.
- `timeBeginPeriod` is process-wide while macros run. That is acceptable. Do not leave it on when idle; the engine already drops it.
- If playback ever needs a dedicated raw-input thread, or the pipe must accept several clients without stalling hooks, that is the point to replace `engine/MacroEngine.ahk`. The shell and the profile schema can stay.

## C. Next steps

1. **Leave the engine up when the window closes.** In `shell/MainWindow.xaml.cs` `OnClosed`, stop sending `command/quit` and stop calling `Kill` on a normal close. Add an explicit Quit action later if you want a real exit. Confirm a second launch reconnects to the existing `\\.\pipe\MacroUI` instead of starting a second AHK. Optional follow-up in the same change: a login shortcut that starts the engine without showing the window. Do not add a Windows service.

2. **Reload from disk instead of piping the whole profile.** The shell already writes `profiles/default.json`, and `Engine.Handle` already implements `command/reload` via `LoadDisk` on that same path. After a save, send `reload` (small) rather than `type: profile` with the full document. Keep accepting `type: profile` so old shells still work. This keeps the tick thread off a large parse on every edit. The 180 ms debounce in `ui/src/useProfile.ts` can stay.

3. **Let the engine pick the config from the foreground exe.** `switchConfig` already copies one config into `macros`. Move that choice into `Engine.Tick` when `ActiveExe` changes: match `configs[].focusExe` the same way `FocusMatch` matches today, otherwise keep the config with an empty `focusExe`, otherwise keep `activeId`. Swap the live macro set without a UI round-trip. Broadcast the chosen id (step 4). The editor keeps using `switchConfig` for manual picks.

4. **Honor `priority` next to `exclusive`.** In `StartRunner`, `exclusive` currently calls `StopAll`. Change that so a new exclusive runner stops runners with lower or equal `priority` only, and a lower-priority trigger does not cancel a higher one. `priority` is already on `Macro` in `ui/src/profile.ts`. No new field. `pending` stays a same-tick handoff; do not build a queue.

5. **Name the active config on the state snapshot.** Extend `StateJson` with `activeId` (and keep `front`). The shell already caches the last `state` and replays it on `hello`, so a reopened window shows the config the engine actually applied. Add an optional `id` on command messages and echo it on `ack` so a save can be matched to its reload. Old clients ignore unknown fields.

## D. Message and schema extensions

Backward compatible. Old engines ignore unknown fields. New engines still accept the current `profile` and `command` messages.

State, added field:

```json
{ "v": 1, "type": "state", "armed": true, "running": [], "held": [], "front": "Game.exe", "activeId": "default", "windows": [] }
```

Command, added optional id. `reload` is already implemented:

```json
{ "v": 1, "type": "command", "action": "reload", "id": "1" }
```

Ack, echo that id when present:

```json
{ "v": 1, "type": "ack", "for": "command", "id": "1", "ok": true }
```

Profile document, no new required fields. The engine starts reading `configs` and `activeId`, which the editor already writes. `focusExe` on a config means "use this config while that exe is in front." Empty `focusExe` means the fallback config. Per-macro `focusExe` stays a tighter gate inside the active config (`FocusOk` already checks the macro, then the profile).

Do not add a second transport. The pipe remains the only IPC.

## E. Do not copy

- Names, icons, sounds, shaders, marketing pages, and the GG / Engine / Prism product layout.
- GameSense handler JSON, lighting zones, and the lisp files under `apps/engine/lisp`.
- Certificates, sub-app secret metadata, analytics keys, and the encrypted loopback channel.
- Onboard profile slot bytes and device-module plugins (`IDeviceModuleInterfaceV1`).
- Their websocket type names, private routes, and the Kestrel plus SQLite plus sub-app process manager.
- A Go rewrite, or any new framework, unless step 5's seam is later replaced because AHK's single thread cannot meet a measured input stall.
