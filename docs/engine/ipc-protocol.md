# IPC protocol

#42. The bytes on `\\.\pipe\MacroUI` between the shell and the native engine. The shell code that speaks them is #50. This file does not add an `IEngineLink`, a pipe client, or a runner.

The page bridge stays JSON. `ui/src/bridge.ts` does not change. #50 turns these frames into the page messages that bridge already understands.

Behaviour the port must match is in `docs/engine/plan.md` and `docs/engine/ahk-analysis.md`. Threads and the report image's place in a tick are in `docs/engine/architecture.md`. When this file names a byte, `docs/engine/ipc-vectors.txt` is the vector a later test compares.

## Pipe

The native engine is the server. One instance. One client. Message mode. Duplex. Out buffer and in buffer are each 1048576 bytes. Default timeout is 50 ms. Create flags stay `PIPE_ACCESS_DUPLEX` (`0x3`) and `PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_NOWAIT` (`0x7`).

One `WriteFile` is one pipe message. A native message is one frame. It has no trailing newline. The pipe message size is the frame size. The largest frame is 1048576 bytes, header included. A larger read drops the client.

A frame whose first byte is `{` (`0x7B`) is an AHK JSON message, not a native frame. The native engine answers that with `ack` `bad frame` and does not apply the payload. An old shell paired with the native engine is not a supported pair. The dual-engine shell is #50, and it classifies the first message before it writes.

## Frame

Little-endian. The header is 12 bytes. The payload length is the `length` field.

| Offset | Size | Field |
|---|---|---|
| 0 | 4 | Magic bytes `4D 55 49 31` (`MUI1`) |
| 4 | 2 | `version`, u16. This version is 1 |
| 6 | 2 | `type`, u16 |
| 8 | 4 | `length`, u32, payload bytes |
| 12 | `length` | Payload |

Magic is four bytes, so the first byte is `M` on the wire. A reader that loads those four bytes as a little-endian u32 gets `0x3149554D`. Message tables below use payload offsets. Add 12 for the offset in the pipe message.

`hello` with armed clear:

```
4D 55 49 31  01 00  01 00  01 00 00 00  00
magic        version type   length 1     armed
```

Decode:

1. A first byte of `0x7B` is AHK JSON. The shell classifier stops here and speaks JSON for the rest of the connection. The native engine has no JSON mode: it answers with `ack` `bad frame` and does not apply the payload.
2. Fewer than 12 bytes is a bad frame.
3. Magic other than `MUI1` is a bad frame.
4. `version` other than 1 is `bad version`.
5. The pipe message size must equal `12 + length`, and the whole frame must be at most 1048576 bytes. Anything else is a bad frame.
6. Parse the payload for `type`. A type this direction does not send is ignored. No ack.
7. The payload must be consumed exactly. A string whose byte count runs past the payload is a bad frame. Bytes left over after the fields are a bad frame.

A bad frame gets `ack` with `ok` 0, `which` 0, and error `bad frame`, then the engine keeps the client. `bad version` uses the same ack shape with error `bad version`, and the engine then drops the client. A read error other than 232 (`ERROR_NO_DATA`) drops the client with no ack. Unknown `type` values are ignored with no ack.

Writes: a `state` frame gets one try. Every other frame gets four tries when the write fails with 232. Any other write error drops the client. A failed `state` write is not a loss. The next signature change, or the next forced state, sends it again.

## Handshake

The engine writes `hello`, then a forced `state`, before it reads. Two pipe messages, in that order. That is the same order as AHK.

The shell reads the first pipe message before it writes.

| First bytes | Rest of this connection |
|---|---|
| `{` | AHK JSON. The shell keeps today's messages. It does not send frames. |
| `MUI1` and version 1 | Native frames in both directions. |
| Anything else, or a version other than 1 | The shell drops the client and shows an error on the page. It does not guess. |

Version lives in the header. `hello` is the first frame, so the shell sees the version before it sends `profile`, `output`, or `command`. There is no shell `hello`. A native `hello` or `state` means the engine is up, which is what the shell already treats as live.

After that classification the shell may read the forced `state` that follows `hello`. State does not classify the peer again.

## Strings

A string is a u16 byte count, then that many UTF-8 bytes. No NUL. No escape sequences. The count is the stored length.

The encoder's cap is 4096 bytes. A longer source is cut to 4096 on a UTF-8 boundary. The decoder rejects a count above 4096. An empty string is `00 00`.

Control bytes are legal in the frame. When #50 rebuilds page JSON, it escapes them the way `JSON.Quote` does: backslash, quote, CR, LF, and tab get the short escapes, and any other byte below `0x20` becomes `\u00XX`.

## Types

| Type | Value | Direction |
|---|---|---|
| `hello` | 1 | Engine to shell |
| `state` | 2 | Engine to shell |
| `ack` | 3 | Engine to shell |
| `error` | 4 | Engine to shell |
| `report` | 5 | Engine to shell |
| `profile` | 6 | Shell to engine |
| `output` | 7 | Shell to engine |
| `command` | 8 | Shell to engine |

Type 0 is ignored. A frame sent the wrong way is ignored. No ack.

## hello

Payload is one byte.

| Offset | Size | Field |
|---|---|---|
| 0 | 1 | `armed`. 0 or 1 |

The engine sends this on connect, then the forced `state`. `armed` is the engine's armed flag at that moment.

## state

Latest snapshot. The playback thread stores key ids. The IPC thread writes this frame and is the side that turns those ids into names. Playback does not format the text.

| Offset | Size | Field |
|---|---|---|
| 0 | 1 | `armed`. 0 or 1 |
| 1 | 1 | Reserved. Encoder writes 0. Decoder ignores it |
| 2 | 2 | `running_count`, u16 |
| 4 | 2 | `held_count`, u16 |
| 6 | 2 | `window_count`, u16 |
| 8 | | `active_id` string. The id `StateJson` shows. The internal `"-"` sentinel is not this field |
| | | `front` string. Foreground exe, or empty |
| | | `running_count` records. Each record is an `id` string, then a `name` string. Runners that are finished are omitted. Order is runner order |
| | | `held_count` strings. Applied key names, in applied order |
| | | `window_count` records. Each record is an `exe` string, then a `title` string |

Caps, after the walk below: 256 running, 64 held, 256 windows. The encoder drops extras. The decoder rejects a count above the cap.

Windows follow today's walk: distinct exe in `WinGetList` order, then the foreground exe appended when it was missing. If that list is longer than 256, the encoder drops the earliest rows until 256 remain, and if `front` is then missing it replaces the last row with the foreground exe and title.

`state-sample` in the vector file is armed, `active_id` `default`, `front` `Game.exe`, one running pair `sprint-jump` / `Sprint jump`, held `space`, one window `Game.exe` / `Game`.

### Names for key ids

`held` uses these names. The report frame uses the ids. Letter and digit ids are the lowercase letter or the digit, which is the self-test name for `b` extended across those virtual keys. Modifier and function names are the `BuildVkMap` overrides.

| Id | Name |
|---|---|
| `0x0D` | `enter` |
| `0x10`, `0xA0` | `LShift` |
| `0x11`, `0xA2` | `LCtrl` |
| `0x12`, `0xA4` | `LAlt` |
| `0x20` | `space` |
| `0x30`..`0x39` | `0`..`9` |
| `0x41`..`0x5A` | `a`..`z` |
| `0x5B` | `LWin` |
| `0x5C` | `RWin` |
| `0x70`..`0x7B` | `f1`..`f12` |
| `0xA1` | `RShift` |
| `0xA3` | `RCtrl` |
| `0xA5` | `RAlt` |
| 256 | `numpadenter` |
| 257 | `LButton` |
| 258 | `RButton` |
| 259 | `MButton` |
| 260 | `XButton1` |
| 261 | `XButton2` |

Any other id uses the name table #44 fills. Until that table has a name, the id is omitted from `held`. The shell does not invent a second spelling. The table is `engine_key_name` in `engine-native/src/core/hook.c`.

## ack

Command acks and profile acks carry no id. `docs/resident-engine.md` section D asked for an echoed id. The plan and the analysis leave it off. A save is a `reload`. The page does not match an ack to a save.

| Offset | Size | Field |
|---|---|---|
| 0 | 1 | `ok`. 0 or 1 |
| 1 | 1 | `which`. 0 none, 1 command, 2 profile |
| 2 | | `error` string. Empty when `ok` is 1 |

| Case | `ok` | `which` | `error` |
|---|---|---|---|
| Command accepted, including an unknown action | 1 | 1 | empty |
| Profile frame accepted | 1 | 2 | empty |
| Bad frame | 0 | 0 | `bad frame` |
| Bad version | 0 | 0 | `bad version` |

The IPC thread sends the ack when it accepts the frame, not when playback finishes the effect. The following `state` can arrive on a later tick. That is the split the three threads require. AHK applied the command and broadcast state before the ack returned, on one thread.

`quit` is the exception on order inside the IPC thread: write the ack, then set the quit flag, then wake playback. The release still runs on the playback thread and can trail the ack by the rest of the current wait.

A profile ack is sent when the type was `profile`, including when the document was not applied.

## error

The pipe error is the profile-file failure. Shell errors (`bad-shape`, `profile-write`, `no-board`, `board-mismatch`) are shell-to-page messages. They are not frames.

| Offset | Size | Field |
|---|---|---|
| 0 | 1 | `where` |
| 1 | 1 | `code` |
| 2 | | `detail` string |

| `where` | `code` | Page `where` | Page `code` | `detail` |
|---|---|---|---|---|
| 1 | 1 | `engine` | `profile-read` | `the saved profile could not be read` |

The encoder uses that detail string. #50 passes `detail` through. An unknown `where` becomes page `where` `unknown`. An unknown `code` becomes the decimal text of the byte. `where` 1 stays `engine`.

The engine sends this only to a connected client. A failed load before the client connects keeps the last good profile, or an empty one, and does not wait to deliver the error. A later `reload` that fails sends `error` and then the command `ack`.

## report

The playback image. 156 bytes, always. On the pipe the payload length is 156. In memory the image is those 156 bytes without the frame header.

One publish produces one image. Software turns that image into one `SendInput` inside the process and does not write a frame. A board mode writes one `report` frame when the image has a down, an up, a move, a wheel, or the absolute flag. An image of zeros with the absolute flag clear is not written.

The image is the publish result before the board transforms. Logical key ids, unscaled `dx` and `dy`, wheel in step sign (positive is down). `swapClicks` is not applied here. Software applies it while building `SendInput`. The shell applies it while mapping mouse bits.

| Offset | Size | Field |
|---|---|---|
| 0 | 1 | `down_count`, 0..32 |
| 1 | 1 | `up_count`, 0..32 |
| 2 | 2 | `flags`, u16 |
| 4 | 64 | `down[32]`, u16 key ids. The first `down_count` entries are live. The rest are 0 |
| 68 | 64 | `up[32]`, u16 key ids, same rule |
| 132 | 4 | `dx`, i32. Relative. Already clamped to -16000..16000. Ignored when absolute is set. The engine writes 0 then |
| 136 | 4 | `dy`, i32. Same clamp and the same absolute rule |
| 140 | 4 | `wheel`, i32. Step sign, positive down. May be nonzero when absolute is set |
| 144 | 4 | `ax`, i32. Used when absolute is set |
| 148 | 4 | `ay`, i32 |
| 152 | 4 | `dpi`, u32. Foreground dpi from the tick snapshot, else the system dpi, else 96. 0 means the shell uses 96 |

`flags` bit 0 is absolute. Bit 1 is report overflow. The decoder ignores other bits. The encoder writes them 0.

Key ids: 0 is empty and is skipped. 1..255 are the virtual key after side resolution. 256 is Numpad Enter. 257..261 are `LButton`, `RButton`, `MButton`, `XButton1`, `XButton2`. They are not virtual keys. Any other id is skipped by the shell.

A count above 32, or a payload length other than 156, is a bad frame.

More than 32 downs or 32 ups in one publish sets bit 1, writes the first 32 of each list in emit order, and leaves the rest applied. The playback thread also `StopAll`, so later images are the remaining ups and not new downs. An up that did not fit is not deleted. Emit order is the order the publish walk produced, not sorted order.

`report-relative` is space (`0x20`) down, `dx` 4, `dy` -3, `wheel` 1, dpi 96. `report-three-downs` is `LButton`, `w` (`0x57`), `space`, with `dx` 4 and `dy` -3. `report-absolute` is `LButton` down, absolute, `ax` 10, `ay` 20, dpi 120.

### Board path

The serial port stays in the shell. The engine does not open it.

#39 measured the board line at about 0.13 ms median (`docs/engine/benchmarks.md`). The profile acks on that pipe were about 394 to 591 ms, and that time is `JSON.Parse` plus the window list. Those two already run off the playback thread. The report frame removes the hid JSON parse from the board hop. The 0.13 ms line is the serial reply, and it remains whoever writes the line.

Opening the port in the engine would put the 1500 ms up-wait on playback, or it would add another queue on the IPC thread and move port discovery, the `?` identity check, and the Settings page out of the shell. Two processes would have to stay off the port. The measured line does not pay for that. #45 turns this image into one `SendInput` and into the `K` / `M` lines. The decision is `docs/engine/decision-board-path.md`. It does not open the port.

The shell keeps the lock rule from #13: a report that includes an up waits up to 1500 ms, a report with no up waits 40 ms, and a down that misses the wait is queued, cap 16, oldest dropped. It keeps the firmware `hid-busy` retry from #17. `SteerMouse` stays the absolute move.

For each live down id, then later each live up id, the shell maps the id:

- 257..261 are mouse bits 0..4, before `swapClicks`. `swapClicks` swaps `LButton` and `RButton` only.
- 256 is HID usage `0x58`.
- Any other live id uses the name table above and `HidMap`. A name `HidMap` does not know is skipped.

Write order stays `WriteHidLocked`:

1. Downs. A key is `K {usage} 1`. A button sets its bit.
2. Absolute: `SteerMouse` for `ax`, `ay`. `dx` and `dy` are not written. `wheel` is left for step 4.
3. Otherwise, when a button changed or `dx`, `dy`, or `wheel` is nonzero: one `M` line. `dx` and `dy` are scaled below. The wheel on that line is the negated wheel. Then `dx`, `dy`, and `wheel` are consumed.
4. Ups. A key is `K {usage} 0`. A button clears its bit.
5. If `wheel` is still nonzero, `M {bits} 0 0 {board_wheel}`.
6. If a button changed in step 4 and step 3 did not already send those bits, `M {bits} 0 0 0`.

Relative scale, half away from zero, the same rule as `docs/engine/architecture.md`:

```
if dpi == 0: dpi = 96
if dpi != 96:
    dx = half_away(dx * dpi / 96)
    dy = half_away(dy * dpi / 96)
board_wheel = -wheel

half_away(x) = floor(x + 0.5) when x >= 0, else ceil(x - 0.5)
```

Absolute points are not scaled by that formula. `SteerMouse` does its own conversion, as it does today.

## profile

Compatibility frame. On connect, after a native `hello`, the shell sends this frame with the file bytes, then `output`. That is today's `SendProfileEnvelope`, in this framing. A save writes the file and sends `command` `reload`. The save does not send `profile`.

The payload is the profile document itself, the same UTF-8 JSON the shell writes to disk. It is not wrapped in `{"type":"profile","profile":...}`.

| Offset | Size | Field |
|---|---|---|
| 0 | 4 | `json_bytes`, u32 |
| 4 | `json_bytes` | UTF-8 JSON |

A leading UTF-8 BOM (`EF BB BF`) is skipped by the parser, matching `JSON.Parse`. The document the shell writes has no BOM.

The IPC thread parses it. A JSON object becomes the next profile image. Playback adopts that image on a later tick. The ack does not wait for the adopt. A parse failure, or a JSON value that is not an object, applies nothing. The ack is still `ok` 1, `which` 2. `profile-not-object` in the vector file is the document `[]`.

`profile-empty` is the document `LoadDisk` writes when the file is missing:

```
{"version":1,"name":"Default","variables":{},"macros":[]}
```

The profile schema stays this JSON document. A second binary schema would drift from the file and from the editor. Ignored fields (`variables`, `priority`, profile `version`, profile `name`) stay in the document and stay ignored.

The command ring does not carry `profile`. A full ring does not block a profile publish.

## output

Payload is one byte. No ack.

| Value | Mode |
|---|---|
| 0 | `software` |
| 1 | `rp2040` |
| 2 | `rp2350` |
| any other | stored as `software` |

The page message that also carries `requested` is shell-to-page. The frame has the mode the engine should use, which is today's pipe `output`.

A change flushes applied keys, clears hook bindings, forgets the remembered mouse point, and rebinds. That effect is #45. This frame is only the byte.

`output` shares the command ring with `command`, so it stays in parse order with `start`, `stop`, and `reload`.

## command

Payload is one byte.

| Value | Action |
|---|---|
| 1 | `start`. Arm |
| 2 | `stop`. Disarm |
| 3 | `reload`. Read `--profile` from disk |
| 4 | `quit` |
| any other | No effect. The ack is still command `ok` |

`reload` is the save path. The path is the process `--profile` argument. The frame does not carry a path. The IPC thread reads the file. On success it publishes a new profile image. On failure it keeps the last image and sends `error` `profile-read`. Either way it then sends the command ack. Playback adopts a newer image on its next tick. Startup reads `--profile` the same way, on the IPC thread, before the first adopt.

`quit` does not take a ring slot. The quit flag is set even when the ring is full. The ack is the command ack, written before the flag is signaled.

`start`, `stop`, `reload`, and `output` share the ring of 64. When that ring is full the IPC thread sets the overflow latch, does not enqueue the new item, wakes playback, and still sends the command ack when the frame was a command. Playback calls `StopAll` and releases keys on the next tick. A missed key-up is worse than a stopped macro.

## What #50 sends the page

The page already parses these JSON objects. #50 builds them from frames. It does not hand the page a frame.

| Frame | Page JSON |
|---|---|
| `hello` | None. The shell marks the engine live |
| `state` | `{"v":1,"type":"state","armed":true\|false,"running":[{"id","name"}],"held":["..."],"activeId":"...","front":"...","windows":[{"exe","title"}]}` |
| `ack` | `{"v":1,"type":"ack","ok":true\|false}` plus `"for":"command"` or `"for":"profile"` when `which` is 1 or 2, plus `"error"` when the string is non-empty |
| `error` | `{"v":1,"type":"error","where":"...","code":"...","detail":"..."}` |
| `report` | None. The shell writes the board. The page does not see hid JSON today |

Shell to engine, from the page messages that already exist:

| Page message | Frame |
|---|---|
| `command` `start`, `stop`, `reload` | `command` with that action |
| `profile` save | The shell writes the file, then `command` `reload` |
| `input` | The shell opens or closes the port as it does today, then `output` with the mode it actually applied |
| Connect, after a native `hello` | `profile` with the file bytes, then `output` |
| Connect, after an AHK `{` | Today's JSON profile envelope, then today's JSON `output` |

`quit` on window close stays the shell's current close behaviour. This spec does not change it.

## Dropped

| Item | Reason |
|---|---|
| Page messages `ready`, `chrome`, `link`, `ports`, `window`, `shell`, `flushed`, `flush`, page `hello`, page `input`, page `output` | They cross WebView2, not this pipe |
| Ack `id`, command `id` | The plan leaves ids off acks. Section D of `docs/resident-engine.md` is not the pipe |
| JSON `v` and `type` | The header version and the type byte replace them |
| JSON `hid` | The `report` image replaces it. Scale, wheel sign, and `swapClicks` for the board run in the shell, on this image |
| A binary profile schema | The file is already the document. `reload` reads that file |
| The shell opening nothing and the engine opening the serial port | Decided above. The port stays in the shell |

## Vectors

`docs/engine/ipc-vectors.txt` is the byte list. A line that starts with `#` is a comment. A blank line is ignored. A record is a line `name direction`, then one line of uppercase hex bytes separated by single spaces.

`direction` is `engine`, `shell`, `json`, or `reject`. `engine` and `shell` are frames. `json` is the AHK hello a classifier must treat as JSON (`7B` first). `reject` is a pipe message a decoder must not accept as a frame.

| Name | What it locks |
|---|---|
| `hello-disarmed`, `hello-armed` | The worked example above, and armed 1 |
| `state-sample` | The state layout and the locked name `space` |
| `ack-command`, `ack-profile`, `ack-bad-frame`, `ack-bad-version` | Ack bytes, including no id |
| `error-profile-read` | The only pipe error |
| `report-relative`, `report-absolute`, `report-three-downs`, `report-up`, `report-overflow` | The 156-byte image |
| `profile-empty`, `profile-not-object` | The document, and a non-object that is still a valid frame |
| `output-software`, `output-rp2040`, `output-rp2350` | The mode byte |
| `command-start`, `command-stop`, `command-reload`, `command-quit`, `command-unknown` | The action byte. Unknown is a valid frame |
| `json-hello` | AHK `{"v":1,"type":"hello","armed":false}` plus the newline `SendRaw` appends |
| `reject-short`, `reject-trailing`, `reject-magic` | Length with no payload, an extra trailing byte, a bad magic |

The report image is `engine-native/src/ipc/report.c`. The other frames are still this file and `docs/engine/ipc-vectors.txt`. The pipe calls stay in `src/win`.
