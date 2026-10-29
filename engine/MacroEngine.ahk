#Requires AutoHotkey v2.0
#SingleInstance Force
SendMode("Input")
CoordMode("Mouse", "Screen")
CoordMode("Pixel", "Screen")
SendLevel(1)
SetKeyDelay(-1, -1)
SetMouseDelay(-1)
SetDefaultMouseSpeed(0)
SetStoreCapsLockMode(false)
ListLines(false)
; A fast loop is normal here. This stops the "too many hotkeys" dialog.
A_HotkeyInterval := 0

#Include JSON.ahk
#Include TracePlay.ahk

; One process, no window. Macros advance from a high-resolution clock.
; SetTimer cannot keep a 5 ms wait at 5 ms on Windows.
; Pipe: \\.\pipe\MacroUI  (message-mode JSON, one object per write)

if (A_Args.Length && A_Args[1] = "--self-test")
    ExitApp(SelfTest())

if (A_Args.Length >= 2 && A_Args[1] = "--trace")
    ExitApp(TracePlay(A_Args[2], A_Args.Length >= 3 ? A_Args[3] : ""))

; The shell checks this before starting another copy. A second process would
; hit #SingleInstance Force, kill this one, and release every held key.
Engine.instanceMutex := DllCall("CreateMutexW", "Ptr", 0, "Int", 1, "Str", "Local\MacroUI.Engine", "Ptr")

A_IconHidden := true
ListLines(false)
KeyHistory(0)
InstallKeybdHook()
InstallPhysical()
OnExit(OnEngineExit)
Engine.LoadDisk()
Engine.OpenPipe()
Engine.Loop()

OnEngineExit(*) {
    if Engine.instanceMutex
        DllCall("CloseHandle", "Ptr", Engine.instanceMutex)
    try Engine.ReleaseAll()
    try {
        for key, _ in Engine.hooks {
            try Hotkey(HookSpec(key), "Off")
            try Hotkey(HookSpec(key) " up", "Off")
        }
    }
    if Engine.hKeyHook
        DllCall("UnhookWindowsHookEx", "Ptr", Engine.hKeyHook)
    if Engine.hMouseHook
        DllCall("UnhookWindowsHookEx", "Ptr", Engine.hMouseHook)
    if Engine.periodOn
        DllCall("Winmm.dll\timeEndPeriod", "UInt", 1)
    if Engine.hTimer
        DllCall("CloseHandle", "Ptr", Engine.hTimer)
    if Engine.hPipe
        DllCall("CloseHandle", "Ptr", Engine.hPipe)
}

Field(v, key, def := "") {
    if (v is Map && v.Has(key))
        return v[key]
    return def
}

AsNum(v, def) {
    if IsNumber(v)
        return v + 0
    ; Number("abc") throws. A bad field in one saved step must not raise out of
    ; Advance every tick, which would stall Publish and leave keys held.
    if (v is String && v != "") {
        try return Number(v)
        catch
            return def
    }
    return def
}

AsBool(v) {
    if (v = true || v = 1)
        return true
    s := StrLower(Trim(String(v)))
    return (s = "true" || s = "1")
}

SafeKey(name) {
    name := String(name)
    if (name = "|")
        name := "\"
    if RegExMatch(name, "^[A-Za-z0-9]+$")
        return name
    if (name = "-" || name = "=" || name = "[" || name = "]" || name = "\" || name = ";" || name = "'" || name = "," || name = "." || name = "/" || name = "``")
        return name
    return ""
}

; Send treats ` as an escape, so a backtick key has to be doubled in the string.
SendToken(key, dir) {
    if (key = "``")
        key := "````"
    return "{" key " " dir "}"
}

SendPiece(key, dir) {
    if IsMouseButton(key) {
        SendMouseButton(key, dir)
        return ""
    }
    return SendToken(key, dir)
}

; Our own mouse buttons carry this extra value so the physical hook can ignore them.
; A sent Mouse 4 must not look like the finger letting go, or the Z hold ends immediately.
SendMouseButton(key, dir) {
    flags := 0
    data := 0
    if (key = "LButton")
        flags := dir = "down" ? 0x0002 : 0x0004
    else if (key = "RButton")
        flags := dir = "down" ? 0x0008 : 0x0010
    else if (key = "MButton")
        flags := dir = "down" ? 0x0020 : 0x0040
    else if (key = "XButton1" || key = "XButton2") {
        flags := dir = "down" ? 0x0080 : 0x0100
        data := key = "XButton1" ? 1 : 2
    } else
        return
    size := A_PtrSize = 8 ? 40 : 28
    off := A_PtrSize = 8 ? 8 : 4
    inp := Buffer(size, 0)
    NumPut("Int", 0, inp, off)
    NumPut("Int", 0, inp, off + 4)
    NumPut("UInt", data, inp, off + 8)
    NumPut("UInt", flags, inp, off + 12)
    NumPut("UInt", 0, inp, off + 16)
    NumPut("Ptr", Engine.echo, inp, off + (A_PtrSize = 8 ? 24 : 20))
    DllCall("SendInput", "UInt", 1, "Ptr", inp, "Int", size)
}

SendName(key) {
    if (key = "LCtrl")
        return "LControl"
    if (key = "RCtrl")
        return "RControl"
    return key
}

; Software: $* swallows the trigger so the game only sees SendInput.
; Board: ~$* lets the Pico's keys through. The low-level hook still eats the
; real keyboard trigger, otherwise a Pico "b" is swallowed by the B macro.
; When a focus window is set and that window is not active, ~$* so typing works.
HookSpec(key) {
    key := SendName(key)
    prefix := (Engine.PassBoard() || !Engine.SwallowTriggers() || Engine.PassKey(key)) ? "~$*" : "$*"
    if (key = "``")
        return prefix "``"
    return prefix key
}

; Swallows the trigger key. The low-level hook records the real finger.
; This still records it too, so a release is seen even if that hook is behind.
TriggerHook(name) {
    ; Board keys look like a second keyboard. The LL hook owns them so a Pico
    ; tap of a trigger key is not counted as the finger going down again.
    if Engine.PassBoard()
        return
    raw := String(name)
    up := false
    if (StrLen(raw) >= 3 && SubStr(raw, -2) = "up") {
        before := SubStr(raw, -3, 1)
        if (before = " " || before = A_Tab) {
            up := true
            raw := Trim(SubStr(raw, 1, StrLen(raw) - 3))
        }
    }
    while (raw != "" && InStr("$*~", SubStr(raw, 1, 1)))
        raw := SubStr(raw, 2)
    if (raw = "")
        return
    raw := NormKey(raw)
    if (raw = "")
        return
    ; The mouse hook owns side buttons. The hotkey also fires for buttons we send back out.
    if (IsMouseButton(raw) && Engine.hMouseHook)
        return
    ; SendLevel can fire this after our own Send. That must not start another macro.
    if Engine.Echoing(raw, up)
        return
    ; The LL hook already recorded the real finger. Updating again here lets a
    ; late send hotkey overwrite a real release and leave the trigger stuck.
    if (Engine.rawOk && !IsMouseButton(raw))
        return
    Engine.NotePhysical(raw, !up)
}

InstallPhysical() {
    Engine.BuildVkMap()
    Engine.keyProc := CallbackCreate(PhysKeyProc, , 3)
    Engine.mouseProc := CallbackCreate(PhysMouseProc, , 3)
    module := DllCall("GetModuleHandle", "Ptr", 0, "Ptr")
    Engine.hKeyHook := DllCall("SetWindowsHookExW", "Int", 13, "Ptr", Engine.keyProc, "Ptr", module, "UInt", 0, "Ptr")
    Engine.rawOk := Engine.hKeyHook != 0
    Engine.hookTick := A_TickCount
    if !Engine.rawOk
        Engine.Log("physical hook failed")
}

PhysKeyProc(nCode, wParam, lParam) {
    block := false
    if (nCode >= 0 && Engine.rawOk) {
        Engine.hookTick := A_TickCount
        try block := Engine.ReadKey(wParam, lParam)
    }
    if block
        return 1
    return DllCall("CallNextHookEx", "Ptr", 0, "Int", nCode, "Ptr", wParam, "Ptr", lParam, "Ptr")
}

PhysMouseProc(nCode, wParam, lParam) {
    block := false
    ; Movement and the wheel are not triggers. Handling them here cuts the mouse rate.
    if (nCode >= 0 && Engine.hMouseHook && wParam != 0x200 && wParam != 0x20A && wParam != 0x20E) {
        Engine.mouseHookTick := A_TickCount
        try block := Engine.ReadMouse(wParam, lParam)
    }
    if block
        return 1
    return DllCall("CallNextHookEx", "Ptr", 0, "Int", nCode, "Ptr", wParam, "Ptr", lParam, "Ptr")
}

RawMouseMsg(wParam, lParam, msg, hwnd) {
    try Engine.ReadRaw(lParam)
}

; The MacroUI board, VID_CAFE&PID_4004.
RawNameIsBoard(name) {
    return InStr(name, "VID_CAFE&PID_4004") > 0
}

IsMouseButton(key) {
    return key = "LButton" || key = "RButton" || key = "MButton" || key = "XButton1" || key = "XButton2"
}

Alias(name) {
    static table := Map(
        "space", "space", "tab", "tab", "enter", "enter", "escape", "escape", "backspace", "backspace",
        "up", "up", "down", "down", "left", "left", "right", "right",
        "ins", "Insert", "del", "Delete",
        "shift", "LShift", "lshift", "LShift", "rshift", "RShift",
        "ctrl", "LCtrl", "control", "LCtrl", "lctrl", "LCtrl", "lcontrol", "LCtrl",
        "rctrl", "RCtrl", "rcontrol", "RCtrl",
        "alt", "LAlt", "lalt", "LAlt", "ralt", "RAlt",
        "lwin", "LWin", "rwin", "RWin",
        "|", "\"
    )
    name := String(name)
    if (name = "")
        return ""
    if table.Has(name)
        return table[name]
    low := StrLower(name)
    if table.Has(low)
        return table[low]
    if RegExMatch(name, "i)^F(\d+)$", &hit)
        return "f" hit[1]
    if (StrLen(name) = 1)
        return StrLower(name)
    return name
}

; One name everywhere. Old "shift" files become LShift. Left and right stay distinct.
NormKey(name) {
    key := SafeKey(Alias(String(name)))
    if (key = "")
        key := SafeKey(String(name))
    return key
}

; LL hook often reports the generic VK. Scan + extended bit pick left vs right.
SideKey(vk, scan, flags) {
    ext := flags & 0x01
    if (vk = 0xA0 || (vk = 0x10 && scan != 0x36))
        return "LShift"
    if (vk = 0xA1 || (vk = 0x10 && scan = 0x36))
        return "RShift"
    if (vk = 0xA2 || (vk = 0x11 && !ext))
        return "LCtrl"
    if (vk = 0xA3 || (vk = 0x11 && ext))
        return "RCtrl"
    if (vk = 0xA4 || (vk = 0x12 && !ext))
        return "LAlt"
    if (vk = 0xA5 || (vk = 0x12 && ext))
        return "RAlt"
    if (vk = 0x5B)
        return "LWin"
    if (vk = 0x5C)
        return "RWin"
    return ""
}

CompileBlocks(blocks) {
    steps := []
    if !(blocks is Array)
        return steps
    n := blocks.Length
    i := 1
    while (i <= n) {
        block := blocks[i]
        if !(block is Map) {
            i++
            continue
        }
        kind := String(Field(block, "type", ""))
        child := Field(block, "steps", [])
        if !(child is Array)
            child := []
        swapMs := NextSwapMs(blocks, i)
        if (kind = "whileHeld") {
            mute := Field(block, "mute", [])
            if !(mute is Array)
                mute := []
            steps.Push(Map("type", "holdLoop", "steps", child, "mute", mute, "releaseStop", BlockReleaseStop(block), "swapAfterMs", swapMs))
        }
        else if (kind = "ifShort")
            steps.Push(Map("type", "ifShort", "underMs", AsNum(Field(block, "underMs", 150), 150), "minCycles", AsNum(Field(block, "minCycles", 3), 3), "steps", child))
        else if (kind = "swapAfter") {
            mute := Field(block, "mute", [])
            if !(mute is Array)
                mute := []
            steps.Push(Map("type", "swapLoop", "afterMs", AsNum(Field(block, "afterMs", 150), 150), "steps", child, "mute", mute, "releaseStop", BlockReleaseStop(block), "swapAfterMs", swapMs))
        }
        else if (kind = "then")
            steps.Push(Map("type", "burst", "forMs", AsNum(Field(block, "forMs", 0), 0), "steps", child))
        else if (kind = "repeat")
            steps.Push(Map("type", "repeat", "count", AsNum(Field(block, "count", 1), 1), "steps", child, "releaseStop", BlockReleaseStop(block)))
        else if (kind = "wait")
            steps.Push(Map("type", "wait", "ms", AsNum(Field(block, "ms", 0), 0)))
        else if (kind = "steps")
            steps.Push(Map("type", "repeat", "count", 1, "steps", child, "releaseStop", BlockReleaseStop(block)))
        else if (kind = "onceHeld")
            steps.Push(Map("type", "onceHeld", "gapMs", AsNum(Field(block, "gapMs", 70), 70), "steps", child))
        else if (kind = "tapHold" || kind = "tapSpam") {
            watch := Field(block, "watch", [])
            if !(watch is Array)
                watch := []
            step := Map("type", "edgeHold", "key", NormKey(Field(block, "key", "z")), "watch", watch, "armMs", AsNum(Field(block, "armMs", 5), 5), "gapMs", AsNum(Field(block, "gapMs", 80), 80), "ignore", NormalizeIgnore(Field(block, "ignore", "off")), "ignoreMs", AsNum(Field(block, "ignoreMs", 0), 0), "pauseWatch", NormalizePause(Field(block, "pauseWatch", "off")), "spam", kind = "tapSpam", "holdMs", AsNum(Field(block, "holdMs", 18), 18), "restMs", AsNum(Field(block, "restMs", 18), 18))
            steps.Push(step)
        }
        else if (kind = "pressHold") {
            if (child is Array) {
                for s in child
                    if (s is Map)
                        steps.Push(s)
            }
            key := NormKey(Field(block, "key", ""))
            if (key != "" && (!(child is Array) || child.Length = 0)) {
                steps.Push(Map("type", "key", "action", "down", "key", key))
                steps.Push(Map("type", "wait", "ms", 18))
                steps.Push(Map("type", "key", "action", "up", "key", key))
            }
            steps.Push(Map("type", "scanWait", "ms", AsNum(Field(block, "holdMs", 200), 200)))
        }
        i++
    }
    return steps
}

NextSwapMs(blocks, i) {
    if (i >= blocks.Length)
        return 0
    nxt := blocks[i + 1]
    if !(nxt is Map)
        return 0
    if (String(Field(nxt, "type", "")) != "swapAfter")
        return 0
    return AsNum(Field(nxt, "afterMs", 150), 150)
}

HasHoldLoop(steps) {
    if !(steps is Array)
        return false
    for step in steps {
        if (step is Map) {
            kind := String(Field(step, "type", ""))
            ; Repress and spam loop themselves. One pass lets After release run once spam stops.
            if (kind = "holdLoop" || kind = "swapLoop" || kind = "onceHeld" || kind = "edgeHold")
                return true
        }
    }
    return false
}

NormalizeIgnore(value) {
    mode := String(value)
    if (mode = "macro" || mode = "both")
        return mode
    return "off"
}

NormalizePause(value) {
    if (value = true || value = 1)
        return "off"
    s := StrLower(Trim(String(value)))
    if (s = "block")
        return "block"
    return "off"
}

BlockReleaseStop(block) {
    s := String(Field(block, "releaseStop", ""))
    if (s = "finish" || s = "nextUp")
        return s
    return ""
}

class Runner {
    __New(macro, modeOverride := "") {
        this.macro := macro
        this.id := String(Field(macro, "id", ""))
        this.name := String(Field(macro, "name", "Macro"))
        this.speed := AsNum(Field(macro, "speed", 1), 1)
        if (this.speed < 0.05)
            this.speed := 0.05
        this.holds := Map()
        this.pulses := Map()
        this.echoDown := Map()
        this.stack := []
        this.waitUntil := 0
        this.scanWait := false
        this.done := false
        this.paused := false
        this.moveX := 0
        this.moveY := 0
        this.wheel := 0
        this.hasGoto := false
        this.gotoX := 0
        this.gotoY := 0
        this.didGoto := false
        ; The turn step being sent. turnOn is cleared when all of it is sent.
        this.turnOn := false
        this.turnX := 0
        this.turnTotal := 0
        this.turnSent := 0
        this.turnFrom := 0
        this.turnTo := 0
        this.releaseStop := String(Field(macro, "releaseStop", "nextUp"))
        if (this.releaseStop != "finish")
            this.releaseStop := "nextUp"
        this.stopping := false
        this.sawUp := false
        ; A nextUp release drained a hold loop. The trigger counts as up for the
        ; rest of this runner: a re-press belongs to the next one (held-press).
        this.letGo := false
        this.releasedNow := false
        ; Why it ended, for the runner stop line. The first reason wins.
        this.endWhy := ""
        mode := modeOverride != "" ? String(modeOverride) : String(Field(macro, "playMode", "once"))
        this.mode := mode
        this.trigger := ""
        trig := Field(macro, "trigger", Map())
        if (trig is Map)
            this.trigger := NormKey(Field(trig, "button", ""))
        this.cycles := 0
        this.heldMs := 0
        this.tookShort := false
        this.startedAt := -1
        repeats := 1
        if (mode = "repeat")
            repeats := Max(1, Floor(AsNum(Field(macro, "repeatCount", 1), 1)))
        else if (mode = "whileHeld" || mode = "toggle")
            repeats := 0
        blocks := Field(macro, "blocks", [])
        if (Field(macro, "advanced", false) && (blocks is Array) && blocks.Length > 0) {
            compiled := CompileBlocks(blocks)
            ; Hold loops loop themselves. Everything else follows playback:
            ; play once must not restart the whole graph.
            if HasHoldLoop(compiled)
                topRepeats := 1
            else
                topRepeats := repeats
            this.stack.Push({steps: compiled, index: 1, repeatsLeft: topRepeats})
        }
        else {
            steps := Field(macro, "steps", [])
            if !(steps is Array)
                steps := []
            this.stack.Push({steps: steps, index: 1, repeatsLeft: repeats})
        }
    }

    AskStop() {
        if this.done
            return
        this.stopping := true
        if (this.StopMode() = "nextUp" && this.sawUp)
            this.MarkDone()
    }

    NoteHoldRelease() {
        if (this.trigger = "" || this.TriggerDown())
            return
        if (this.StopMode() != "nextUp")
            return
        i := this.stack.Length
        while (i >= 1) {
            frame := this.stack[i]
            i--
            if !(frame is Object)
                continue
            if (frame.HasProp("holdLoop") && frame.holdLoop) {
                frame.drain := true
                this.letGo := true
            }
        }
    }

    ; The runner's own trigger. Up after letGo, whatever the finger does.
    TriggerDown() {
        return !this.letGo && Engine.KeyDown(this.trigger)
    }

    TriggerFinger() {
        return !this.letGo && Engine.FingerDown(this.trigger)
    }

    StopMode() {
        i := this.stack.Length
        while (i >= 1) {
            frame := this.stack[i]
            i--
            if !(frame is Object) || !frame.HasProp("releaseStop")
                continue
            mode := String(frame.releaseStop)
            if (mode = "finish" || mode = "nextUp")
                return mode
        }
        return this.releaseStop
    }

    MarkDone(why := "finished") {
        if this.done
            return
        this.done := true
        this.endWhy := why
        this.paused := false
        ; Stop means stop: the rest of a turn is dropped.
        if this.turnOn {
            this.turnOn := false
            Engine.Log(Format('turn stopped macro="{}" id={} sent={}/{} reason={}', this.name, this.id, this.turnSent, this.turnTotal, why))
        }
        frames := []
        if (this.stack is Array) {
            for frame in this.stack
                frames.Push(frame)
        }
        for frame in frames {
            this.ReleaseFrameMute(frame)
            this.ReleaseFramePause(frame)
        }
        this.holds := Map()
        this.pulses := Map()
        this.echoDown := Map()
        this.stack := []
    }

    ArmMute(frame) {
        ids := frame.mute
        if !(ids is Array) || (ids.Length = 0)
            return
        frame.muteOn := true
        Engine.MuteIds(ids)
    }

    ReleaseFrameMute(frame) {
        if !(frame is Object)
            return
        if !(frame.HasProp("muteOn") && frame.muteOn)
            return
        frame.muteOn := false
        Engine.UnmuteIds(frame.mute)
    }

    ArmPause(frame) {
        ids := frame.HasProp("pauseOnce") ? frame.pauseOnce : []
        if !(ids is Array) || (ids.Length = 0)
            return
        frame.pauseOn := true
        Engine.ArmOnceLock(ids)
    }

    ReleaseFramePause(frame) {
        if !(frame is Object)
            return
        if !(frame.HasProp("pauseOn") && frame.pauseOn)
            return
        frame.pauseOn := false
        Engine.ReleaseOnceLock(frame.pauseOnce)
    }

    Pause() {
        this.paused := true
        this.holds := Map()
        this.pulses := Map()
        this.echoDown := Map()
        this.moveX := 0
        this.moveY := 0
        ; This tick's turn share is not sent, so it is still owed.
        this.turnSent -= this.turnX
        this.turnX := 0
        this.wheel := 0
        this.hasGoto := false
    }

    Advance(now) {
        this.moveX := 0
        this.moveY := 0
        this.turnX := 0
        this.wheel := 0
        this.hasGoto := false
        this.pressedNow := Map()
        this.releasedNow := false
        if (this.done || this.paused)
            return
        this.NoteHoldRelease()
        for k, releaseAt in this.pulses.Clone()
            if (releaseAt <= now && this.pulses.Has(k))
                this.pulses.Delete(k)
        this.TurnTick(now)
        if (this.waitUntil != 0 && now < this.waitUntil) {
            if this.HoldShouldStop() {
                top := this.stack.Length ? this.stack[-1] : ""
                ; Spam yields on release so the next block, including After release, can run.
                if (top is Object && top.HasProp("edgeSpam") && top.edgeSpam)
                    this.waitUntil := 0
                else {
                    this.MarkDone()
                    return
                }
            } else
                return
        }
        this.waitUntil := 0
        this.scanWait := false
        if (this.stopping && this.StopMode() = "nextUp" && this.sawUp) {
            this.MarkDone()
            return
        }
        ; A turn over the per-report limit goes out over the next ticks.
        if this.turnOn
            return
        if (this.startedAt < 0)
            this.startedAt := now
        this.heldMs := now - this.startedAt
        loop 64 {
            if (this.done || this.stack.Length = 0) {
                this.MarkDone()
                return
            }
            frame := this.stack[-1]
            steps := frame.steps
            if !(steps is Array)
                steps := []
            if (frame.index > steps.Length) {
                if (frame.HasProp("edgeHold") && frame.edgeHold) {
                    if (frame.HasProp("edgeSpam") && frame.edgeSpam) {
                        if (this.TickSpamEdge(frame, now))
                            return
                        continue
                    }
                    this.TickEdge(frame, now)
                    return
                }
                if (frame.HasProp("onceHeld") && frame.onceHeld) {
                    if (this.TickOnce(frame, now))
                        return
                    continue
                }
                if (frame.HasProp("holdLoop") && frame.holdLoop) {
                    this.cycles++
                    this.heldMs := now - this.startedAt
                    if this.stopping {
                        this.ReleaseFrameMute(frame)
                        if this.done
                            return
                        this.stack.Pop()
                        continue
                    }
                    if (this.trigger != "" && this.TriggerDown()) {
                        swapAt := frame.HasProp("swapAfterMs") ? AsNum(frame.swapAfterMs, 0) : 0
                        if (swapAt > 0 && this.heldMs >= swapAt) {
                            this.ReleaseFrameMute(frame)
                            if this.done
                                return
                            this.stack.Pop()
                            continue
                        }
                        if (steps.Length = 0) {
                            this.ArmWait(now,1)
                            return
                        }
                        frame.index := 1
                        continue
                    }
                    this.ReleaseFrameMute(frame)
                    if this.done
                        return
                    this.stack.Pop()
                    continue
                }
                if (frame.HasProp("burstUntil") && now < frame.burstUntil) {
                    frame.index := 1
                    continue
                }
                if (steps.Length = 0) {
                    this.stack.Pop()
                    continue
                }
                if (frame.repeatsLeft = 0) {
                    if this.stopping {
                        this.MarkDone()
                        return
                    }
                    frame.index := 1
                    continue
                }
                frame.repeatsLeft--
                if (frame.repeatsLeft > 0) {
                    frame.index := 1
                    continue
                }
                this.stack.Pop()
                continue
            }
            step := steps[frame.index]
            frame.index++
            if (frame.HasProp("drain") && frame.drain && this.sawUp) {
                frame.index := steps.Length + 1
                continue
            }
            result := this.Exec(step, now)
            if (result = "again") {
                frame.index--
                return
            }
            if (result = "wait")
                return
        }
        if (this.waitUntil = 0)
            this.ArmWait(now,1)
    }

    Exec(step, now) {
        if !(step is Map)
            return ""
        kind := String(Field(step, "type", ""))
        ; A short-tap branch is exclusive of later Run-once / hold / wait
        ; blocks. After-release (burst) still runs after both paths.
        if (this.tookShort && this.stack.Length = 1 && kind != "burst" && kind != "ifShort")
            return ""
        if (kind = "randomWait") {
            this.scanWait := false
            ms := Engine.RandomWaitMs(Field(step, "minMs", 0), Field(step, "maxMs", 0)) / this.speed
            return this.ArmWait(now, ms)
        }
        if (kind = "wait" || kind = "scanWait") {
            if (kind = "scanWait") {
                if (this.trigger != "" && !this.TriggerFinger()) {
                    this.MarkDone()
                    return "wait"
                }
                this.scanWait := true
            } else
                this.scanWait := false
            ms := AsNum(Field(step, "ms", 0), 0) / this.speed
            return this.ArmWait(now,ms)
        }
        if (kind = "key" || kind = "mouse") {
            key := NormKey(kind = "key" ? Field(step, "key", "") : Field(step, "button", ""))
            if (key = "")
                return ""
            action := String(Field(step, "action", "tap"))
            if (action = "down") {
                top := this.stack.Length > 0 ? this.stack[-1] : ""
                if (top is Object && top.HasProp("drain") && top.drain)
                    return ""
                this.holds[key] := true
                this.pressedNow[key] := true
                this.sawUp := false
                Engine.lastSent[key] := now
                if this.pulses.Has(key)
                    this.pulses.Delete(key)
            } else if (action = "up") {
                just := this.pressedNow.Has(key)
                if this.holds.Has(key)
                    this.holds.Delete(key)
                if this.pulses.Has(key)
                    this.pulses.Delete(key)
                if this.pressedNow.Has(key)
                    this.pressedNow.Delete(key)
                this.sawUp := true
                this.releasedNow := true
                top := this.stack.Length > 0 ? this.stack[-1] : ""
                cut := (top is Object && top.HasProp("drain") && top.drain)
                ; A down and up in the same tick would never be sent. Hold it for one tick.
                if just {
                    this.pulses[key] := now + 1
                    if cut
                        top.index := top.steps.Length + 1
                    return this.ArmWait(now,1)
                }
                if (this.stopping && this.StopMode() = "nextUp") {
                    this.MarkDone()
                    return "wait"
                }
                if cut
                    top.index := top.steps.Length + 1
            } else {
                hold := AsNum(Field(step, "holdMs", 1), 1) / this.speed
                this.pulses[key] := now + Max(1, Round(hold))
                Engine.lastSent[key] := now
            }
            return ""
        }
        if (kind = "move") {
            x := this.Clamp(Round(AsNum(Field(step, "x", 0), 0)))
            y := this.Clamp(Round(AsNum(Field(step, "y", 0), 0)))
            this.moveX += x
            this.moveY += y
            return ""
        }
        if (kind = "scroll") {
            this.wheel += Round(AsNum(Field(step, "dy", 0), 0))
            return ""
        }
        if (kind = "turn")
            return this.StartTurn(step, now)
        if (kind = "goto") {
            tx := Round(AsNum(Field(step, "x", 0), 0))
            ty := Round(AsNum(Field(step, "y", 0), 0))
            ms := AsNum(Field(step, "ms", 15), 15) / this.speed
            if !this.didGoto {
                Engine.mouseAtOk := false
                this.didGoto := true
            }
            sx := 0
            sy := 0
            if !Engine.LocalToScreen(Engine.GotoWhere(Field(step, "where", "screen")), tx, ty, &sx, &sy)
                return ""
            dx := 0
            dy := 0
            Engine.CursorDelta(sx, sy, &dx, &dy)
            this.moveX += dx
            this.moveY += dy
            this.hasGoto := true
            this.gotoX := sx
            this.gotoY := sy
            Engine.RememberMouse(sx, sy)
            if (ms > 0)
                return this.ArmWait(now,ms)
            return ""
        }
        if (kind = "repeat") {
            count := Floor(AsNum(Field(step, "count", 1), 1))
            if (count < 0)
                count := 0
            child := Field(step, "steps", [])
            if !(child is Array)
                child := []
            this.stack.Push({steps: child, index: 1, repeatsLeft: count, releaseStop: String(Field(step, "releaseStop", ""))})
            return ""
        }
        if (kind = "edgeHold") {
            key := NormKey(Field(step, "key", ""))
            watch := Field(step, "watch", [])
            if !(watch is Array)
                watch := []
            prev := Map()
            for item in watch {
                name := NormKey(item)
                if (name != "")
                    prev[name] := false
            }
            mute := []
            pauseOnce := []
            pauseMode := NormalizePause(Field(step, "pauseWatch", "off"))
            if (pauseMode = "once" || pauseMode = "block") {
                ids := Engine.MacrosOnWatch(watch, this.id)
                if (pauseMode = "block")
                    mute := ids
                else
                    pauseOnce := ids
            }
            spam := Field(step, "spam", false)
            holdFor := Max(1, Round(AsNum(Field(step, "holdMs", 18), 18) / this.speed))
            restFor := Max(1, Round(AsNum(Field(step, "restMs", 18), 18) / this.speed))
            frame := {steps: [], index: 1, repeatsLeft: 1, edgeHold: true, edgeKey: key, edgePrev: prev, edgePhase: "down", edgeArm: AsNum(Field(step, "armMs", 5), 5), edgeGap: AsNum(Field(step, "gapMs", 80), 80), edgeIgnore: NormalizeIgnore(Field(step, "ignore", "off")), edgeIgnoreMs: AsNum(Field(step, "ignoreMs", 0), 0), edgeLock: Map(), edgeRepeatAt: now + Engine.KeyDelayMs(), edgeSpam: spam ? true : false, edgeHoldFor: holdFor, edgeRestFor: restFor, edgeSpamOn: true, edgeSpamAt: spam ? now + holdFor : 0, mute: mute, muteOn: false, pauseOnce: pauseOnce, pauseOn: false}
            this.stack.Push(frame)
            this.ArmMute(frame)
            this.ArmPause(frame)
            if (key != "")
                this.holds[key] := true
            if (this.trigger != "" && this.trigger != key)
                this.holds[this.trigger] := true
            this.ArmWait(now,5)
            return "wait"
        }
        if (kind = "onceHeld") {
            if this.stopping
                return ""
            child := Field(step, "steps", [])
            if !(child is Array)
                child := []
            gap := AsNum(Field(step, "gapMs", 70), 70) / this.speed
            if (gap < 0)
                gap := 0
            this.stack.Push({steps: child, index: 1, repeatsLeft: 1, onceHeld: true, oncePhase: "run", onceFirst: true, onceGap: gap, onceReady: 0})
            return ""
        }
        if (kind = "holdLoop") {
            if this.stopping
                return ""
            if (this.trigger != "" && Engine.physDown.Has(this.trigger) && !this.TriggerDown())
                return ""
            this.PushHoldFrame(step)
            return ""
        }
        if (kind = "swapLoop") {
            if this.stopping
                return ""
            if (this.trigger != "" && Engine.physDown.Has(this.trigger) && !this.TriggerDown())
                return ""
            after := AsNum(Field(step, "afterMs", 150), 150)
            still := this.trigger != "" && this.TriggerDown()
            if !still
                return ""
            if (this.heldMs < after) {
                this.ArmWait(now,1)
                return "again"
            }
            this.PushHoldFrame(step)
            return ""
        }
        if (kind = "ifShort") {
            under := AsNum(Field(step, "underMs", 150), 150)
            still := this.trigger != "" && this.TriggerDown()
            if (still && this.heldMs < under) {
                this.ArmWait(now,1)
                return "again"
            }
            if (this.heldMs >= under)
                return ""
            this.tookShort := true
            left := Floor(AsNum(Field(step, "minCycles", 0), 0)) - this.cycles
            if (left < 1)
                return ""
            child := Field(step, "steps", [])
            if !(child is Array)
                child := []
            this.stack.Push({steps: child, index: 1, repeatsLeft: left})
            return ""
        }
        if (kind = "burst") {
            ; After release runs only once the trigger is up. A down with no
            ; up in the block before this stays held until this block's up.
            if (this.trigger != "" && this.TriggerDown()) {
                this.ArmWait(now, 1)
                return "again"
            }
            child := Field(step, "steps", [])
            if !(child is Array)
                child := []
            forMs := AsNum(Field(step, "forMs", 0), 0) / this.speed
            if (forMs <= 0)
                this.stack.Push({steps: child, index: 1, repeatsLeft: 1})
            else
                this.stack.Push({steps: child, index: 1, repeatsLeft: 1, burstUntil: now + forMs})
            return ""
        }
        if (kind = "run") {
            target := Engine.FindMacro(String(Field(step, "macroId", "")))
            if (target is Map)
                Engine.StartRunner(target, "once", false, "run")
            return ""
        }
        return ""
    }

    ; true means this tick should stop. false means Advance should keep going.
    TickOnce(frame, now) {
        phase := String(frame.oncePhase)
        up := this.stopping || this.trigger = "" || !this.TriggerDown()
        if (phase = "gap") {
            if (now < frame.onceReady) {
                this.ArmWait(now, frame.onceReady - now)
                return true
            }
            this.stack.Pop()
            return false
        }
        if (phase = "decide") {
            if (up) {
                frame.oncePhase := "gap"
                if (now < frame.onceReady) {
                    this.ArmWait(now, frame.onceReady - now)
                    return true
                }
                this.stack.Pop()
                return false
            }
            if (now >= frame.onceReady) {
                frame.oncePhase := "run"
                frame.onceFirst := false
                frame.index := 1
                if (frame.steps.Length = 0) {
                    this.ArmWait(now, 1)
                    return true
                }
                return false
            }
            this.ArmWait(now, 1)
            return true
        }
        this.cycles++
        gap := AsNum(frame.onceGap, 0)
        frame.onceReady := now + gap
        if (frame.onceFirst) {
            frame.onceFirst := false
            if (up) {
                frame.oncePhase := "gap"
                if (gap <= 0) {
                    this.stack.Pop()
                    return false
                }
                this.ArmWait(now, gap)
                return true
            }
            if (gap <= 0) {
                frame.oncePhase := "run"
                frame.index := 1
                if (frame.steps.Length = 0 || this.releasedNow) {
                    this.ArmWait(now, 1)
                    return true
                }
                return false
            }
            frame.oncePhase := "decide"
            this.ArmWait(now, 1)
            return true
        }
        if (up) {
            frame.oncePhase := "gap"
            if (gap <= 0) {
                this.stack.Pop()
                return false
            }
            this.ArmWait(now, gap)
            return true
        }
        frame.oncePhase := "run"
        frame.index := 1
        if (frame.steps.Length = 0) {
            this.ArmWait(now, 1)
            return true
        }
        ; An up in this same tick must be sent before the next down.
        if this.releasedNow {
            this.ArmWait(now, 1)
            return true
        }
        return false
    }

    PushHoldFrame(step) {
        child := Field(step, "steps", [])
        if !(child is Array)
            child := []
        mute := Field(step, "mute", [])
        if !(mute is Array)
            mute := []
        clean := []
        for item in mute {
            name := String(item)
            if (name != "" && name != this.id)
                clean.Push(name)
        }
        frame := {steps: child, index: 1, repeatsLeft: 1, holdLoop: true, mute: clean, muteOn: false, releaseStop: String(Field(step, "releaseStop", "")), swapAfterMs: AsNum(Field(step, "swapAfterMs", 0), 0)}
        this.stack.Push(frame)
        this.ArmMute(frame)
    }

    ArmWait(now, ms) {
        this.waitUntil := now + Max(1, Round(ms))
        return "wait"
    }

    ; Counts are degrees times the live config's countsPer360 over 360, right
    ; positive. Uncalibrated sends nothing and still waits ms. 0 ms sends it
    ; all at once. The next step waits until all of it is sent.
    StartTurn(step, now) {
        ; A turn cut off by a skipped wait gets its rest now.
        if this.turnOn {
            this.turnX += this.turnTotal - this.turnSent
            this.turnOn := false
        }
        degrees := AsNum(Field(step, "degrees", 180), 180)
        if !(degrees > 0)
            degrees := 0
        if (degrees > 3600)
            degrees := 3600
        ms := Round(AsNum(Field(step, "ms", 0), 0))
        if (ms < 0)
            ms := 0
        per360 := Engine.TurnPer360()
        if (per360 <= 0) {
            Engine.Log(Format('turn skipped macro="{}" id={} reason=uncalibrated', this.name, this.id))
        } else {
            counts := Min(Floor(degrees * per360 / 360 + 0.5), 1000000000)
            if (String(Field(step, "dir", "right")) = "left")
                counts := -counts
            if (counts != 0) {
                this.turnOn := true
                this.turnTotal := counts
                this.turnSent := 0
                this.turnFrom := now
                this.turnTo := ms > 0 ? now + Max(1, Round(ms / this.speed)) : now
                this.TurnTick(now)
            }
        }
        if (ms > 0)
            return this.ArmWait(now, ms / this.speed)
        return this.turnOn ? "wait" : ""
    }

    ; Sends the share of the turn due by now. Truncation keeps it short of the
    ; end until the last tick, which sends the rest.
    TurnTick(now) {
        if !this.turnOn
            return
        if (now >= this.turnTo || this.turnTo <= this.turnFrom)
            want := this.turnTotal
        else
            want := Integer(this.turnTotal * (now - this.turnFrom) / (this.turnTo - this.turnFrom))
        delta := this.Clamp(want - this.turnSent)
        this.turnX += delta
        this.turnSent += delta
        if (this.turnSent = this.turnTotal)
            this.turnOn := false
    }

    DropHold(key) {
        if (key = "")
            return
        if this.holds.Has(key)
            this.holds.Delete(key)
        if this.pulses.Has(key)
            this.pulses.Delete(key)
    }

    HoldShouldSwap(now) {
        if this.stack.Length = 0
            return false
        frame := this.stack[-1]
        if !(frame is Object)
            return false
        if !(frame.HasProp("holdLoop") && frame.holdLoop)
            return false
        swapAt := frame.HasProp("swapAfterMs") ? AsNum(frame.swapAfterMs, 0) : 0
        if (swapAt <= 0)
            return false
        if (this.startedAt < 0)
            return false
        this.heldMs := now - this.startedAt
        return this.trigger != "" && this.TriggerDown() && this.heldMs >= swapAt
    }

    HoldShouldStop() {
        if this.stack.Length = 0
            return false
        frame := this.stack[-1]
        if !(frame is Object)
            return false
        watching := (frame.HasProp("edgeHold") && frame.edgeHold)
        if (this.HasProp("scanWait") && this.scanWait) {
            if this.stopping
                return true
            return this.trigger != "" && !this.TriggerFinger()
        }
        if !watching
            return false
        if this.stopping
            return true
        return this.trigger != "" && !this.TriggerDown()
    }

    TickEdge(frame, now) {
        if this.stopping {
            this.MarkDone()
            return
        }
        if (this.trigger != "" && !this.TriggerDown()) {
            this.MarkDone()
            return
        }
        key := frame.edgeKey
        if (frame.edgePhase = "arm") {
            this.DropHold(key)
            if (this.trigger != key)
                this.DropHold(this.trigger)
            frame.edgePhase := "gap"
            this.ArmWait(now,frame.edgeGap / this.speed)
            return
        }
        if (frame.edgePhase = "gap") {
            if (this.trigger != "" && !this.TriggerDown()) {
                this.MarkDone()
                return
            }
            if (key != "")
                this.holds[key] := true
            frame.edgePhase := "down"
            frame.edgeRepeatAt := now + Engine.KeyDelayMs()
        }
        pulsed := false
        for name, wasDown in frame.edgePrev {
            isDown := Engine.FingerDown(name)
            if (isDown && !wasDown && !pulsed && this.AcceptWatch(frame, name, now)) {
                frame.edgePhase := "arm"
                pulsed := true
            }
            frame.edgePrev[name] := isDown
        }
        if (frame.edgePhase = "arm") {
            this.ArmWait(now,frame.edgeArm / this.speed)
            return
        }
        this.RepeatHold(frame, now)
        this.ArmWait(now,5)
    }

    ; true means this tick should stop. false means the spam frame ended and Advance should continue.
    TickSpamEdge(frame, now) {
        key := frame.edgeKey
        up := this.trigger != "" && !this.TriggerDown()
        if (this.stopping) {
            this.MarkDone()
            return true
        }
        if (up) {
            this.DropHold(key)
            if (this.trigger != "" && this.trigger != key)
                this.DropHold(this.trigger)
            this.ReleaseFrameMute(frame)
            this.ReleaseFramePause(frame)
            this.stack.Pop()
            return false
        }
        if (frame.edgePhase = "quiet") {
            frame.edgePhase := "down"
            frame.edgeSpamOn := false
            frame.edgeSpamAt := now
        }
        pulsed := false
        for name, wasDown in frame.edgePrev {
            isDown := Engine.FingerDown(name)
            if (isDown && !wasDown && !pulsed && this.AcceptWatch(frame, name, now)) {
                frame.edgePhase := "quiet"
                pulsed := true
            }
            frame.edgePrev[name] := isDown
        }
        if (frame.edgePhase = "quiet") {
            this.DropHold(key)
            this.ArmWait(now, frame.edgeGap / this.speed)
            return true
        }
        this.TickSpam(frame, now)
        this.ArmWait(now, 5)
        return true
    }

    TickSpam(frame, now) {
        key := frame.edgeKey
        if (key = "" || !frame.HasProp("edgeSpamAt") || frame.edgeSpamAt = 0 || now < frame.edgeSpamAt)
            return
        if (frame.edgeSpamOn) {
            this.DropHold(key)
            frame.edgeSpamOn := false
            frame.edgeSpamAt := now + frame.edgeRestFor
        }
        else {
            this.holds[key] := true
            frame.edgeSpamOn := true
            frame.edgeSpamAt := now + frame.edgeHoldFor
        }
    }

    AcceptWatch(frame, name, now) {
        mode := frame.HasProp("edgeIgnore") ? String(frame.edgeIgnore) : "off"
        ms := frame.HasProp("edgeIgnoreMs") ? AsNum(frame.edgeIgnoreMs, 0) : 0
        if !frame.HasProp("edgeLock")
            frame.edgeLock := Map()
        last := frame.edgeLock.Has(name) ? frame.edgeLock[name] : -100000
        if (mode != "off" && ms > 0 && now - last < ms) {
            if (mode = "both")
                return false
            if (mode = "macro" && (Engine.SentDown(name) || Engine.SentSince(name, last)))
                return false
        }
        frame.edgeLock[name] := now
        return true
    }

    RepeatHold(frame, now) {
        key := frame.edgeKey
        if (key = "" || IsMouseButton(key) || !this.holds.Has(key))
            return
        if (!frame.HasProp("edgeRepeatAt") || frame.edgeRepeatAt = 0)
            frame.edgeRepeatAt := now + Engine.KeyDelayMs()
        if (now < frame.edgeRepeatAt)
            return
        this.echoDown[key] := true
        frame.edgeRepeatAt := now + Engine.KeyRepeatMs()
    }

    Clamp(n) {
        if (n > 16000)
            return 16000
        if (n < -16000)
            return -16000
        return n
    }
}

class Engine {
    static runners := []
    static muted := Map()
    static onceLock := Map()
    static oncePass := Map()
    static passKeys := Map()
    static sendNow := Map()
    static pending := []
    static applied := Map()
    static prevDown := Map()
    ; The trigger each prevDown entry was seeded for. A reload keeps prev for
    ; an unchanged trigger, so a press that lands with it is still an edge.
    static prevButton := Map()
    ; A whileHeld press refused while the last runner was finishing. Starts
    ; as held-press once that runner is gone, if the key is still down.
    static owed := Map()
    static liveName := ""
    static profile := Map()
    static liveId := ""
    static outputMode := "software"
    static hidEcho := []
    static mouseAtOk := false
    static mouseAtX := 0
    static mouseAtY := 0
    static winJson := "[]"
    static winAt := -100000
    static armed := false
    static dry := false
    static finger := Map()
    static sent := []
    static lastSent := Map()
    static busy := false
    static panicWas := false
    static hPipe := 0
    static pipeOn := false
    static pipeRetryAt := 0
    static lastState := ""
    static readBuf := Buffer(1048576)
    static profilePath := ""
    static hooks := Map()
    static physDown := Map()
    ; Set by the LL hooks, logged by PollTriggers. A hook callback must not write a file.
    ; repeatSeen: a down while the finger was already down. lastSwallow: the hook's answer to the down.
    static repeatSeen := Map()
    static repeatLogged := Map()
    static lastSwallow := Map()
    ; Raw Input's count of ups for middle and side buttons from a real mouse,
    ; not the board. A swallowed button still reaches Raw Input. rawAtDown is
    ; the count at the last down. redo is a lost release, pressed again after
    ; one poll. lostSeen is for the log.
    static rawGui := 0
    static rawOn := false
    ; Raw Input for the keyboard, while armed. rawKeyTick is the last key a
    ; real keyboard sent, for the silent hook line.
    static rawKeysOn := false
    static rawKeyTick := 0
    static rawState := "off"
    static rawUpCount := Map()
    static rawAtDown := Map()
    static rawBoard := Map()
    static redo := Map()
    static lostSeen := Map()
    static echo := 0x56454E44
    static vkName := Map()
    static rawOk := false
    static keyProc := 0
    static mouseProc := 0
    static hKeyHook := 0
    static hMouseHook := 0
    static hookTick := 0
    static mouseHookTick := 0
    static hookSuspect := false
    static instanceMutex := 0
    static qpcFreq := 0
    static qpcOrigin := 0
    static hTimer := 0
    static swallowKnown := false
    static swallowOn := true
    static frontExe := ""
    static frontKnown := false
    static focusCache := Map()
    static hooksDirty := true
    static hookFold := Map()
    static lastSig := ""
    static periodOn := false
    static delayMs := -1
    static repeatMs := -1
    static macroTapHold := Map()
    static macroHoldRel := Map()

    static Loop() {
        while true {
            this.busy := true
            try this.Tick()
            catch as err
                this.Log(err.Message)
            this.busy := false
            this.Pace()
        }
    }

    static NotePhysical(name, down) {
        name := NormKey(name)
        if (name = "")
            return
        this.physDown[name] := down
        folded := StrLower(name)
        if (folded != name)
            this.physDown[folded] := down
    }

    static BuildVkMap() {
        this.vkName := Map()
        loop 255 {
            vk := A_Index
            label := ""
            try label := GetKeyName(Format("vk{:X}", vk))
            button := NormKey(label)
            if (button != "")
                this.vkName[vk] := button
        }
        for vk, button in Map(
            0x10, "LShift", 0xA0, "LShift", 0xA1, "RShift",
            0x11, "LCtrl", 0xA2, "LCtrl", 0xA3, "RCtrl",
            0x12, "LAlt", 0xA4, "LAlt", 0xA5, "RAlt",
            0x5B, "LWin", 0x5C, "RWin",
            0x20, "space", 0x0D, "enter")
            this.vkName[vk] := button
    }

    ; Called after hotkeys are installed so this hook sees the finger first.
    ; Otherwise the macro hook can swallow the real key-up and the tap stays stuck.
    static RaisePhysicalHook() {
        if !this.keyProc
            return
        if this.hKeyHook
            DllCall("UnhookWindowsHookEx", "Ptr", this.hKeyHook)
        module := DllCall("GetModuleHandle", "Ptr", 0, "Ptr")
        this.hKeyHook := DllCall("SetWindowsHookExW", "Int", 13, "Ptr", this.keyProc, "Ptr", module, "UInt", 0, "Ptr")
        this.rawOk := this.hKeyHook != 0
        this.hookTick := A_TickCount
    }

    ; Windows removes a low-level hook when this thread stalls, and leaves the
    ; handle looking valid. rawOk would stay true while keys stop arriving.
    ; The mouse hook dies the same way, so a side button stays silent until restart.
    static CheckPhysicalHook() {
        if !this.keyProc
            return
        if !this.hKeyHook {
            this.Log("physical hook missing")
            this.RaisePhysicalHook()
            this.RaiseMouseHook()
            return
        }
        ; A profile parse just ran on this thread. Put both hooks back now, in
        ; case that stall got them removed, instead of waiting out the silence.
        if this.hookSuspect {
            this.hookSuspect := false
            this.RaisePhysicalHook()
            this.RaiseMouseHook()
            return
        }
        if (!this.armed && this.runners.Length = 0 && this.pending.Length = 0)
            return
        now := A_TickCount
        if (this.hookTick = 0)
            this.hookTick := now
        if (this.mouseHookTick = 0)
            this.mouseHookTick := now
        needMouse := this.MouseHookNeeded()
        if (needMouse && !this.hMouseHook)
            this.RaiseMouseHook()
        if this.NeedsKeyRaise(this.hookTick, this.mouseHookTick, needMouse, true, now) {
            this.Log(this.SilenceLine(now))
            this.RaisePhysicalHook()
        }
        if this.NeedsMouseRaise(this.mouseHookTick, needMouse, true, now) {
            this.Log("mouse hook silent")
            this.RaiseMouseHook()
        }
    }

    ; The native line. Raw Input still sees keys that a hook Windows removed
    ; misses, so a recent raw key means the hook was dropped.
    static SilenceLine(now) {
        rawAge := (this.rawKeysOn && this.rawKeyTick > 0) ? (now - this.rawKeyTick) "ms" : "-"
        mouseAge := this.mouseHookTick > 0 ? (now - this.mouseHookTick) "ms" : "-"
        return Format('hook key reinstall reason=silent config="{}" id={} age={}ms raw={} raw_key_age={} mouse_age={} verdict={}',
            this.liveName != "" ? this.liveName : "-", (this.liveId != "" && this.liveId != Chr(1)) ? this.liveId : "-",
            now - this.hookTick, this.rawKeysOn ? "on" : "off", rawAge, mouseAge,
            SilenceVerdict(this.rawKeysOn, this.hookTick, this.rawKeyTick))
    }

    ; Mouse clicks keep this thread alive. An idle keyboard hook is still installed.
    static NeedsKeyRaise(keyTick, mouseTick, needMouse, armed, now) {
        if !armed
            return false
        if (now - keyTick < 8000)
            return false
        if (needMouse && mouseTick != 0 && now - mouseTick < 8000)
            return false
        return true
    }

    static NeedsMouseRaise(mouseTick, needMouse, armed, now) {
        if (!armed || !needMouse)
            return false
        return (now - mouseTick) >= 8000
    }

    static MouseHookNeeded() {
        for key, _ in this.hooks
            if IsMouseButton(key)
                return true
        return false
    }

    ; Unhook first. A handle Windows already removed still looks valid, and
    ; installing a second time would leave the dead one in place.
    static RaiseMouseHook() {
        if this.hMouseHook {
            DllCall("UnhookWindowsHookEx", "Ptr", this.hMouseHook)
            this.hMouseHook := 0
        }
        if (!this.MouseHookNeeded() || !this.mouseProc)
            return
        module := DllCall("GetModuleHandle", "Ptr", 0, "Ptr")
        this.hMouseHook := DllCall("SetWindowsHookExW", "Int", 14, "Ptr", this.mouseProc, "Ptr", module, "UInt", 0, "Ptr")
        this.mouseHookTick := A_TickCount
    }

    ; Raw Input only while a middle or side button is a trigger, like the mouse hook.
    static SyncRawMouse(want) {
        if (want = this.rawOn)
            return
        dev := Buffer(8 + A_PtrSize, 0)
        NumPut("UShort", 1, dev, 0)
        NumPut("UShort", 2, dev, 2)
        if !want {
            NumPut("UInt", 0x1, dev, 4)
            DllCall("RegisterRawInputDevices", "Ptr", dev, "UInt", 1, "UInt", dev.Size)
            this.rawOn := false
            this.SetRawState("off")
            return
        }
        if !this.rawGui {
            try {
                this.rawGui := Gui()
                OnMessage(0x00FF, RawMouseMsg)
            }
        }
        ok := false
        if this.rawGui {
            NumPut("UInt", 0x100, dev, 4)
            NumPut("Ptr", this.rawGui.Hwnd, dev, 8)
            ok := DllCall("RegisterRawInputDevices", "Ptr", dev, "UInt", 1, "UInt", dev.Size)
        }
        this.rawOn := !!ok
        this.SetRawState(ok ? "on" : "unavailable")
    }

    ; The keyboard is its own registration, while armed. Only the time of the
    ; last real key is kept.
    static SyncRawKeys(want) {
        if (want = this.rawKeysOn)
            return
        dev := Buffer(8 + A_PtrSize, 0)
        NumPut("UShort", 1, dev, 0)
        NumPut("UShort", 6, dev, 2)
        if !want {
            NumPut("UInt", 0x1, dev, 4)
            DllCall("RegisterRawInputDevices", "Ptr", dev, "UInt", 1, "UInt", dev.Size)
            this.rawKeysOn := false
            return
        }
        if !this.rawGui {
            try {
                this.rawGui := Gui()
                OnMessage(0x00FF, RawMouseMsg)
            }
        }
        ok := false
        if this.rawGui {
            NumPut("UInt", 0x100, dev, 4)
            NumPut("Ptr", this.rawGui.Hwnd, dev, 8)
            ok := DllCall("RegisterRawInputDevices", "Ptr", dev, "UInt", 1, "UInt", dev.Size)
        }
        this.rawKeysOn := !!ok
    }

    static SetRawState(state) {
        if (state = this.rawState)
            return
        this.rawState := state
        if (state = "unavailable")
            this.Log("raw input unavailable, a lost mouse release waits for the next up")
        else
            this.Log("raw input " state)
    }

    ; One WM_INPUT. Only ups of middle, mouse 4 and mouse 5 count. SendInput has
    ; no device, and the board is the engine's own output.
    static ReadRaw(lParam) {
        head := 8 + 2 * A_PtrSize
        size := 0
        DllCall("GetRawInputData", "Ptr", lParam, "UInt", 0x10000003, "Ptr", 0, "UInt*", &size, "UInt", head)
        if (size < head + 8 || size > 256)
            return
        buf := Buffer(size, 0)
        if (DllCall("GetRawInputData", "Ptr", lParam, "UInt", 0x10000003, "Ptr", buf, "UInt*", &size, "UInt", head) = 0xFFFFFFFF)
            return
        kind := NumGet(buf, 0, "UInt")
        if (kind = 1) {
            this.NoteRawKey(NumGet(buf, 8, "Ptr"), A_TickCount)
            return
        }
        if (kind != 0)
            return
        flags := NumGet(buf, head + 4, "UShort")
        this.NoteRaw(NumGet(buf, 8, "Ptr"), flags)
    }

    static NoteRaw(device, flags) {
        if !(flags & 0x2A0)
            return
        if (!device || this.RawIsBoard(device))
            return
        if (flags & 0x20)
            this.rawUpCount["MButton"] := this.RawUps("MButton") + 1
        if (flags & 0x80)
            this.rawUpCount["XButton1"] := this.RawUps("XButton1") + 1
        if (flags & 0x200)
            this.rawUpCount["XButton2"] := this.RawUps("XButton2") + 1
    }

    ; A key from a real keyboard. SendInput has no device, and the board's
    ; keys are the engine's own output.
    static NoteRawKey(device, tick) {
        if (!device || this.RawIsBoard(device))
            return
        this.rawKeyTick := tick
    }

    static RawUps(name) {
        return this.rawUpCount.Has(name) ? this.rawUpCount[name] : 0
    }

    static RawIsBoard(device) {
        if this.rawBoard.Has(device)
            return this.rawBoard[device]
        size := 0
        DllCall("GetRawInputDeviceInfoW", "Ptr", device, "UInt", 0x20000007, "Ptr", 0, "UInt*", &size)
        name := ""
        if (size > 0 && size < 1024) {
            buf := Buffer(size * 2 + 2, 0)
            if (DllCall("GetRawInputDeviceInfoW", "Ptr", device, "UInt", 0x20000007, "Ptr", buf, "UInt*", &size) != 0xFFFFFFFF)
                name := StrGet(buf, "UTF-16")
        }
        board := RawNameIsBoard(name)
        this.rawBoard[device] := board
        return board
    }

    ; Raw Input saw this button go up since its last down, and the hook did not.
    static RawLostUp(name) {
        if (!this.rawOn || !(name = "MButton" || name = "XButton1" || name = "XButton2"))
            return false
        return this.RawUps(name) != (this.rawAtDown.Has(name) ? this.rawAtDown[name] : 0)
    }

    ; After the poll that saw a lost release's up. Only names queued before
    ; that poll began: those were up for all of it.
    static FlushRedo(names) {
        for name, _ in names {
            if !this.redo.Has(name)
                continue
            this.redo.Delete(name)
            this.rawAtDown[name] := this.RawUps(name)
            this.NotePhysical(name, true)
        }
    }

    static SyncMouseHook() {
        this.SyncRawMouse(this.MouseHookNeeded())
        if !this.MouseHookNeeded() {
            if this.hMouseHook {
                DllCall("UnhookWindowsHookEx", "Ptr", this.hMouseHook)
                this.hMouseHook := 0
            }
            return
        }
        if (!this.hMouseHook && this.mouseProc)
            this.RaiseMouseHook()
    }

    ; Injected SendInput is ignored. Pico HID is a real device, so those reports
    ; are matched as hidEcho instead of counting as the finger.
    static ReadKey(msg, info) {
        flags := NumGet(info, 8, "UInt")
        if (flags & 0x12)
            return false
        extraOff := 16
        if (NumGet(info, extraOff, "Ptr") = this.echo)
            return false
        up := (msg = 0x101 || msg = 0x105 || (flags & 0x80))
        vk := NumGet(info, 0, "UInt")
        scan := NumGet(info, 4, "UInt")
        name := SideKey(vk, scan, flags)
        if (name = "" && vk = 0x0D && (flags & 0x01))
            name := "NumpadEnter"
        else if (name = "" && this.vkName.Has(vk))
            name := this.vkName[vk]
        name := NormKey(name)
        if (name = "")
            return false
        if this.MatchHidEcho(name, !up)
            return false
        return this.NoteHooked(name, !up)
    }

    ; Windows auto-repeat, or a second side-button down, while the finger is down.
    ; It is not an edge: physDown is already true, so PollTriggers sees no new press.
    ; A middle or side button that Raw Input saw go up lost its up in the hook:
    ; it is up for one poll and pressed again after it, a new press.
    ; Board: the LL hook is the swallow. Software: the $* hotkey is.
    static NoteHooked(name, down) {
        held := this.physDown.Has(name) && this.physDown[name]
        lost := down && held && this.RawLostUp(name)
        repeat := down && held && !lost
        if repeat
            this.repeatSeen[name] := true
        if lost {
            this.lostSeen[name] := true
            this.redo[name] := true
            this.NotePhysical(name, false)
        } else {
            if (!down && this.redo.Has(name))
                this.redo.Delete(name)
            if (down && !repeat)
                this.rawAtDown[name] := this.RawUps(name)
            this.NotePhysical(name, down)
        }
        block := this.PassBoard() && this.Hooked(name) && this.SwallowTriggers() && !this.PassKey(name)
        if (down && !repeat)
            this.lastSwallow[name] := block
        return block
    }

    static Hooked(name) {
        if this.hooks.Has(name)
            return true
        folded := StrLower(String(name))
        if this.hookFold.Has(folded)
            return true
        return this.hooks.Has(folded)
    }

    ; Returns true to swallow. On a board this is the only swallow for middle and
    ; side triggers, like ReadKey for keys. The board's own clicks match hidEcho first.
    static ReadMouse(which, info) {
        flags := NumGet(info, 12, "UInt")
        if (flags & 0x03)
            return false
        extraOff := A_PtrSize = 8 ? 24 : 20
        if (NumGet(info, extraOff, "Ptr") = this.echo)
            return false
        name := ""
        down := false
        if (which = 0x201 || which = 0x202) {
            name := "LButton"
            down := which = 0x201
        } else if (which = 0x204 || which = 0x205) {
            name := "RButton"
            down := which = 0x204
        } else if (which = 0x207 || which = 0x208) {
            name := "MButton"
            down := which = 0x207
        } else if (which = 0x20B || which = 0x20C) {
            button := NumGet(info, 8, "UInt") >> 16
            name := button = 1 ? "XButton1" : button = 2 ? "XButton2" : ""
            down := which = 0x20B
        }
        if (name = "")
            return false
        if this.MatchHidEcho(name, down)
            return false
        ; Left and right are never triggers. Their clicks always reach the game.
        if (name = "LButton" || name = "RButton") {
            this.NotePhysical(name, down)
            return false
        }
        return this.NoteHooked(name, down)
    }

    static Now() {
        if !this.qpcFreq {
            freq := 0
            origin := 0
            DllCall("QueryPerformanceFrequency", "Int64*", &freq)
            DllCall("QueryPerformanceCounter", "Int64*", &origin)
            this.qpcFreq := freq
            this.qpcOrigin := origin
        }
        counter := 0
        DllCall("QueryPerformanceCounter", "Int64*", &counter)
        return (counter - this.qpcOrigin) * 1000 / this.qpcFreq
    }

    ; Block without spinning. Always pump queued input so low-level hooks
    ; cannot sit unprocessed and then fire in a burst. wakeOnInput returns
    ; as soon as a key/button message arrives. Mouse-move is ignored so
    ; aiming does not keep this thread awake.
    static Pump() {
        Sleep(0)
    }

    static HrtWait(ms, wakeOnInput := false) {
        if (ms < 0.3) {
            this.Pump()
            return
        }
        if !this.hTimer
            this.hTimer := DllCall("CreateWaitableTimerExW", "Ptr", 0, "Ptr", 0, "UInt", 2, "UInt", 0x1F0003, "Ptr")
        if !this.hTimer {
            DllCall("Sleep", "UInt", Max(1, Round(ms)))
            return
        }
        handles := Buffer(A_PtrSize, 0)
        NumPut("Ptr", this.hTimer, handles)
        deadline := this.Now() + ms
        ; QS_ALLINPUT minus QS_MOUSEMOVE.
        wake := 0x04FD
        loop {
            remain := deadline - this.Now()
            if (remain < 0.3) {
                this.Pump()
                return
            }
            due := -Max(1, Round(remain * 10000))
            DllCall("SetWaitableTimer", "Ptr", this.hTimer, "Int64*", &due, "Int", 0, "Ptr", 0, "Ptr", 0, "Int", 0)
            result := DllCall("MsgWaitForMultipleObjects", "UInt", 1, "Ptr", handles.Ptr, "Int", 0, "UInt", Max(1, Round(remain) + 2), "UInt", wake, "UInt")
            this.Pump()
            if (result = 1) {
                if wakeOnInput
                    return
            } else
                return
        }
    }

    static Soonest(now) {
        soon := now + 15
        for r in this.runners {
            if r.done
                continue
            if (r.waitUntil = 0 || r.waitUntil <= now)
                return now
            if (r.waitUntil < soon)
                soon := r.waitUntil
            for k, at in r.pulses
                if (at > now && at < soon)
                    soon := at
        }
        if this.pending.Length
            return now
        return soon
    }

    static NeedFineClock() {
        if this.pending.Length
            return true
        for r in this.runners {
            if !r.done
                return true
        }
        return false
    }

    static SyncPeriod() {
        want := this.NeedFineClock()
        if (want = this.periodOn)
            return
        if want
            DllCall("Winmm.dll\timeBeginPeriod", "UInt", 1)
        else
            DllCall("Winmm.dll\timeEndPeriod", "UInt", 1)
        this.periodOn := want
    }

    static Pace() {
        this.SyncPeriod()
        if (!this.armed && this.runners.Length = 0 && this.pending.Length = 0) {
            this.HrtWait(16)
            return
        }
        if (this.runners.Length = 0 && this.pending.Length = 0) {
            this.HrtWait(8, true)
            return
        }
        soon := this.Soonest(this.Now())
        remain := soon - this.Now()
        ; Never busy-spin. An overdue wait still has to pump hooks, or Windows
        ; queues them and they all fire at once after the stall.
        if (remain < 1)
            remain := 1
        if (remain > 8)
            remain := 8
        this.HrtWait(remain, true)
    }

    static Tick() {
        this.frontKnown := false
        this.focusCache := Map()
        now := this.Now()
        this.sendNow := Map()
        this.PollPipe()
        this.CheckPhysicalHook()
        this.ApplyLiveConfig()
        this.PollPanic()
        this.SyncSwallow()
        if this.hooksDirty {
            this.SyncHooks()
            this.hooksDirty := false
        }
        if this.armed
            this.PollTriggers()
        else
            this.FlushRedo(this.redo.Clone())
        this.RemoveDone()
        this.FlushPending()
        now := this.Now()
        for r in this.runners {
            ; One macro that throws must not abort the tick. Before this, an
            ; error here skipped Publish, so every other runner froze with its
            ; keys still down. Kill only the broken runner and keep going.
            try r.Advance(now)
            catch as err {
                this.Log("runner " r.id " " err.Message)
                try r.MarkDone()
                catch
                    r.done := true
            }
        }
        this.FlushPending()
        ; Publish before dropping finished runners. A scroll or move on the last
        ; step is a one-shot, and RemoveDone would throw it away with the runner.
        this.Publish(now)
        this.RemoveDone()
        if this.armed
            this.PollOwed()
        this.SyncRawKeys(this.armed)
        this.Broadcast(false)
    }

    ; First matching focusExe wins. An empty focusExe is the fallback, preferring activeId.
    static PickLive(configs, front, prefer) {
        chosen := ""
        fallback := ""
        if !(configs is Array)
            return ""
        for cfg in configs {
            if !(cfg is Map)
                continue
            exe := String(Field(cfg, "focusExe", ""))
            if (exe = "") {
                if !(fallback is Map)
                    fallback := cfg
                else if (String(Field(cfg, "id", "")) = prefer)
                    fallback := cfg
                continue
            }
            if (front != "" && this.FocusMatch(exe, front)) {
                chosen := cfg
                break
            }
        }
        return (chosen is Map) ? chosen : fallback
    }

    ; Pick the config whose focusExe matches the foreground app.
    ; An empty focusExe is the fallback. No configs means keep the flat macro list.
    ; turn.countsPer360 of the live config, or of the profile when it has no
    ; configs. 0 is not calibrated.
    static TurnPer360() {
        owner := this.profile
        configs := Field(this.profile, "configs", "")
        if (configs is Array) && (configs.Length > 0) {
            owner := ""
            for config in configs
                if (config is Map && String(Field(config, "id", "")) = this.liveId)
                    owner := config
        }
        turn := (owner is Map) ? Field(owner, "turn", "") : ""
        if !(turn is Map)
            return 0
        per360 := AsNum(Field(turn, "countsPer360", 0), 0)
        if !(per360 > 0)
            return 0
        return Min(per360, 1000000000)
    }

    static ApplyLiveConfig() {
        configs := Field(this.profile, "configs", "")
        if !(configs is Array) || (configs.Length = 0) {
            if (this.liveId != "-") {
                this.liveId := "-"
                this.liveName := ""
                this.SeedTriggers(true)
                this.ReapOrphans()
                this.hooksDirty := true
            }
            return
        }
        front := this.ActiveExe()
        prefer := String(Field(this.profile, "activeId", ""))
        pick := this.PickLive(configs, front, prefer)
        id := (pick is Map) ? String(Field(pick, "id", "")) : ""
        if (id = this.liveId)
            return
        this.liveId := id
        this.liveName := (pick is Map) ? String(Field(pick, "name", "")) : ""
        if (pick is Map) {
            macros := Field(pick, "macros", [])
            if !(macros is Array)
                macros := []
            this.profile["macros"] := macros
            this.profile["focusExe"] := String(Field(pick, "focusExe", ""))
            this.profile["activeId"] := id
        } else {
            this.profile["macros"] := []
            this.profile["focusExe"] := ""
        }
        this.SeedTriggers(true)
        this.ReapOrphans()
        this.hooksDirty := true
    }

    static LoadDisk() {
        this.profilePath := A_ScriptDir "\..\profiles\default.json"
        if !FileExist(this.profilePath) {
            DirCreate(A_ScriptDir "\..\profiles")
            FileAppend('{"version":1,"name":"Default","variables":{},"macros":[]}', this.profilePath, "UTF-8")
        }
        ; A corrupt or half-written file used to throw here. At startup that is
        ; outside Loop's try, so the engine died with a dialog and the shell
        ; kept relaunching it. Keep the last good profile (or none) and report.
        loaded := ""
        try {
            text := FileRead(this.profilePath, "UTF-8")
            loaded := JSON.Parse(text)
        } catch as err {
            this.Log("profile read " err.Message)
            this.SendRaw('{"v":1,"type":"error","where":"engine","code":"profile-read","detail":"the saved profile could not be read"}')
            loaded := ""
        }
        if (loaded is Map)
            this.profile := loaded
        else if !(this.profile is Map)
            this.profile := Map()
        this.liveId := Chr(1)
        this.hookSuspect := true
        this.ApplyLiveConfig()
        if this.hooksDirty {
            this.SyncHooks()
            this.hooksDirty := false
        }
        this.winAt := -100000
        this.Broadcast(true)
    }

    static SetArmed(on) {
        if (on != this.armed)
            this.Log(on ? "armed on" : "armed off")
        this.armed := on
        if !on
            this.StopAll()
        this.hooksDirty := true
        this.SyncHooks()
        this.hooksDirty := false
        this.SeedTriggers()
        this.RemoveDone()
        this.Publish(this.Now())
        this.winAt := -100000
        this.Broadcast(true)
    }

    static FindMacro(id) {
        macros := Field(this.profile, "macros", [])
        if !(macros is Array)
            return ""
        for m in macros
            if (m is Map && String(Field(m, "id", "")) = id)
                return m
        return ""
    }

    static HasRunner(id) {
        for r in this.runners
            if (!r.done && r.id = id)
                return true
        for r in this.pending
            if (r.id = id)
                return true
        return false
    }

    static PrepareBasic(macro) {
        if !Field(macro, "basic", false)
            return macro
        trig := Field(macro, "trigger", Map())
        if !(trig is Map)
            return macro
        button := NormKey(Field(trig, "button", ""))
        if (button = "" || button = "LButton" || button = "RButton")
            return macro
        hold := 18
        gap := AsNum(Field(macro, "gapMs", 18), 18)
        if (gap < 0)
            gap := 0
        steps := []
        if IsMouseButton(button) {
            steps.Push(Map("type", "mouse", "action", "down", "button", button))
            steps.Push(Map("type", "wait", "ms", hold))
            steps.Push(Map("type", "mouse", "action", "up", "button", button))
            steps.Push(Map("type", "wait", "ms", gap))
        } else if (String(Field(trig, "kind", "")) = "key") {
            steps.Push(Map("type", "key", "action", "down", "key", button))
            steps.Push(Map("type", "wait", "ms", hold))
            steps.Push(Map("type", "key", "action", "up", "key", button))
            steps.Push(Map("type", "wait", "ms", gap))
        } else
            return macro
        macro["steps"] := steps
        return macro
    }

    ; true when queued. Otherwise refused says why, like native:
    ; already-running is a whileHeld or toggle runner that is still playing.
    static refused := ""
    static StartRunner(macro, modeOverride := "", restart := true, why := "edge") {
        this.refused := ""
        if !(macro is Map)
            return false
        macro := this.PrepareBasic(macro)
        id := String(Field(macro, "id", ""))
        if (id = "")
            return false
        mode := modeOverride != "" ? String(modeOverride) : String(Field(macro, "playMode", "once"))
        exists := this.HasRunner(id)
        if (exists && !restart) {
            this.refused := "run-target-live"
            return false
        }
        if (exists && (mode = "whileHeld" || mode = "toggle")) {
            this.refused := "already-running"
            return false
        }
        if (Field(macro, "busy", false) && exists) {
            this.refused := "busy"
            return false
        }
        if exists
            this.StopRunner(id, "restart")
        if Field(macro, "exclusive", false)
            this.StopAll("exclusive")
        r := Runner(macro, mode)
        this.pending.Push(r)
        this.Log(Format('runner start macro="{}" id={} mode={} reason={}', r.name, id, mode, why))
        return true
    }

    static StopRunner(id, why := "stop") {
        for r in this.runners
            if (r.id = id)
                r.MarkDone(why)
        kept := []
        for r in this.pending
            if (r.id != id)
                kept.Push(r)
        this.pending := kept
    }

    static HasPending(id) {
        for r in this.pending
            if (r.id = id && !r.done)
                return true
        return false
    }

    static PauseRunner(id) {
        for r in this.runners
            if (r.id = id && !r.done)
                r.Pause()
        for r in this.pending
            if (r.id = id && !r.done)
                r.Pause()
    }

    static UnpauseRunner(id) {
        for r in this.runners
            if (r.id = id && !r.done)
                r.paused := false
        for r in this.pending
            if (r.id = id && !r.done)
                r.paused := false
    }

    static AskStop(id) {
        for r in this.runners
            if (r.id = id)
                r.AskStop()
    }

    static StopAll(why := "stop-all") {
        for r in this.runners
            r.MarkDone(why)
        this.pending := []
    }

    static MuteIds(ids) {
        if !(ids is Array)
            return
        for raw in ids {
            id := String(raw)
            if (id = "")
                continue
            slot := this.muted.Has(id) ? this.muted[id] : ""
            if !(slot is Map) {
                slot := Map("count", 0, "resume", this.HasRunner(id) || this.HasPending(id))
                this.muted[id] := slot
            }
            count := Integer(slot["count"]) + 1
            slot["count"] := count
            if (count = 1)
                this.PauseRunner(id)
        }
    }

    static UnmuteIds(ids) {
        if !(ids is Array)
            return
        for raw in ids {
            id := String(raw)
            if (id = "" || !this.muted.Has(id))
                continue
            slot := this.muted[id]
            if !(slot is Map)
                continue
            count := Integer(slot["count"]) - 1
            if (count > 0) {
                slot["count"] := count
                continue
            }
            resume := slot["resume"]
            this.muted.Delete(id)
            this.UnpauseRunner(id)
            if (this.HasRunner(id) || this.HasPending(id)) {
                ; PollTriggers skips muted ids, so a plain while-held macro whose
                ; key came up during the mute is still alive here. Advance runs
                ; before the next poll and would send one tick of its keys.
                macro := this.FindMacro(id)
                key := this.MacroTrigger(id)
                if (macro is Map && !Field(macro, "advanced", false) && key != "" && !this.KeyDown(key)) {
                    for r in this.runners
                        if (r.id = id && !r.done && r.mode = "whileHeld")
                            r.MarkDone("unmute")
                }
                continue
            }
            if (resume || this.HoldReplay(id)) {
                macro := this.FindMacro(id)
                if (macro is Map)
                    this.StartRunner(macro, "", true, "unmute")
            }
        }
    }

    ; A hold macro whose key is still down should start when the mute lifts,
    ; even if it was not running at the moment the mute began.
    static HoldReplay(id) {
        macro := this.FindMacro(id)
        if !(macro is Map)
            return false
        trig := Field(macro, "trigger", Map())
        button := (trig is Map) ? String(Field(trig, "button", "")) : ""
        if !this.KeyDown(button)
            return false
        blocks := Field(macro, "blocks", [])
        if (blocks is Array) {
            for block in blocks {
                if !(block is Map)
                    continue
                kind := String(Field(block, "type", ""))
                if (kind = "whileHeld" || kind = "swapAfter" || kind = "onceHeld")
                    return true
            }
        }
        return String(Field(macro, "playMode", "once")) = "whileHeld"
    }

    static IsMuted(id) {
        if !this.muted.Has(id)
            return false
        slot := this.muted[id]
        return (slot is Map) && Integer(slot["count"]) > 0
    }

    static SameKey(a, b) {
        left := NormKey(a)
        right := NormKey(b)
        if (left = "" || right = "")
            return false
        return (left = right || StrLower(left) = StrLower(right))
    }

    static MacrosOnWatch(watch, skipId) {
        ids := []
        if !(watch is Array)
            return ids
        macros := Field(this.profile, "macros", [])
        if !(macros is Array)
            return ids
        skip := String(skipId)
        for macro in macros {
            if !(macro is Map)
                continue
            id := String(Field(macro, "id", ""))
            if (id = "" || id = skip)
                continue
            trig := Field(macro, "trigger", Map())
            button := (trig is Map) ? String(Field(trig, "button", "")) : ""
            if (button = "")
                continue
            for item in watch {
                if this.SameKey(button, item) {
                    ids.Push(id)
                    break
                }
            }
        }
        return ids
    }

    static MacroTrigger(id) {
        macro := this.FindMacro(id)
        if !(macro is Map)
            return ""
        trig := Field(macro, "trigger", Map())
        button := (trig is Map) ? String(Field(trig, "button", "")) : ""
        return NormKey(button)
    }

    static PassKey(key) {
        k := NormKey(key)
        return k != "" && this.passKeys.Has(k) && Integer(this.passKeys[k]) > 0
    }

    static Sending(key) {
        k := NormKey(key)
        return k != "" && this.sendNow.Has(k)
    }

    static HoldingOut(key) {
        k := NormKey(key)
        return k != "" && this.applied.Has(k)
    }

    ; Sent keys can fire $* hotkeys. Downs we are outputting are echo, not a finger.
    ; Ups still count unless this exact Send is in flight, so a real release can stop a hold.
    static Echoing(key, up := false) {
        if this.Sending(key)
            return true
        return !up && this.HoldingOut(key)
    }

    static AddPass(key) {
        if (key = "")
            return
        this.passKeys[key] := (this.passKeys.Has(key) ? Integer(this.passKeys[key]) : 0) + 1
        this.RebindKey(key)
    }

    static DropPass(key) {
        if (key = "" || !this.passKeys.Has(key))
            return
        n := Integer(this.passKeys[key]) - 1
        if (n <= 0)
            this.passKeys.Delete(key)
        else
            this.passKeys[key] := n
        this.RebindKey(key)
    }

    static RebindKey(key) {
        if this.dry || (key = "") || !this.hooks.Has(key)
            return
        this.UnhookKey(key)
        try {
            Hotkey(HookSpec(key), TriggerHook, "On")
            Hotkey(HookSpec(key) " up", TriggerHook, "On")
        } catch as err
            this.Log("rebind " key " " err.Message)
    }

    static EnsurePass(id) {
        if this.oncePass.Has(id)
            return
        key := this.MacroTrigger(id)
        if (key = "")
            return
        this.oncePass[id] := key
        this.AddPass(key)
    }

    static StartOncePlay(macro) {
        mode := String(Field(macro, "playMode", "once"))
        if this.HasTapHold(macro)
            mode := "whileHeld"
        override := (mode = "whileHeld" || mode = "toggle") ? "" : "once"
        this.StartRunner(macro, override)
    }

    static ArmOnceLock(ids) {
        if !(ids is Array)
            return
        for raw in ids {
            id := String(raw)
            if (id = "")
                continue
            this.EnsurePass(id)
            if this.HasRunner(id) {
                this.onceLock[id] := "done"
                continue
            }
            macro := this.FindMacro(id)
            key := this.MacroTrigger(id)
            down := key != "" && this.KeyDown(key)
            if (macro is Map && down) {
                this.StartOncePlay(macro)
                this.onceLock[id] := "done"
                continue
            }
            this.onceLock[id] := "open"
        }
    }

    static ReleaseOnceLock(ids) {
        if !(ids is Array)
            return
        for raw in ids {
            id := String(raw)
            if (id = "")
                continue
            if this.oncePass.Has(id) {
                this.DropPass(this.oncePass[id])
                this.oncePass.Delete(id)
            }
            if this.onceLock.Has(id)
                this.onceLock.Delete(id)
        }
    }

    static OnceOpen(id) {
        return this.onceLock.Has(id) && this.onceLock[id] = "open"
    }

    static OnceDone(id) {
        return this.onceLock.Has(id) && this.onceLock[id] = "done"
    }

    static SpendOnce(id) {
        this.onceLock[id] := "done"
    }

    static FlushPending() {
        for r in this.pending
            this.runners.Push(r)
        this.pending := []
    }

    static RemoveDone() {
        kept := []
        for r in this.runners {
            if !r.done {
                kept.Push(r)
                continue
            }
            age := Max(0, Round(r.heldMs))
            ; A stop with the trigger still down is the one to look at.
            held := r.trigger = "" ? "-" : (this.KeyDown(r.trigger) ? "down" : "up")
            this.Log(Format('runner stop macro="{}" id={} mode={} reason={} age={}ms trigger={}',
                r.name, r.id, r.mode, r.endWhy = "" ? "finished" : r.endWhy, age, held))
        }
        this.runners := kept
        for id, state in this.onceLock
            if (state = "done" && !this.HasRunner(id))
                this.EnsurePass(id)
    }

    static ReapOrphans() {
        live := Map()
        macros := Field(this.profile, "macros", [])
        if (macros is Array) {
            for m in macros
                if (m is Map)
                    live[String(Field(m, "id", ""))] := true
        }
        for r in this.runners
            if !live.Has(r.id)
                r.MarkDone("orphan")
    }

    static FocusOk(macro) {
        exe := String(Field(macro, "focusExe", ""))
        if (exe = "")
            exe := String(Field(this.profile, "focusExe", ""))
        if (exe = "") {
            ; An empty focus matches every window, including this editor, so
            ; typing a macro name would start the macro and swallow the key.
            return !this.ShellInFront()
        }
        key := StrLower(exe)
        if this.focusCache.Has(key)
            return this.focusCache[key]
        active := this.ActiveExe()
        ok := (active != "" && this.FocusMatch(exe, active))
        this.focusCache[key] := ok
        return ok
    }

    static ExeStem(name) {
        s := StrLower(Trim(String(name)))
        if (SubStr(s, -4) = ".exe")
            s := SubStr(s, 1, StrLen(s) - 4)
        return s
    }

    ; Dawnwalker.exe and Dawnwalker-Win64-Shipping.exe are the same game.
    static FocusMatch(want, have) {
        a := this.ExeStem(want)
        b := this.ExeStem(have)
        if (a = "" || b = "")
            return false
        if (a = b)
            return true
        if (InStr(b, a "-") = 1 || InStr(a, b "-") = 1)
            return true
        return false
    }

    static ShellInFront() {
        exe := this.ActiveExe()
        if this.FocusMatch("MacroShell.exe", exe)
            return true
        ; Typing in the page can leave the WebView host as the foreground window.
        if !this.FocusMatch("msedgewebview2.exe", exe)
            return false
        hwnd := DllCall("GetForegroundWindow", "Ptr")
        root := DllCall("GetAncestor", "Ptr", hwnd, "UInt", 3, "Ptr")
        if !root
            return false
        owner := ""
        try owner := WinGetProcessName(root)
        catch
            owner := ""
        if (owner = "")
            owner := this.ExeFromHwnd(root)
        return this.FocusMatch("MacroShell.exe", owner)
    }

    static ActiveExe() {
        if this.frontKnown
            return this.frontExe
        hwnd := DllCall("GetForegroundWindow", "Ptr")
        exe := ""
        if hwnd {
            try exe := WinGetProcessName(hwnd)
            catch
                exe := ""
            if (exe = "")
                exe := this.ExeFromHwnd(hwnd)
        }
        this.frontExe := exe
        this.frontKnown := true
        return exe
    }

    static ExeFromHwnd(hwnd) {
        pid := 0
        DllCall("GetWindowThreadProcessId", "Ptr", hwnd, "UInt*", &pid)
        if !pid
            return ""
        proc := DllCall("OpenProcess", "UInt", 0x1000, "Int", 0, "UInt", pid, "Ptr")
        if !proc
            return ""
        n := 260
        path := ""
        loop 2 {
            buf := Buffer(n * 2)
            chars := n
            ok := DllCall("QueryFullProcessImageNameW", "Ptr", proc, "UInt", 0, "Ptr", buf, "UInt*", &chars, "Int")
            if ok {
                path := StrGet(buf, "UTF-16")
                break
            }
            n := 1024
        }
        DllCall("CloseHandle", "Ptr", proc)
        if (path = "")
            return ""
        SplitPath path, &name
        return name
    }

    ; Keys are swallowed only while the focus window matches. Otherwise the
    ; user can type normally; macros stay off via FocusOk.
    static SwallowTriggers() {
        return this.FocusOk(Map())
    }

    static SyncSwallow() {
        want := this.SwallowTriggers()
        if (this.swallowKnown && want = this.swallowOn)
            return
        if (this.swallowKnown && want != this.swallowOn)
            this.ClearHookBindings()
        this.swallowOn := want
        this.swallowKnown := true
        this.hooksDirty := true
    }

    static WatchingHold() {
        for r in this.runners {
            if r.done
                continue
            if (r.mode = "whileHeld" || r.mode = "toggle")
                return true
            if (r.HasProp("scanWait") && r.scanWait)
                return true
            for frame in r.stack {
                if (frame.HasProp("holdLoop") && frame.holdLoop)
                    return true
                if (frame.HasProp("edgeHold") && frame.edgeHold)
                    return true
            }
        }
        return false
    }

    ; Real finger, including keys this macro is not hooked to. "P" ignores keys the macro itself sends.
    static FingerDown(name) {
        key := NormKey(name)
        if (key = "")
            return false
        if (this.dry && this.finger.Has(key))
            return this.finger[key]
        if (this.physDown.Has(key))
            return this.physDown[key]
        if this.Echoing(key)
            return false
        down := false
        try down := GetKeyState(key, "P")
        return down
    }

    static SentDown(name) {
        key := NormKey(name)
        if (key = "")
            return false
        for r in this.runners {
            if r.done
                continue
            if r.holds.Has(key)
                return true
            if r.pulses.Has(key)
                return true
        }
        return false
    }

    static SentSince(name, since) {
        key := NormKey(name)
        if (key = "" || !this.lastSent.Has(key))
            return false
        return this.lastSent[key] >= since
    }

    static KeyDown(button) {
        key := NormKey(button)
        if (key = "")
            return false
        ; Every trigger, recorded or basic, uses the physical finger while that hook is up.
        ; GetKeyState mixes in keys the macro itself is sending.
        if (this.rawOk || this.hooks.Has(key))
            return this.physDown.Has(key) && this.physDown[key]
        if this.Echoing(key)
            return false
        down := false
        try down := GetKeyState(key, "P")
        return down
    }

    ; keep: a reload or a config switch. An id whose trigger is unchanged
    ; keeps prev, so a press or release since the last poll is still an edge.
    ; A new or changed trigger, and a start, read the finger now: a key held
    ; before that does not start.
    static SeedTriggers(keep := false) {
        old := this.prevDown
        oldButton := this.prevButton
        this.prevDown := Map()
        this.prevButton := Map()
        this.owed := Map()
        this.macroTapHold := Map()
        this.macroHoldRel := Map()
        macros := Field(this.profile, "macros", [])
        if !(macros is Array)
            return
        for macro in macros {
            if !(macro is Map)
                continue
            id := String(Field(macro, "id", ""))
            trig := Field(macro, "trigger", Map())
            button := (trig is Map) ? String(Field(trig, "button", "")) : ""
            if (keep && old.Has(id) && oldButton.Has(id) && oldButton[id] = button)
                this.prevDown[id] := old[id]
            else
                this.prevDown[id] := this.KeyDown(button)
            this.prevButton[id] := button
            if (id != "") {
                this.macroTapHold[id] := this.ScanTapHold(macro)
                this.macroHoldRel[id] := this.ScanHoldsOnRelease(macro)
            }
        }
    }

    static PollPanic() {
        down := false
        try down := GetKeyState("Pause", "P")
        if (down && !this.panicWas)
            this.StopAll()
        this.panicWas := down
    }

    static PollTriggers() {
        redo := this.redo.Clone()
        lost := this.lostSeen
        this.lostSeen := Map()
        macros := Field(this.profile, "macros", [])
        if !(macros is Array) {
            this.FlushRedo(redo)
            return
        }
        for macro in macros {
            if !(macro is Map)
                continue
            id := String(Field(macro, "id", ""))
            if (id = "")
                continue
            trig := Field(macro, "trigger", Map())
            button := (trig is Map) ? String(Field(trig, "button", "")) : ""
            down := this.KeyDown(button)
            was := this.prevDown.Has(id) ? this.prevDown[id] : false
            this.prevDown[id] := down
            if (!down && this.owed.Has(id))
                this.owed.Delete(id)
            key := NormKey(button)
            ; A repeat left physDown true, so it is not an edge. One line per hold.
            if (key != "" && down && this.repeatSeen.Has(key) && !this.repeatLogged.Has(key)) {
                this.repeatLogged[key] := true
                this.Log(Format('trigger {} repeat ignored macro="{}" id={}', key, Field(macro, "name", ""), id))
            }
            if (key != "" && !down && this.repeatLogged.Has(key))
                this.repeatLogged.Delete(key)
            if (key != "" && lost.Has(key))
                this.Log(Format('trigger {} lost release, new press macro="{}" id={}', key, Field(macro, "name", ""), id))
            did := this.PollOne(macro, id, down, was)
            if (key != "" && down != was)
                this.Log(Format('trigger {} {} macro="{}" id={} action={} swallow={}', key, down ? "down" : "up",
                    Field(macro, "name", ""), id, did, this.TriggerSwallowed(key) ? "yes" : "no"))
            if (did = "playing")
                this.Log(Format('start blocked macro="{}" id={} mode=once reason=playing', Field(macro, "name", ""), id))
            else if (did = "already-running" || did = "busy")
                this.Log(Format('start blocked macro="{}" id={} mode={} reason={}', Field(macro, "name", ""), id, this.TriggerMode(macro), did))
        }
        this.repeatSeen := Map()
        this.FlushRedo(redo)
    }

    ; What the hook did with this trigger's down. Software swallows with the $* hotkey.
    static TriggerSwallowed(key) {
        if this.PassBoard()
            return this.lastSwallow.Has(key) && this.lastSwallow[key]
        return this.Hooked(key) && SubStr(HookSpec(key), 1, 1) != "~"
    }

    ; Run Once: play once, with no block that loops while the trigger is held.
    ; One press is one whole pass, so a press while it plays is ignored.
    static IsRunOnce(macro) {
        if (String(Field(macro, "playMode", "once")) != "once" || this.HasTapHold(macro))
            return false
        blocks := Field(macro, "blocks", [])
        if (Field(macro, "advanced", false) && (blocks is Array)) {
            for block in blocks {
                kind := (block is Map) ? String(Field(block, "type", "")) : ""
                if (kind = "whileHeld" || kind = "swapAfter" || kind = "onceHeld")
                    return false
            }
        }
        return true
    }

    ; One macro's trigger. Returns what it did, for the log.
    static PollOne(macro, id, down, was) {
        if this.IsMuted(id) {
            this.DropOwed(id)
            return "muted"
        }
        ; A held key does not start it again when focus comes back: prev
        ; stays down, so that needs a new press.
        if !Field(macro, "enabled", true) || !this.FocusOk(macro) {
            why := Field(macro, "enabled", true) ? "focus" : "disabled"
            this.StopRunner(id, why)
            this.DropOwed(id)
            return why
        }
        if this.OnceDone(id) {
            if this.HasRunner(id) {
                mode := String(Field(macro, "playMode", "once"))
                if this.HasTapHold(macro)
                    mode := "whileHeld"
                if (!down && (mode = "whileHeld")) {
                    this.AskStop(id)
                    return "ask-stop"
                }
                return "once-done"
            }
            this.EnsurePass(id)
            return "once-done"
        }
        once := this.OnceOpen(id)
        mode := String(Field(macro, "playMode", "once"))
        if this.HasTapHold(macro)
            mode := "whileHeld"
        ; Playback "On release" means start when the key comes up. That is not
        ; the editor On release (nearest up / play full) stop rule.
        if (Field(macro, "advanced", false) && mode != "onRelease") {
            if (down && !was) {
                if (this.HasRunner(id) && this.IsRunOnce(macro))
                    return "playing"
                if once
                    this.StartOncePlay(macro)
                else if !this.StartRunner(macro)
                    return this.NoteOwed(id, mode)
                if once
                    this.SpendOnce(id)
                return "start"
            } else if (!down && this.HasRunner(id) && !once) {
                if this.HoldsOnRelease(macro) {
                    this.AskStop(id)
                    return "ask-stop"
                }
            }
            return "none"
        }
        action := this.TriggerAction(mode, down, was, this.HasRunner(id))
        if (action = "start") {
            if (this.HasRunner(id) && this.IsRunOnce(macro))
                return "playing"
            if once
                this.StartOncePlay(macro)
            else if !this.StartRunner(macro)
                return this.NoteOwed(id, mode)
            if once
                this.SpendOnce(id)
            return "start"
        } else if (action = "stop") {
            this.StopRunner(id, mode = "toggle" ? "toggle" : "release")
            return "release"
        }
        return "none"
    }

    ; Only whileHeld keeps the press: its runner can still be finishing a pass
    ; after the release. Run Once drops a press while it plays, never owes it.
    static NoteOwed(id, mode) {
        why := this.refused != "" ? this.refused : "none"
        if (why = "already-running" && mode = "whileHeld")
            this.owed[id] := true
        return why
    }

    static DropOwed(id) {
        if this.owed.Has(id)
            this.owed.Delete(id)
    }

    static TriggerMode(macro) {
        return this.HasTapHold(macro) ? "whileHeld" : String(Field(macro, "playMode", "once"))
    }

    ; After the runners advance. An owed press starts once its last runner is
    ; gone and the key is still down. An up, a focus switch, a disabled macro
    ; or a mute drops it.
    static PollOwed() {
        if (this.owed.Count = 0)
            return
        macros := Field(this.profile, "macros", [])
        if !(macros is Array) {
            this.owed := Map()
            return
        }
        live := Map()
        for macro in macros {
            if !(macro is Map)
                continue
            id := String(Field(macro, "id", ""))
            live[id] := true
            if !this.owed.Has(id)
                continue
            trig := Field(macro, "trigger", Map())
            button := (trig is Map) ? String(Field(trig, "button", "")) : ""
            if (!this.KeyDown(button) || this.IsMuted(id) || !Field(macro, "enabled", true)
                || !this.FocusOk(macro) || this.OnceDone(id) || this.OnceOpen(id)) {
                this.owed.Delete(id)
                continue
            }
            if this.HasRunner(id)
                continue
            this.owed.Delete(id)
            this.StartRunner(macro, "", true, "held-press")
        }
        for id, _ in this.owed.Clone()
            if !live.Has(id)
                this.owed.Delete(id)
    }

    static HasTapHold(macro) {
        id := String(Field(macro, "id", ""))
        if (id != "" && this.macroTapHold.Has(id))
            return this.macroTapHold[id]
        return this.ScanTapHold(macro)
    }

    static ScanTapHold(macro) {
        blocks := Field(macro, "blocks", [])
        if !(blocks is Array)
            return false
        for block in blocks
            if (block is Map) {
                kind := String(Field(block, "type", ""))
                if (kind = "tapHold" || kind = "tapSpam")
                    return true
            }
        return false
    }

    static HoldsOnRelease(macro) {
        id := String(Field(macro, "id", ""))
        if (id != "" && this.macroHoldRel.Has(id))
            return this.macroHoldRel[id]
        return this.ScanHoldsOnRelease(macro)
    }

    static ScanHoldsOnRelease(macro) {
        blocks := Field(macro, "blocks", [])
        hasHold := false
        hasTap := false
        hasSpam := false
        if (blocks is Array) {
            for block in blocks {
                if !(block is Map)
                    continue
                kind := String(Field(block, "type", ""))
                if (kind = "whileHeld" || kind = "swapAfter" || kind = "onceHeld")
                    hasHold := true
                else if (kind = "tapSpam")
                    hasSpam := true
                else if (kind = "tapHold")
                    hasTap := true
            }
        }
        ; A While-held or spam block already exits when the trigger comes up. Asking the
        ; whole runner to stop would skip Run-once / After-release blocks after it.
        if (hasHold || hasSpam)
            return false
        if hasTap
            return true
        mode := String(Field(macro, "playMode", "once"))
        return (mode = "whileHeld" || mode = "toggle")
    }

    static TriggerAction(mode, down, was, running) {
        mode := String(mode)
        if (mode = "whileHeld") {
            if (running && !down)
                return "stop"
            if (down && !was)
                return "start"
            return ""
        }
        if (mode = "toggle") {
            if (down && !was)
                return running ? "stop" : "start"
            return ""
        }
        if (mode = "onRelease") {
            if (!down && was)
                return "start"
            return ""
        }
        if (down && !was)
            return "start"
        return ""
    }

    static SyncHooks() {
        if this.dry
            return
        want := Map()
        if this.armed {
            macros := Field(this.profile, "macros", [])
            if (macros is Array) {
                for macro in macros {
                    if !(macro is Map)
                        continue
                    if !Field(macro, "enabled", true)
                        continue
                    steps := Field(macro, "steps", [])
                    blocks := Field(macro, "blocks", [])
                    hasSteps := (steps is Array) && steps.Length > 0
                    hasBlocks := Field(macro, "advanced", false) && (blocks is Array) && blocks.Length > 0
                    if (!hasSteps && !hasBlocks)
                        continue
                    trig := Field(macro, "trigger", Map())
                    button := (trig is Map) ? String(Field(trig, "button", "")) : ""
                    key := NormKey(button)
                    if (key = "" || key = "Pause" || key = "LButton" || key = "RButton")
                        continue
                    want[key] := true
                }
            }
        }
        for key, _ in this.hooks.Clone() {
            if want.Has(key)
                continue
            this.UnhookKey(key)
            this.hooks.Delete(key)
            if this.physDown.Has(key)
                this.physDown.Delete(key)
        }
        added := false
        for key, _ in want {
            if this.hooks.Has(key)
                continue
            try {
                Hotkey(HookSpec(key), TriggerHook, "On")
                Hotkey(HookSpec(key) " up", TriggerHook, "On")
                this.hooks[key] := true
                added := true
            } catch as err
                this.Log("hook " key " " err.Message)
        }
        if added
            this.RaisePhysicalHook()
        this.hookFold := Map()
        for key, _ in this.hooks
            this.hookFold[StrLower(key)] := true
        this.SyncMouseHook()
    }

    static KeyDelayMs() {
        if this.dry
            return 400
        if (this.delayMs >= 0)
            return this.delayMs
        delay := 1
        DllCall("SystemParametersInfoW", "UInt", 0x16, "UInt", 0, "UInt*", &delay, "UInt", 0)
        if (delay < 0 || delay > 3)
            delay := 1
        this.delayMs := 250 * (delay + 1)
        return this.delayMs
    }

    static KeyRepeatMs() {
        if this.dry
            return 33
        if (this.repeatMs >= 0)
            return this.repeatMs
        speed := 31
        DllCall("SystemParametersInfoW", "UInt", 0x0A, "UInt", 0, "UInt*", &speed, "UInt", 0)
        if (speed < 0)
            speed := 0
        if (speed > 31)
            speed := 31
        this.repeatMs := Max(16, Round(1000 / (2.5 + speed * (27.5 / 31))))
        return this.repeatMs
    }

    static Desired(now) {
        want := Map()
        for r in this.runners {
            if (r.done || r.paused)
                continue
            for k, _ in r.holds
                want[k] := true
            for k, releaseAt in r.pulses
                if (releaseAt > now)
                    want[k] := true
        }
        return want
    }

    static Publish(now) {
        dx := 0
        dy := 0
        turn := 0
        wheel := 0
        hasAbs := false
        absX := 0
        absY := 0
        for r in this.runners {
            if r.paused
                continue
            dx += r.moveX
            dy += r.moveY
            turn += r.turnX
            r.turnX := 0
            wheel += r.wheel
            if r.hasGoto {
                hasAbs := true
                absX := r.gotoX
                absY := r.gotoY
                r.hasGoto := false
            }
            r.moveX := 0
            r.moveY := 0
            r.wheel := 0
        }
        ; A board gets counts. With a turn in this tick, the pixel moves are
        ; scaled here and the turn is added after, so Emit does not scale.
        counts := false
        if (turn != 0 && this.outputMode != "software" && !hasAbs) {
            dx := Runner.Prototype.Clamp(dx)
            dy := Runner.Prototype.Clamp(dy)
            this.HidScale(&dx, &dy)
            counts := true
        }
        dx += turn
        if (dx > 16000)
            dx := 16000
        if (dx < -16000)
            dx := -16000
        if (dy > 16000)
            dy := 16000
        if (dy < -16000)
            dy := -16000
        desired := this.Desired(now)
        downs := []
        ups := []
        extra := Map()
        for r in this.runners {
            if (r.done || r.paused)
                continue
            for k, _ in r.echoDown
                if (desired.Has(k) && !IsMouseButton(k))
                    extra[k] := true
            r.echoDown := Map()
        }
        for k, _ in desired
            if !this.applied.Has(k)
                downs.Push(k)
        for k, _ in extra {
            found := false
            for item in downs
                if (item = k)
                    found := true
            if !found
                downs.Push(k)
        }
        for k, _ in this.applied
            if !desired.Has(k)
                ups.Push(k)
        for k in downs {
            this.applied[k] := true
            this.lastSent[k] := now
        }
        for k in ups
            if this.applied.Has(k)
                this.applied.Delete(k)
        if this.dry {
            this.sent.Push(Map("down", downs, "up", ups, "x", dx, "y", dy, "wheel", wheel, "counts", counts))
            return
        }
        if (hasAbs && !this.PassBoard()) {
            this.Emit(downs, ups, 0, 0, false, 0, 0, wheel)
            this.MoveAbs(absX, absY)
        } else if (hasAbs && this.PassBoard())
            this.Emit(downs, ups, 0, 0, true, absX, absY, wheel)
        else
            this.Emit(downs, ups, dx, dy, false, 0, 0, wheel, counts)
    }

    ; counts: dx and dy are device counts already, so the board does not scale.
    static Emit(downs, ups, dx, dy, abs := false, ax := 0, ay := 0, wheel := 0, counts := false) {
        if (this.outputMode != "software") {
            hidWheel := this.HidWheel(wheel)
            if (!downs.Length && !ups.Length && dx = 0 && dy = 0 && hidWheel = 0 && !abs)
                return
            if (abs)
                this.SendRaw(this.HidJson(downs, ups, 0, 0, true, ax, ay, hidWheel))
            else {
                if !counts
                    this.HidScale(&dx, &dy)
                this.SendRaw(this.HidJson(downs, ups, dx, dy, false, 0, 0, hidWheel))
            }
            for k in downs
                this.QueueHidEcho(k, true)
            for k in ups
                this.QueueHidEcho(k, false)
            return
        }
        parts := ""
        for k in downs
            parts .= SendPiece(this.OutName(k), "down")
        for k in ups
            parts .= SendPiece(this.OutName(k), "up")
        parts .= this.WheelSend(wheel)
        if (parts != "") {
            this.sendNow := Map()
            for k in downs
                this.sendNow[k] := true
            for k in ups
                this.sendNow[k] := true
            try Send("{Blind}" parts)
            ; Leave sendNow set until the next tick so a late $* hotkey is still echo.
            ; Send puts AutoHotkey's hook in front of ours. $* then swallows the
            ; trigger's key-up, and a While pressed macro never sees the release.
            this.RaisePhysicalHook()
        }
        if (dx != 0 || dy != 0)
            try MouseMove(dx, dy, 0, "R")
        if (dx != 0 || dy != 0)
            this.NudgeMouse(dx, dy)
    }

    static MoveAbs(x, y) {
        try MouseMove(Integer(x), Integer(y), 0)
        this.RememberMouse(x, y)
    }

    static NudgeMouse(dx, dy) {
        if !this.mouseAtOk
            return
        this.mouseAtX += Integer(dx)
        this.mouseAtY += Integer(dy)
    }

    ; dy on a scroll step is positive for down. The board wheel is the HID sign, positive for up.
    static HidWheel(dy) {
        return -Round(AsNum(dy, 0))
    }

    static WheelSend(dy) {
        n := Round(AsNum(dy, 0))
        if (n = 0)
            return ""
        if (n > 0)
            return "{WheelDown " n "}"
        return "{WheelUp " Abs(n) "}"
    }

    static WaitSpan(minMs, maxMs, &lo, &hi) {
        lo := Max(0, Round(AsNum(minMs, 0)))
        hi := Max(0, Round(AsNum(maxMs, 0)))
        if (lo > hi) {
            hold := lo
            lo := hi
            hi := hold
        }
    }

    static RandomWaitMs(minMs, maxMs) {
        lo := 0
        hi := 0
        this.WaitSpan(minMs, maxMs, &lo, &hi)
        if (lo = hi)
            return lo
        return Random(lo, hi)
    }

    static HidJson(downs, ups, dx, dy, abs := false, ax := 0, ay := 0, wheel := 0) {
        extra := abs ? ',"abs":true,"ax":' Integer(ax) ',"ay":' Integer(ay) : ""
        return '{"v":1,"type":"hid","down":' this.HidKeys(downs) ',"up":' this.HidKeys(ups) ',"x":' Integer(dx) ',"y":' Integer(dy) ',"wheel":' Integer(wheel) extra '}'
    }

    static HidScale(&dx, &dy) {
        dpi := 96
        hwnd := WinExist("A")
        if hwnd {
            got := DllCall("user32\GetDpiForWindow", "Ptr", hwnd, "UInt")
            if got
                dpi := got
        }
        if (dpi = 96)
            dpi := A_ScreenDPI
        if (dpi = 96)
            return
        dx := Integer(Round(dx * dpi / 96))
        dy := Integer(Round(dy * dpi / 96))
    }

    static HidKeys(list) {
        out := "["
        sep := ""
        for k in list {
            ; Logical names. The shell applies swapClicks when it maps HID mouse bits.
            out .= sep . JSON.Quote(k)
            sep := ","
        }
        return out "]"
    }

    static FlushApplied() {
        ups := []
        for k, _ in this.applied
            ups.Push(k)
        if (!this.dry && ups.Length)
            this.Emit([], ups, 0, 0)
        this.applied := Map()
    }

    static PassBoard() {
        return this.outputMode != "software"
    }

    static RememberMouse(x, y) {
        this.mouseAtX := Integer(x)
        this.mouseAtY := Integer(y)
        this.mouseAtOk := true
    }

    static GotoWhere(v) {
        s := StrLower(Trim(String(v)))
        if (s = "window" || s = "client")
            return s
        return "screen"
    }

    static LocalToScreen(where, x, y, &sx, &sy) {
        sx := Integer(x)
        sy := Integer(y)
        if (where = "screen")
            return true
        ox := 0
        oy := 0
        ow := 0
        oh := 0
        try {
            if (where = "client")
                WinGetClientPos(&ox, &oy, &ow, &oh, "A")
            else
                WinGetPos(&ox, &oy, &ow, &oh, "A")
        } catch
            return false
        sx := ox + Integer(x)
        sy := oy + Integer(y)
        return true
    }

    static MousePos(&x, &y) {
        if (this.PassBoard() && this.mouseAtOk) {
            x := this.mouseAtX
            y := this.mouseAtY
            return
        }
        x := 0
        y := 0
        try MouseGetPos(&x, &y)
    }

    static CursorDelta(tx, ty, &dx, &dy) {
        x := 0
        y := 0
        this.MousePos(&x, &y)
        dx := Integer(tx) - x
        dy := Integer(ty) - y
    }

    static QueueHidEcho(key, down) {
        if !this.PassBoard()
            return
        name := String(key)
        if (name = "")
            return
        this.hidEcho.Push({name: name, folded: StrLower(name), down: down ? 1 : 0, until: this.Now() + 150})
    }

    static MatchHidEcho(name, down) {
        folded := StrLower(String(name))
        want := down ? 1 : 0
        now := this.Now()
        i := 1
        while (i <= this.hidEcho.Length) {
            e := this.hidEcho[i]
            if (e.until < now) {
                this.hidEcho.RemoveAt(i)
                continue
            }
            if (e.folded = folded && e.down = want) {
                this.hidEcho.RemoveAt(i)
                return true
            }
            i++
        }
        return false
    }

    static UnhookKey(key) {
        for prefix in ["$*", "~$*"] {
            spec := prefix (key = "``" ? "``" : key)
            try Hotkey(spec, "Off")
            try Hotkey(spec " up", "Off")
        }
    }

    static ClearHookBindings() {
        for key, _ in this.hooks.Clone() {
            this.UnhookKey(key)
            this.hooks.Delete(key)
            if this.physDown.Has(key)
                this.physDown.Delete(key)
        }
        this.hookFold := Map()
        this.hooksDirty := true
    }

    static ClicksSwapped() {
        v := Field(this.profile, "swapClicks", 0)
        if (v = 1 || v = true)
            return true
        s := StrLower(Trim(String(v)))
        return s = "true" || s = "1"
    }

    static OutName(key) {
        key := SendName(key)
        if !this.ClicksSwapped()
            return key
        if (key = "LButton")
            return "RButton"
        if (key = "RButton")
            return "LButton"
        return key
    }

    static ReleaseAll() {
        this.StopAll()
        this.RemoveDone()
        ups := []
        for k, _ in this.applied
            ups.Push(k)
        if (!this.dry && ups.Length)
            this.Emit([], ups, 0, 0)
        this.applied := Map()
    }

    static StateSig() {
        sig := this.armed ? "1" : "0"
        sig .= "|" this.ActiveExe()
        for r in this.runners {
            if r.done
                continue
            sig .= "|" r.id
        }
        for k, _ in this.applied
            sig .= ">" k
        sig .= "@" this.liveId
        return sig
    }

    static StateJson() {
        running := ""
        for index, r in this.runners {
            if r.done
                continue
            if (running != "")
                running .= ","
            running .= '{"id":' JSON.Quote(r.id) ',"name":' JSON.Quote(r.name) "}"
        }
        held := ""
        for k, _ in this.applied {
            if (held != "")
                held .= ","
            held .= JSON.Quote(k)
        }
        armed := this.armed ? "true" : "false"
        shown := this.liveId = "-" ? String(Field(this.profile, "activeId", "")) : this.liveId
        return '{"v":1,"type":"state","armed":' armed ',"running":[' running '],"held":[' held '],"activeId":' JSON.Quote(shown) ',"front":' JSON.Quote(this.ActiveExe()) ',"windows":' this.WindowListJson() '}'
    }

    static WindowListJson() {
        now := this.Now()
        busy := false
        for r in this.runners {
            if !r.done {
                busy := true
                break
            }
        }
        ttl := busy ? 30000 : 5000
        if (this.winJson != "" && now - this.winAt < ttl)
            return this.winJson
        if (busy && this.winJson != "")
            return this.winJson
        this.winAt := now
        seen := Map()
        out := "["
        sep := ""
        try list := WinGetList()
        catch
            list := []
        for hwnd in list {
            exe := ""
            try exe := WinGetProcessName(hwnd)
            catch
                continue
            if (exe = "")
                continue
            key := StrLower(exe)
            if seen.Has(key)
                continue
            seen[key] := true
            title := ""
            try title := WinGetTitle(hwnd)
            catch
                title := ""
            out .= sep '{"exe":' JSON.Quote(exe) ',"title":' JSON.Quote(title) '}'
            sep := ","
        }
        front := this.ActiveExe()
        if (front != "" && !seen.Has(StrLower(front))) {
            title := ""
            try title := WinGetTitle("A")
            catch
                title := ""
            out .= sep '{"exe":' JSON.Quote(front) ',"title":' JSON.Quote(title) '}'
        }
        this.winJson := out "]"
        return this.winJson
    }

    static Broadcast(force := false) {
        if (!force && !this.pipeOn)
            return
        sig := this.StateSig()
        if (!force && sig = this.lastSig)
            return
        this.lastSig := sig
        json := this.StateJson()
        this.lastState := json
        this.SendRaw(json)
    }

    static OpenPipe() {
        if (this.hPipe && this.hPipe != -1)
            return
        this.hPipe := DllCall("CreateNamedPipeW"
            , "Str", "\\.\pipe\MacroUI"
            , "UInt", 0x3
            , "UInt", 0x7
            , "UInt", 1
            , "UInt", 1048576
            , "UInt", 1048576
            , "UInt", 50
            , "Ptr", 0
            , "Ptr")
        if (!this.hPipe || this.hPipe = -1) {
            this.hPipe := 0
            this.Log("CreateNamedPipe failed")
        }
    }

    static EnsurePipe() {
        if !this.hPipe
            this.OpenPipe()
        if (!this.hPipe || this.pipeOn)
            return
        ok := DllCall("ConnectNamedPipe", "Ptr", this.hPipe, "Ptr", 0)
        err := A_LastError
        if (ok || err = 535) {
            this.pipeOn := true
            armed := this.armed ? "true" : "false"
            this.SendRaw('{"v":1,"type":"hello","armed":' armed '}')
            this.Broadcast(true)
        }
    }

    static DropClient() {
        if !this.pipeOn
            return
        this.pipeOn := false
        this.lastState := ""
        this.lastSig := ""
        if this.hPipe
            DllCall("DisconnectNamedPipe", "Ptr", this.hPipe)
    }

    static PollPipe() {
        this.EnsurePipe()
        if !this.pipeOn
            return
        loop 16 {
            avail := 0
            ok := DllCall("PeekNamedPipe", "Ptr", this.hPipe, "Ptr", 0, "UInt", 0, "Ptr", 0, "UInt*", &avail, "Ptr", 0)
            if !ok {
                this.DropClient()
                return
            }
            if (avail = 0)
                return
            read := 0
            ok := DllCall("ReadFile", "Ptr", this.hPipe, "Ptr", this.readBuf, "UInt", this.readBuf.Size - 1, "UInt*", &read, "Ptr", 0)
            if !ok {
                err := A_LastError
                if (err = 232)
                    return
                this.DropClient()
                return
            }
            if (read <= 0)
                return
            if (read >= this.readBuf.Size)
                read := this.readBuf.Size - 1
            NumPut("UChar", 0, this.readBuf, read)
            this.Handle(StrGet(this.readBuf, "UTF-8"))
        }
    }

    static SendRaw(json) {
        if (!this.pipeOn || this.dry || !this.hPipe)
            return
        if !InStr(json, "`n")
            json .= "`n"
        size := StrPut(json, "UTF-8")
        if (size < 2)
            return
        buf := Buffer(size)
        StrPut(json, buf, "UTF-8")
        ; Message mode takes the whole write or fails with 232. State is sent
        ; again next tick. An error or ack is not, so those get a few more tries.
        tries := InStr(json, '"type":"state"') ? 1 : 4
        loop tries {
            written := 0
            ok := DllCall("WriteFile", "Ptr", this.hPipe, "Ptr", buf, "UInt", size - 1, "UInt*", &written, "Ptr", 0)
            if ok
                return
            err := A_LastError
            if (err != 232) {
                this.DropClient()
                return
            }
        }
    }

    static Handle(text) {
        try msg := JSON.Parse(text)
        catch {
            this.SendRaw('{"v":1,"type":"ack","ok":false,"error":"bad json"}')
            return
        }
        if !(msg is Map)
            return
        type := String(Field(msg, "type", ""))
        if (type = "profile") {
            incoming := Field(msg, "profile", "")
            if (incoming is Map) {
                this.profile := incoming
                this.liveId := Chr(1)
                this.hookSuspect := true
                this.ApplyLiveConfig()
                if this.hooksDirty {
                    this.SyncHooks()
                    this.hooksDirty := false
                }
                this.winAt := -100000
                this.Broadcast(true)
            }
            this.SendRaw('{"v":1,"type":"ack","for":"profile","ok":true}')
            return
        }
        if (type = "output") {
            mode := String(Field(msg, "mode", "software"))
            if (mode != "rp2040" && mode != "rp2350")
                mode := "software"
            if (mode != this.outputMode) {
                this.FlushApplied()
                this.ClearHookBindings()
                this.outputMode := mode
                this.mouseAtOk := false
                this.hooksDirty := true
                this.SyncHooks()
                this.hooksDirty := false
            }
            return
        }
        if (type = "command") {
            action := String(Field(msg, "action", ""))
            if (action = "start")
                this.SetArmed(true)
            else if (action = "stop")
                this.SetArmed(false)
            else if (action = "reload") {
                this.Log("reload")
                this.LoadDisk()
            }
            else if (action = "quit") {
                this.SendRaw('{"v":1,"type":"ack","for":"command","ok":true}')
                ExitApp()
            }
            this.SendRaw('{"v":1,"type":"ack","for":"command","ok":true}')
        }
    }

    ; profiles\engine.log, next to the profile, in the native engine's format.
    ; Only one engine runs at a time, so both write the same file.
    ; The self-test points logPath at a temp file so it never writes the real log.
    static logPath := A_ScriptDir "\..\profiles\engine.log"
    ; The soak reads the lines instead of the file when this is an Array.
    static capture := ""
    static Log(message) {
        if (this.capture is Array) {
            this.capture.Push(message)
            return
        }
        try FileAppend(FormatTime(, "yyyy-MM-dd HH:mm:ss") "." Format("{:03}", A_MSec) " " message "`n", this.logPath, "UTF-8")
    }
}

Check(cond, label, fails) {
    if !cond {
        FileAppend("FAIL " label "`n", "*")
        try FileAppend("FAIL " label "`n", A_ScriptDir "\self-test-result.txt", "UTF-8")
        return fails + 1
    }
    return fails
}

; "unknown" without Raw Input for the keyboard. "dropped" when Raw Input saw a
; key more than 250 ms after the hook's last one. "idle" otherwise.
SilenceVerdict(rawOk, hookAt, rawAt) {
    if !rawOk
        return "unknown"
    if (rawAt > 0 && rawAt > hookAt + 250)
        return "dropped"
    return "idle"
}

HasVal(list, key) {
    if !(list is Array)
        return false
    for item in list
        if (item = key)
            return true
    return false
}

SelfTest() {
    fails := 0
    try FileDelete(A_ScriptDir "\self-test-result.txt")
    Engine.logPath := A_Temp "\macroui-self-test.log"
    try FileDelete(Engine.logPath)
    try fails := RunChecks(fails)
    catch as err
        fails := Check(false, "exception " err.Message " line " err.Line, fails)
    line := (fails ? "FAIL " fails : "PASS") "`n"
    FileAppend(line, "*")
    try FileAppend(line, A_ScriptDir "\self-test-result.txt", "UTF-8")
    return fails ? 1 : 0
}

J(text) {
    return JSON.Parse(StrReplace(text, "'", Chr(34)))
}

RunChecks(fails) {
    fails := Check(StrPut("a", "UTF-8") = 2, "utf8 size", fails)
    fails := Check(HookSpec("a") = "$*a", "hook letter", fails)
    fails := Check(HookSpec("``") = "$*``", "hook backtick", fails)
    Engine.outputMode := "rp2350"
    fails := Check(HookSpec("a") = "~$*a", "board hook passes keys", fails)
    Engine.hidEcho := []
    Engine.QueueHidEcho("b", true)
    fails := Check(Engine.MatchHidEcho("b", true), "hid echo matches a board tap", fails)
    fails := Check(!Engine.MatchHidEcho("b", true), "hid echo is consumed once", fails)
    Engine.outputMode := "software"
    fails := Check(HookSpec("a") = "$*a", "software hook swallows again", fails)
    Engine.profile := Map("focusExe", "___never___.exe")
    fails := Check(!Engine.SwallowTriggers(), "unmatched focus does not swallow", fails)
    fails := Check(HookSpec("a") = "~$*a", "unfocused software hook passes", fails)
    Engine.outputMode := "rp2350"
    fails := Check(HookSpec("a") = "~$*a", "unfocused board hook still passes", fails)
    Engine.outputMode := "software"
    Engine.profile := Map()
    Engine.frontKnown := true
    Engine.frontExe := "notepad.exe"
    fails := Check(Engine.SwallowTriggers(), "no focus restriction swallows", fails)
    Engine.frontExe := "MacroShell.exe"
    fails := Check(!Engine.SwallowTriggers(), "the editor window does not swallow keys", fails)
    Engine.frontExe := "notepad.exe"
    fails := Check(HookSpec("a") = "$*a", "software swallows without focus exe", fails)
    Engine.frontKnown := false
    Engine.frontExe := ""
    liveConfigs := J("[{'id':'home','focusExe':'','macros':[]},{'id':'game','focusExe':'Game.exe','macros':[]}]")
    fails := Check(String(Field(Engine.PickLive(liveConfigs, "notepad.exe", "home"), "id", "")) = "home", "foreground miss uses empty focus", fails)
    fails := Check(String(Field(Engine.PickLive(liveConfigs, "Game.exe", "home"), "id", "")) = "game", "foreground exe picks that config", fails)
    fails := Check(String(Field(Engine.PickLive(liveConfigs, "Game-Win64-Shipping.exe", "home"), "id", "")) = "game", "shipping exe still matches the stem", fails)
    fails := Check(Engine.PickLive(J("[{'id':'game','focusExe':'Game.exe','macros':[]}]"), "notepad.exe", "game") = "", "no fallback means nothing runs", fails)
    fails := Check(SendToken("``", "down") = "{```` down}", "send backtick", fails)
    fails := Check(SendToken("q", "up") = "{q up}", "send letter", fails)
    fails := Check(SafeKey("|") = "\", "pipe key is backslash", fails)
    fails := Check(Engine.GotoWhere("Client") = "client", "goto client", fails)
    fails := Check(Engine.GotoWhere("") = "screen", "goto screen default", fails)
    fails := Check(Engine.FocusMatch("Dawnwalker.exe", "Dawnwalker-Win64-Shipping.exe"), "ue shipping matches editor exe", fails)
    fails := Check(Engine.FocusMatch("Dawnwalker-Win64-Shipping.exe", "Dawnwalker.exe"), "ue editor matches shipping exe", fails)
    fails := Check(!Engine.FocusMatch("notepad.exe", "notepad++.exe"), "notepad does not match notepad++", fails)
    fails := Check(!Engine.FocusMatch("chrome.exe", "Dawnwalker.exe"), "unrelated exe does not match", fails)
    Engine.profile := Map("swapClicks", 1)
    hid := Engine.HidJson(["LButton", "w"], ["space"], 4, -3)
    fails := Check(InStr(hid, '"LButton"') && InStr(hid, '"w"') && InStr(hid, '"space"') && InStr(hid, '"x":4') && InStr(hid, '"y":-3') && InStr(hid, '"type":"hid"') && !InStr(hid, '"RButton"'), "hid json keeps logical names", fails)
    absHid := Engine.HidJson([], [], 0, 0, true, 1565, 75)
    fails := Check(InStr(absHid, '"abs":true') && InStr(absHid, '"ax":1565') && InStr(absHid, '"ay":75'), "hid json abs goto", fails)
    fails := Check(Engine.WheelSend(3) = "{WheelDown 3}", "scroll down", fails)
    fails := Check(Engine.WheelSend(-3) = "{WheelUp 3}", "scroll up", fails)
    fails := Check(Engine.WheelSend(0) = "", "zero scroll sends nothing", fails)
    fails := Check(Engine.HidWheel(-3) = 3, "scroll up is a positive board wheel", fails)
    fails := Check(Engine.HidWheel(3) = -3, "scroll down is a negative board wheel", fails)
    spanLo := 0
    spanHi := 0
    Engine.WaitSpan(70, 40, &spanLo, &spanHi)
    fails := Check(spanLo = 40 && spanHi = 70, "random wait swaps the range", fails)
    Engine.WaitSpan(-8, 12, &spanLo, &spanHi)
    fails := Check(spanLo = 0 && spanHi = 12, "random wait clamps the range", fails)
    fails := Check(Engine.RandomWaitMs(15, 15) = 15, "random wait uses a fixed range", fails)
    loop 30 {
        rolled := Engine.RandomWaitMs(70, 40)
        if (rolled < 40 || rolled > 70) {
            fails := Check(false, "random wait stays inside the range", fails)
            break
        }
    }
    fails := Check(Engine.OutName("LButton") = "RButton", "swap left sends right", fails)
    fails := Check(Engine.OutName("RButton") = "LButton", "swap right sends left", fails)
    fails := Check(Engine.OutName("MButton") = "MButton", "swap leaves middle", fails)
    Engine.profile := J("{'swapClicks':true}")
    fails := Check(Engine.OutName("LButton") = "RButton", "json true swaps left", fails)
    Engine.profile := Map()
    fails := Check(Engine.OutName("LButton") = "LButton", "swap off leaves left", fails)
    sideBasic := J("{'id':'m4','name':'M4','enabled':true,'basic':true,'gapMs':5,'playMode':'whileHeld','trigger':{'kind':'side','button':'XButton1'},'steps':[]}")
    sidePrep := Engine.PrepareBasic(sideBasic)
    sideSteps := sidePrep["steps"]
    fails := Check(sideSteps.Length = 4 && sideSteps[1]["type"] = "mouse" && sideSteps[1]["button"] = "XButton1", "side button basic clicks that button", fails)
    leftBasic := J("{'id':'lb','name':'Left','enabled':true,'basic':true,'playMode':'once','trigger':{'kind':'mouse','button':'LButton'},'steps':[]}")
    leftPrep := Engine.PrepareBasic(leftBasic)
    fails := Check(leftPrep["steps"].Length = 0, "left click is not a macro trigger", fails)
    hooked := false
    try {
        Hotkey(HookSpec("F24"), TriggerHook, "On")
        hooked := true
        Hotkey(HookSpec("F24"), "Off")
        hooked := false
    } catch as err {
        fails := Check(false, "hook register " err.Message, fails)
        if hooked
            try Hotkey(HookSpec("F24"), "Off")
    }
    TriggerHook("$*b")
    fails := Check(Engine.physDown.Has("b") && Engine.physDown["b"], "physical press tracked", fails)
    TriggerHook("$*b up")
    fails := Check(Engine.physDown.Has("b") && !Engine.physDown["b"], "physical release tracked", fails)
    Engine.hooks["b"] := true
    fails := Check(!Engine.KeyDown("b"), "hooked key follows the real release", fails)
    TriggerHook("$*b")
    fails := Check(Engine.KeyDown("b"), "hooked key follows the real press", fails)
    Engine.hooks.Delete("b")
    Engine.physDown.Delete("b")
    Engine.BuildVkMap()
    Engine.hooks["b"] := true
    info := Buffer(32, 0)
    NumPut("UInt", 0x42, info, 0)
    NumPut("UInt", 0x10, info, 8)
    Engine.NotePhysical("b", true)
    Engine.ReadKey(0x100, info)
    fails := Check(Engine.KeyDown("b"), "a sent key cannot press the trigger", fails)
    NumPut("UInt", 0, info, 8)
    Engine.ReadKey(0x100, info)
    fails := Check(Engine.KeyDown("b"), "physical down is left to the hotkey", fails)
    NumPut("UInt", 0x80, info, 8)
    Engine.ReadKey(0x101, info)
    fails := Check(!Engine.KeyDown("b"), "physical release clears a tap", fails)
    Engine.hooks.Delete("b")
    Engine.physDown.Delete("b")
    Engine.BuildVkMap()
    fails := Check(Engine.vkName.Has(0x42) && Engine.vkName[0x42] = "b", "vk b", fails)
    fails := Check(Engine.vkName[0x20] = "space", "vk space", fails)
    fails := Check(Engine.vkName[0x70] = "f1", "vk f1", fails)
    fails := Check(Engine.vkName[0xA0] = "LShift", "vk left shift", fails)
    fails := Check(Engine.vkName[0xA1] = "RShift", "vk right shift", fails)
    fails := Check(NormKey("shift") = "LShift", "shift means left shift", fails)
    fails := Check(NormKey("RShift") = "RShift", "right shift stays right", fails)
    fails := Check(Engine.SameKey("shift", "LShift"), "old shift matches LShift", fails)
    fails := Check(!Engine.SameKey("LShift", "RShift"), "left and right shift differ", fails)
    fails := Check(SideKey(0x10, 0x36, 0) = "RShift", "right scan is RShift", fails)
    fails := Check(SideKey(0x11, 0x1D, 1) = "RCtrl", "extended ctrl is RCtrl", fails)
    fails := Check(HookSpec("LCtrl") = "$*LControl", "ctrl hook uses LControl", fails)
    fails := Check(Engine.TriggerAction("once", true, false, false) = "start", "once starts on press", fails)
    fails := Check(Engine.TriggerAction("once", true, true, true) = "", "once ignores hold", fails)
    fails := Check(Engine.TriggerAction("once", false, true, true) = "", "once ignores release", fails)
    fails := Check(Engine.TriggerAction("repeat", true, false, false) = "start", "repeat starts on press", fails)
    fails := Check(Engine.TriggerAction("whileHeld", true, false, false) = "start", "while pressed starts", fails)
    fails := Check(Engine.TriggerAction("whileHeld", true, true, true) = "", "while pressed stays", fails)
    fails := Check(Engine.TriggerAction("whileHeld", false, true, true) = "stop", "while pressed stops on release", fails)
    fails := Check(Engine.TriggerAction("whileHeld", false, false, true) = "stop", "while pressed stops if the release was missed", fails)
    fails := Check(Engine.TriggerAction("toggle", true, false, false) = "start", "toggle starts", fails)
    fails := Check(Engine.TriggerAction("toggle", true, false, true) = "stop", "toggle stops", fails)
    fails := Check(Engine.TriggerAction("toggle", false, true, true) = "", "toggle ignores release", fails)
    fails := Check(Engine.TriggerAction("onRelease", true, false, false) = "", "on release ignores press", fails)
    fails := Check(Engine.TriggerAction("onRelease", false, true, false) = "start", "on release starts", fails)

    parsed := J("{'version':1,'name':'Default','macros':[{'id':'sprint-jump','name':'Sprint jump','enabled':true,'playMode':'whileHeld','exclusive':false,'trigger':{'kind':'side','button':'XButton1'},'steps':[{'type':'key','action':'down','key':'w'},{'type':'repeat','count':0,'steps':[{'type':'key','action':'tap','key':'space','holdMs':20},{'type':'wait','ms':50}]}]},{'id':'click-burst','name':'Click burst','enabled':true,'playMode':'once','exclusive':false,'trigger':{'kind':'key','button':'f'},'steps':[{'type':'mouse','action':'tap','button':'LButton','holdMs':15},{'type':'wait','ms':40},{'type':'mouse','action':'tap','button':'LButton','holdMs':15},{'type':'wait','ms':40},{'type':'mouse','action':'tap','button':'LButton','holdMs':15}]}]}")
    fails := Check(parsed["name"] = "Default", "profile name", fails)
    fails := Check(parsed["macros"].Length = 2, "macro count", fails)
    fails := Check(parsed["macros"][1]["steps"][2]["count"] = 0, "infinite repeat", fails)
    fails := Check(Field(parsed["macros"][1], "exclusive", true) = 0, "exclusive false", fails)

    Engine.dry := true
    Engine.profile := parsed
    Engine.armed := true
    Engine.StartRunner(Engine.FindMacro("sprint-jump"))
    Engine.FlushPending()
    sprint := Engine.runners[1]
    sprint.Advance(0)
    want := Engine.Desired(0)
    fails := Check(want.Has("w") && want.Has("space"), "t0 holds w and space", fails)
    sprint.Advance(20)
    want := Engine.Desired(20)
    fails := Check(want.Has("w") && !want.Has("space"), "t20 space released w stays", fails)
    sprint.Advance(50)
    want := Engine.Desired(50)
    fails := Check(want.Has("w") && want.Has("space"), "t50 space taps again", fails)

    Engine.StartRunner(Engine.FindMacro("click-burst"))
    Engine.FlushPending()
    fails := Check(Engine.runners.Length = 2, "both macros running", fails)
    Engine.runners[2].Advance(50)
    want := Engine.Desired(50)
    fails := Check(want.Has("w") && want.Has("LButton"), "click does not drop w", fails)
    Engine.StopRunner("click-burst")
    Engine.RemoveDone()
    want := Engine.Desired(50)
    fails := Check(want.Has("w") && !want.Has("LButton") && Engine.runners.Length = 1, "stopping click leaves sprint", fails)
    Engine.Publish(50)
    fails := Check(Engine.applied.Has("w") && Engine.applied.Has("space"), "merger pressed w and space", fails)
    Engine.StopRunner("sprint-jump")
    Engine.RemoveDone()
    Engine.Publish(51)
    fails := Check(!Engine.applied.Has("w") && !Engine.applied.Has("space") && Engine.runners.Length = 0, "stopping sprint releases w", fails)

    once := J("{'id':'once-key','name':'Once','enabled':true,'basic':true,'gapMs':5,'playMode':'once','repeatCount':1,'trigger':{'kind':'key','button':'q'},'steps':[{'type':'key','action':'tap','key':'q','holdMs':18},{'type':'wait','ms':18}]}")
    Engine.StartRunner(once)
    Engine.FlushPending()
    play := Engine.runners[1]
    built := play.stack[1].steps
    fails := Check(built.Length = 4 && built[1]["action"] = "down" && built[2]["ms"] = 18 && built[3]["action"] = "up" && built[4]["ms"] = 5, "repeat ms is the gap after an 18 ms hold", fails)
    play.Advance(0)
    fails := Check(Engine.Desired(0).Has("q"), "basic key is down", fails)
    play.Advance(17)
    fails := Check(Engine.Desired(17).Has("q"), "basic key stays for the 18 ms hold", fails)
    play.Advance(18)
    fails := Check(!Engine.Desired(18).Has("q") && !play.done, "basic key releases before the next repeat", fails)
    play.Advance(23)
    fails := Check(play.done && !Engine.Desired(23).Has("q"), "play once finishes after one press", fails)
    Engine.RemoveDone()

    slow := J("{'id':'slow-key','name':'Slow','enabled':true,'basic':true,'gapMs':40,'playMode':'once','repeatCount':1,'trigger':{'kind':'key','button':'b'},'steps':[]}")
    Engine.StartRunner(slow)
    Engine.FlushPending()
    slowSteps := Engine.runners[1].stack[1].steps
    fails := Check(slowSteps[2]["ms"] = 18 && slowSteps[4]["ms"] = 40, "repeat ms stays the gap after the hold", fails)
    Engine.StopAll()
    Engine.RemoveDone()

    repeat := J("{'id':'repeat-key','name':'Repeat','enabled':true,'basic':false,'playMode':'repeat','repeatCount':3,'trigger':{'kind':'key','button':'e'},'steps':[{'type':'wait','ms':10}]}")
    Engine.StartRunner(repeat)
    Engine.FlushPending()
    rep := Engine.runners[1]
    rep.Advance(0)
    fails := Check(!rep.done, "repeat still going after 1", fails)
    rep.Advance(10)
    fails := Check(!rep.done, "repeat still going after 2", fails)
    rep.Advance(20)
    fails := Check(!rep.done, "repeat still going after 3", fails)
    rep.Advance(30)
    fails := Check(rep.done, "repeat stops after the count", fails)
    Engine.RemoveDone()

    held := J("{'id':'held-key','name':'Held','enabled':true,'basic':false,'playMode':'whileHeld','trigger':{'kind':'key','button':'r'},'steps':[{'type':'key','action':'down','key':'w'}]}")
    Engine.StartRunner(held)
    Engine.FlushPending()
    looped := Engine.runners[1]
    looped.Advance(0)
    looped.Advance(50)
    fails := Check(!looped.done && Engine.Desired(50).Has("w"), "while pressed keeps holding", fails)
    Engine.StopRunner("held-key")
    Engine.RemoveDone()
    fails := Check(!Engine.Desired(50).Has("w"), "while pressed releases on stop", fails)

    rec := J("{'id':'rec-held','name':'Rec','enabled':true,'basic':false,'playMode':'whileHeld','trigger':{'kind':'key','button':'b'},'steps':[{'type':'key','action':'down','key':'b'},{'type':'wait','ms':18},{'type':'key','action':'up','key':'b'},{'type':'wait','ms':5}]}")
    savedProfile := Engine.profile
    Engine.profile := Map("macros", [rec])
    Engine.prevDown := Map()
    Engine.hooks["b"] := true
    Engine.NotePhysical("b", true)
    Engine.PollTriggers()
    Engine.FlushPending()
    fails := Check(Engine.HasRunner("rec-held"), "recorded while held starts", fails)
    Engine.runners[1].Advance(0)
    fails := Check(Engine.Desired(0).Has("b"), "recorded macro sends its own trigger", fails)
    sent := Buffer(32, 0)
    NumPut("UInt", 0x42, sent, 0)
    NumPut("UInt", 0x10, sent, 8)
    Engine.ReadKey(0x100, sent)
    fails := Check(Engine.KeyDown("b"), "sent trigger stays ignored while the finger is down", fails)
    NumPut("UInt", 0x80, sent, 8)
    Engine.ReadKey(0x101, sent)
    Engine.PollTriggers()
    Engine.RemoveDone()
    fails := Check(!Engine.HasRunner("rec-held"), "recorded while held stops when the finger lets go", fails)
    Engine.hooks["LButton"] := true
    Engine.NotePhysical("LButton", true)
    Engine.ReadMouse(0x202, Buffer(32, 0))
    fails := Check(!Engine.KeyDown("LButton"), "recorded mouse trigger clears on the real release", fails)
    Engine.hooks["XButton1"] := true
    Engine.NotePhysical("XButton1", true)
    echoUp := Buffer(32, 0)
    NumPut("UInt", 1 << 16, echoUp, 8)
    NumPut("Ptr", Engine.echo, echoUp, A_PtrSize = 8 ? 24 : 20)
    Engine.ReadMouse(0x20C, echoUp)
    fails := Check(Engine.KeyDown("XButton1"), "a sent mouse button is not a physical release", fails)
    savedHook := Engine.hMouseHook
    Engine.hMouseHook := 1
    TriggerHook("$*XButton1 up")
    fails := Check(Engine.KeyDown("XButton1"), "the mouse hotkey ignores buttons this macro sends", fails)
    Engine.hMouseHook := savedHook
    realUp := Buffer(32, 0)
    NumPut("UInt", 1 << 16, realUp, 8)
    Engine.ReadMouse(0x20C, realUp)
    fails := Check(!Engine.KeyDown("XButton1"), "a real mouse release still ends the hold", fails)
    Engine.hooks.Delete("XButton1")
    Engine.physDown.Delete("XButton1")
    Engine.StopAll()
    Engine.RemoveDone()
    Engine.hooks.Delete("b")
    Engine.hooks.Delete("LButton")
    Engine.physDown.Delete("b")
    Engine.physDown.Delete("LButton")
    Engine.profile := savedProfile

    flash := J("{'id':'flash-key','name':'Flash','enabled':true,'basic':false,'playMode':'once','trigger':{'kind':'key','button':'t'},'steps':[{'type':'key','action':'down','key':'a'},{'type':'key','action':'up','key':'a'}]}")
    Engine.StartRunner(flash)
    Engine.FlushPending()
    tap := Engine.runners[1]
    tap.Advance(0)
    fails := Check(Engine.Desired(0).Has("a") && !tap.done, "zero delay press is sent", fails)
    tap.Advance(1)
    fails := Check(!Engine.Desired(1).Has("a") && tap.done, "zero delay press releases", fails)

    Engine.StopAll()
    Engine.RemoveDone()

    adv := J("{'id':'adv-n','name':'N','enabled':true,'advanced':true,'busy':true,'playMode':'whileHeld','trigger':{'kind':'key','button':'b'},'steps':[],'blocks':[{'type':'whileHeld','steps':[{'type':'wait','ms':26}]},{'type':'ifShort','underMs':150,'minCycles':3,'steps':[{'type':'wait','ms':26}]},{'type':'then','forMs':0,'steps':[{'type':'key','action':'down','key':'9'},{'type':'wait','ms':10}]}]}")
    Engine.hooks["b"] := true
    Engine.NotePhysical("b", true)
    Engine.profile := Map("macros", [adv])
    Engine.prevDown["adv-n"] := false
    Engine.PollTriggers()
    Engine.FlushPending()
    fails := Check(Engine.HasRunner("adv-n"), "advanced starts on press", fails)
    play := Engine.runners[1]
    play.Advance(0)
    play.Advance(26)
    Engine.NotePhysical("b", false)
    Engine.PollTriggers()
    Engine.RemoveDone()
    fails := Check(Engine.HasRunner("adv-n"), "advanced keeps going after release", fails)
    play.Advance(52)
    fails := Check(play.cycles = 2 && !Engine.Desired(52).Has("9"), "short hold counted two cycles", fails)
    play.Advance(78)
    fails := Check(Engine.Desired(78).Has("9"), "then runs after the short tap is topped up", fails)
    Engine.StopAll()
    Engine.RemoveDone()
    Engine.NotePhysical("b", true)
    Engine.StartRunner(adv)
    Engine.FlushPending()
    long := Engine.runners[1]
    long.Advance(0)
    long.Advance(160)
    Engine.NotePhysical("b", false)
    long.Advance(186)
    fails := Check(long.cycles = 2 && Engine.Desired(186).Has("9"), "long hold skips the extra cycles and runs then", fails)
    Engine.StopAll()
    Engine.RemoveDone()
    Engine.NotePhysical("b", true)
    Engine.prevDown["adv-n"] := false
    Engine.profile := Map("macros", [adv])
    Engine.PollTriggers()
    Engine.FlushPending()
    Engine.prevDown["adv-n"] := false
    Engine.PollTriggers()
    Engine.FlushPending()
    fails := Check(Engine.runners.Length = 1, "busy ignores a second press", fails)
    Engine.StopAll()
    Engine.RemoveDone()
    Engine.hooks.Delete("b")
    Engine.physDown.Delete("b")
    Engine.profile := savedProfile

    split := J("{'id':'split-b','name':'Split','enabled':true,'advanced':true,'playMode':'whileHeld','trigger':{'kind':'key','button':'b'},'steps':[],'blocks':[{'type':'ifShort','underMs':130,'minCycles':1,'steps':[{'type':'key','action':'down','key':'o'},{'type':'wait','ms':10}]},{'type':'whileHeld','steps':[{'type':'key','action':'down','key':'x'},{'type':'wait','ms':10}]}]}")
    Engine.hooks["b"] := true
    Engine.profile := Map("macros", [split])
    Engine.NotePhysical("b", true)
    Engine.prevDown["split-b"] := false
    Engine.PollTriggers()
    Engine.FlushPending()
    quick := Engine.runners[1]
    quick.Advance(0)
    fails := Check(!Engine.Desired(0).Has("o") && !Engine.Desired(0).Has("x"), "if-released-first waits before sending", fails)
    Engine.NotePhysical("b", false)
    Engine.PollTriggers()
    quick.Advance(40)
    fails := Check(Engine.Desired(40).Has("o") && !Engine.Desired(40).Has("x"), "a quick tap sends the short path once", fails)
    quick.Advance(50)
    Engine.RemoveDone()
    fails := Check(!Engine.HasRunner("split-b") && !Engine.Desired(50).Has("x"), "a quick tap does not run the hold loop", fails)
    Engine.NotePhysical("b", true)
    Engine.prevDown["split-b"] := false
    Engine.PollTriggers()
    Engine.FlushPending()
    heldSplit := Engine.runners[1]
    heldSplit.Advance(0)
    heldSplit.Advance(130)
    fails := Check(!Engine.Desired(130).Has("o") && Engine.Desired(130).Has("x"), "holding past the window starts the hold loop", fails)
    Engine.StopAll()
    Engine.RemoveDone()
    Engine.hooks.Delete("b")
    Engine.physDown.Delete("b")
    Engine.profile := savedProfile

    Engine.muted := Map()
    hot := J("{'id':'hot','name':'Hot','enabled':true,'playMode':'toggle','trigger':{'kind':'key','button':'f'},'steps':[{'type':'wait','ms':5000}]}")
    idle := J("{'id':'idle','name':'Idle','enabled':true,'playMode':'toggle','trigger':{'kind':'key','button':'g'},'steps':[{'type':'wait','ms':5000}]}")
    spam := J("{'id':'spam','name':'Spam','enabled':true,'advanced':true,'playMode':'once','trigger':{'kind':'key','button':'b'},'steps':[],'blocks':[{'type':'whileHeld','mute':['hot','idle','spam'],'steps':[]}]}")
    Engine.profile := Map("macros", [hot, idle, spam])
    Engine.hooks["b"] := true
    Engine.hooks["f"] := true
    Engine.NotePhysical("b", false)
    Engine.NotePhysical("f", false)
    Engine.StartRunner(hot)
    Engine.FlushPending()
    fails := Check(Engine.HasRunner("hot") && !Engine.HasRunner("idle"), "mute target was running", fails)
    Engine.NotePhysical("b", true)
    Engine.StartRunner(spam)
    Engine.FlushPending()
    play := ""
    for r in Engine.runners
        if (r.id = "spam" && !r.done)
            play := r
    play.Advance(0)
    hotPaused := false
    for r in Engine.runners
        if (r.id = "hot" && !r.done && r.paused)
            hotPaused := true
    fails := Check(Engine.IsMuted("hot") && Engine.IsMuted("idle") && !Engine.IsMuted("spam") && hotPaused, "while held pauses the other macros", fails)
    Engine.NotePhysical("f", true)
    Engine.prevDown["hot"] := false
    Engine.prevDown["spam"] := true
    Engine.PollTriggers()
    Engine.FlushPending()
    hotLive := 0
    for r in Engine.runners
        if (r.id = "hot" && !r.done && !r.paused)
            hotLive++
    fails := Check(hotLive = 0, "muted macro does not start", fails)
    Engine.NotePhysical("b", false)
    play.Advance(200)
    Engine.FlushPending()
    hotOn := false
    for r in Engine.runners
        if (r.id = "hot" && !r.done && !r.paused)
            hotOn := true
    fails := Check(!Engine.IsMuted("hot") && !Engine.IsMuted("idle") && hotOn && !Engine.HasRunner("idle"), "release restores only the macro that was running", fails)
    Engine.PollTriggers()
    Engine.FlushPending()
    hotCount := 0
    for r in Engine.runners
        if (r.id = "hot" && !r.done)
            hotCount++
    fails := Check(hotCount = 1, "a key that stayed down does not start a second copy", fails)
    Engine.StopAll()
    Engine.RemoveDone()
    Engine.muted := Map()
    Engine.hooks.Delete("b")
    Engine.hooks.Delete("f")
    Engine.physDown.Delete("b")
    Engine.physDown.Delete("f")
    Engine.profile := savedProfile

    Engine.muted := Map()
    shot := J("{'id':'shot','name':'Shot','enabled':true,'advanced':true,'playMode':'once','trigger':{'kind':'key','button':'v'},'steps':[],'blocks':[{'type':'whileHeld','steps':[{'type':'key','action':'down','key':'x'},{'type':'wait','ms':20}]}]}")
    wall := J("{'id':'wall','name':'Wall','enabled':true,'advanced':true,'playMode':'once','trigger':{'kind':'key','button':'n'},'steps':[],'blocks':[{'type':'whileHeld','mute':['shot'],'steps':[{'type':'wait','ms':50}]}]}")
    Engine.profile := Map("macros", [shot, wall])
    Engine.hooks["v"] := true
    Engine.hooks["n"] := true
    Engine.NotePhysical("v", true)
    Engine.NotePhysical("n", false)
    Engine.prevDown["shot"] := false
    Engine.prevDown["wall"] := false
    Engine.PollTriggers()
    Engine.FlushPending()
    shotRun := ""
    for r in Engine.runners
        if (r.id = "shot" && !r.done)
            shotRun := r
    shotRun.Advance(0)
    fails := Check(Engine.Desired(0).Has("x") && !shotRun.paused, "shotgun plays while its key is held", fails)
    Engine.NotePhysical("n", true)
    Engine.prevDown["wall"] := false
    Engine.PollTriggers()
    Engine.FlushPending()
    wallRun := ""
    for r in Engine.runners
        if (r.id = "wall" && !r.done)
            wallRun := r
    wallRun.Advance(0)
    fails := Check(Engine.IsMuted("shot") && shotRun.paused && !Engine.Desired(0).Has("x"), "wall pauses shotgun and drops its keys", fails)
    Engine.NotePhysical("n", false)
    wallRun.Advance(80)
    Engine.FlushPending()
    fails := Check(!Engine.IsMuted("shot") && !shotRun.paused && !shotRun.done, "releasing wall unpauses shotgun", fails)
    shotRun.Advance(80)
    fails := Check(Engine.Desired(80).Has("x"), "shotgun plays again while its key is still held", fails)
    Engine.StopAll()
    Engine.RemoveDone()
    Engine.muted := Map()
    Engine.NotePhysical("v", true)
    Engine.NotePhysical("n", false)
    Engine.prevDown["shot"] := false
    Engine.prevDown["wall"] := false
    Engine.PollTriggers()
    Engine.FlushPending()
    shotRun := ""
    for r in Engine.runners
        if (r.id = "shot" && !r.done)
            shotRun := r
    shotRun.Advance(1000)
    Engine.NotePhysical("n", true)
    Engine.prevDown["wall"] := false
    Engine.PollTriggers()
    Engine.FlushPending()
    wallRun := ""
    for r in Engine.runners
        if (r.id = "wall" && !r.done)
            wallRun := r
    wallRun.Advance(1000)
    Engine.NotePhysical("v", false)
    Engine.NotePhysical("n", false)
    wallRun.Advance(1100)
    Engine.FlushPending()
    shotRun.Advance(1100)
    fails := Check(shotRun.done && !Engine.Desired(1100).Has("x"), "shotgun stays stopped if its key came up during the pause", fails)
    Engine.StopAll()
    Engine.RemoveDone()
    Engine.muted := Map()
    Engine.NotePhysical("v", false)
    Engine.NotePhysical("n", true)
    Engine.prevDown["shot"] := false
    Engine.prevDown["wall"] := false
    Engine.PollTriggers()
    Engine.FlushPending()
    wallRun := ""
    for r in Engine.runners
        if (r.id = "wall" && !r.done)
            wallRun := r
    wallRun.Advance(2000)
    Engine.NotePhysical("v", true)
    Engine.PollTriggers()
    Engine.FlushPending()
    fails := Check(Engine.IsMuted("shot") && !Engine.HasRunner("shot"), "a held key does not start while muted", fails)
    Engine.NotePhysical("n", false)
    wallRun.Advance(2100)
    Engine.FlushPending()
    shotRun := ""
    for r in Engine.runners
        if (r.id = "shot" && !r.done)
            shotRun := r
    fails := Check(shotRun != "", "a key that stayed down starts when the mute lifts", fails)
    if (shotRun != "")
        shotRun.Advance(2100)
    fails := Check(Engine.Desired(2100).Has("x"), "that restarted hold sends again", fails)
    Engine.StopAll()
    Engine.RemoveDone()
    Engine.muted := Map()
    Engine.hooks.Delete("v")
    Engine.hooks.Delete("n")
    Engine.physDown.Delete("v")
    Engine.physDown.Delete("n")
    Engine.profile := savedProfile

    tap := J("{'id':'tap-z','name':'Tap','enabled':true,'advanced':true,'playMode':'whileHeld','trigger':{'kind':'side','button':'XButton1'},'steps':[],'blocks':[{'type':'tapHold','key':'z','watch':['t'],'armMs':5,'gapMs':80}]}")
    Engine.hooks["XButton1"] := true
    Engine.NotePhysical("XButton1", true)
    Engine.finger["t"] := false
    Engine.StartRunner(tap)
    Engine.FlushPending()
    held := Engine.runners[1]
    held.Advance(0)
    fails := Check(Engine.Desired(0).Has("z") && Engine.Desired(0).Has("XButton1"), "tap hold presses z and the trigger", fails)
    held.Advance(5)
    fails := Check(Engine.Desired(5).Has("z") && Engine.Desired(5).Has("XButton1"), "tap hold stays down while the watched key is up", fails)
    Engine.sent := []
    Engine.applied := Map()
    Engine.Publish(5)
    first := Engine.sent.Length ? Engine.sent[1]["down"] : []
    fails := Check(HasVal(first, "z"), "tap hold sends z down", fails)
    held.Advance(400)
    Engine.Publish(400)
    again := Engine.sent.Length ? Engine.sent[-1]["down"] : []
    fails := Check(HasVal(again, "z"), "tap hold repeats z like a windows hold", fails)
    Engine.finger["t"] := true
    held.Advance(410)
    fails := Check(Engine.Desired(410).Has("z") && Engine.Desired(410).Has("XButton1"), "tap hold waits before the release", fails)
    held.Advance(415)
    fails := Check(!Engine.Desired(415).Has("z") && !Engine.Desired(415).Has("XButton1"), "tap hold releases z and the trigger", fails)
    held.Advance(495)
    fails := Check(Engine.Desired(495).Has("z") && !Engine.Desired(495).Has("XButton1"), "tap hold presses z again while the trigger is still held", fails)
    Engine.StopAll()
    Engine.RemoveDone()
    Engine.NotePhysical("XButton1", true)
    Engine.finger["t"] := true
    Engine.StartRunner(tap)
    Engine.FlushPending()
    dropped := Engine.runners[1]
    dropped.Advance(0)
    dropped.Advance(5)
    dropped.Advance(10)
    dropped.Advance(15)
    Engine.NotePhysical("XButton1", false)
    dropped.Advance(95)
    fails := Check(dropped.done && !Engine.Desired(95).Has("z"), "letting go during the gap does not press z again", fails)
    Engine.StopAll()
    Engine.RemoveDone()
    fails := Check(!Engine.Desired(95).Has("z") && !Engine.Desired(95).Has("XButton1"), "physical release clears z", fails)
    Engine.NotePhysical("XButton1", true)
    Engine.finger["t"] := false
    Engine.StartRunner(tap)
    Engine.FlushPending()
    sprintHold := Engine.runners[1]
    sprintHold.Advance(0)
    sprintHold.Advance(5)
    fails := Check(Engine.Desired(5).Has("z") && Engine.Desired(5).Has("XButton1"), "sprint stays down before release", fails)
    Engine.NotePhysical("XButton1", false)
    sprintHold.Advance(6)
    fails := Check(sprintHold.done && !Engine.Desired(6).Has("z") && !Engine.Desired(6).Has("XButton1"), "releasing sprint while holding stops the repress", fails)
    Engine.StopAll()
    Engine.RemoveDone()
    Engine.hooks.Delete("XButton1")
    Engine.physDown.Delete("XButton1")
    Engine.finger := Map()

    spam := J("{'id':'spam-l','name':'Spam','enabled':true,'advanced':true,'playMode':'whileHeld','trigger':{'kind':'side','button':'XButton2'},'steps':[],'blocks':[{'type':'tapSpam','key':'LButton','watch':['RButton'],'armMs':5,'gapMs':50,'holdMs':18,'restMs':18}]}")
    Engine.hooks["XButton2"] := true
    Engine.NotePhysical("XButton2", true)
    Engine.finger["RButton"] := false
    Engine.StartRunner(spam)
    Engine.FlushPending()
    click := Engine.runners[1]
    click.Advance(0)
    fails := Check(Engine.Desired(0).Has("LButton") && Engine.Desired(0).Has("XButton2"), "spam starts with the click and the trigger down", fails)
    click.Advance(15)
    fails := Check(Engine.Desired(15).Has("LButton") && Engine.Desired(15).Has("XButton2"), "spam stays down through the click", fails)
    click.Advance(20)
    fails := Check(!Engine.Desired(20).Has("LButton") && Engine.Desired(20).Has("XButton2"), "spam lets the click up and keeps the trigger", fails)
    click.Advance(40)
    fails := Check(Engine.Desired(40).Has("LButton") && Engine.Desired(40).Has("XButton2"), "spam clicks again while the trigger is held", fails)
    Engine.finger["RButton"] := true
    click.Advance(45)
    fails := Check(!Engine.Desired(45).Has("LButton") && Engine.Desired(45).Has("XButton2"), "a tracked press stops the clicks and keeps the trigger", fails)
    click.Advance(70)
    fails := Check(!Engine.Desired(70).Has("LButton") && Engine.Desired(70).Has("XButton2"), "spam stays quiet for the pause", fails)
    click.Advance(95)
    fails := Check(Engine.Desired(95).Has("LButton") && Engine.Desired(95).Has("XButton2"), "spam clicks again after the pause while still held", fails)
    Engine.NotePhysical("XButton2", false)
    click.Advance(100)
    fails := Check(click.done && !Engine.Desired(100).Has("LButton") && !Engine.Desired(100).Has("XButton2"), "letting go stops the spam", fails)
    Engine.StopAll()
    Engine.RemoveDone()
    Engine.finger := Map()
    Engine.NotePhysical("XButton2", true)
    Engine.finger["RButton"] := false
    after := J("{'id':'spam-after','name':'After','enabled':true,'advanced':true,'playMode':'whileHeld','trigger':{'kind':'side','button':'XButton2'},'steps':[],'blocks':[{'type':'tapSpam','key':'LButton','watch':['RButton'],'armMs':0,'gapMs':50,'holdMs':18,'restMs':18},{'type':'then','forMs':0,'steps':[{'type':'key','action':'down','key':'a'},{'type':'wait','ms':40}]}]}")
    Engine.StartRunner(after)
    Engine.FlushPending()
    tail := Engine.runners[1]
    tail.Advance(0)
    Engine.finger["RButton"] := true
    tail.Advance(10)
    fails := Check(!Engine.Desired(10).Has("LButton") && Engine.Desired(10).Has("XButton2"), "tracked press pauses spam before after release", fails)
    Engine.NotePhysical("XButton2", false)
    tail.Advance(20)
    fails := Check(Engine.Desired(20).Has("a") && !Engine.Desired(20).Has("LButton") && !tail.done, "letting go during the pause runs after release", fails)
    Engine.StopAll()
    Engine.RemoveDone()
    Engine.hooks.Delete("XButton2")
    Engine.physDown.Delete("XButton2")
    Engine.finger := Map()

    lock := J("{'id':'lock-z','name':'Lock','enabled':true,'advanced':true,'playMode':'whileHeld','trigger':{'kind':'side','button':'XButton1'},'steps':[],'blocks':[{'type':'tapHold','key':'z','watch':['t'],'armMs':5,'gapMs':80,'ignore':'macro','ignoreMs':150}]}")
    spamT := J("{'id':'spam-t','name':'Spam T','enabled':true,'advanced':true,'playMode':'once','trigger':{'kind':'key','button':'f'},'steps':[],'blocks':[{'type':'steps','steps':[{'type':'key','action':'down','key':'t'}]}]}")
    Engine.hooks["XButton1"] := true
    Engine.NotePhysical("XButton1", true)
    Engine.finger["t"] := false
    Engine.StartRunner(lock)
    Engine.FlushPending()
    gate := Engine.runners[1]
    gate.Advance(0)
    Engine.finger["t"] := true
    gate.Advance(10)
    fails := Check(gate.stack[-1].edgePhase = "arm", "lock takes the first tracked press", fails)
    gate.Advance(15)
    gate.Advance(95)
    fails := Check(gate.stack[-1].edgePhase = "down", "lock returns to hold after the first repress", fails)
    Engine.finger["t"] := false
    gate.Advance(100)
    Engine.finger["t"] := true
    gate.Advance(110)
    fails := Check(gate.stack[-1].edgePhase = "arm", "macro lock still takes a physical press inside the window", fails)
    gate.Advance(115)
    gate.Advance(195)
    fails := Check(gate.stack[-1].edgePhase = "down", "macro lock returns to hold after a physical press", fails)
    Engine.StartRunner(spamT)
    Engine.FlushPending()
    Engine.runners[2].Advance(200)
    Engine.finger["t"] := false
    gate.Advance(200)
    Engine.finger["t"] := true
    gate.Advance(210)
    fails := Check(gate.stack[-1].edgePhase = "down", "macro lock skips a sent key inside the window", fails)
    Engine.StopAll()
    Engine.RemoveDone()
    Engine.NotePhysical("XButton1", true)
    Engine.finger["t"] := false
    both := J("{'id':'lock-both','name':'Both','enabled':true,'advanced':true,'playMode':'whileHeld','trigger':{'kind':'side','button':'XButton1'},'steps':[],'blocks':[{'type':'tapHold','key':'z','watch':['t'],'armMs':5,'gapMs':80,'ignore':'both','ignoreMs':150}]}")
    Engine.StartRunner(both)
    Engine.FlushPending()
    hard := Engine.runners[1]
    hard.Advance(0)
    Engine.finger["t"] := true
    hard.Advance(10)
    hard.Advance(15)
    hard.Advance(95)
    Engine.finger["t"] := false
    hard.Advance(100)
    Engine.finger["t"] := true
    hard.Advance(110)
    fails := Check(hard.stack[-1].edgePhase = "down", "both lock skips a physical press inside the window", fails)
    Engine.finger["t"] := false
    hard.Advance(260)
    Engine.finger["t"] := true
    hard.Advance(270)
    fails := Check(hard.stack[-1].edgePhase = "arm", "both lock takes a press after the window", fails)
    Engine.StopAll()
    Engine.RemoveDone()
    Engine.hooks.Delete("XButton1")
    Engine.physDown.Delete("XButton1")
    Engine.finger := Map()

    Engine.muted := Map()
    Engine.onceLock := Map()
    Engine.oncePass := Map()
    Engine.passKeys := Map()
    Engine.sendNow := Map()
    fails := Check(NormalizePause("once") = "off", "once is not a lock", fails)
    fails := Check(NormalizePause("block") = "block", "block stays a mute", fails)
    Engine.sendNow["t"] := true
    fails := Check(Engine.Sending("t"), "sending marks a key the engine just sent", fails)
    Engine.sendNow := Map()
    fails := Check(!Engine.Sending("t"), "sending can be cleared", fails)

    Engine.hooks["LShift"] := true
    if Engine.physDown.Has("LShift")
        Engine.physDown.Delete("LShift")
    Engine.applied := Map()
    Engine.sendNow := Map()
    Engine.applied["LShift"] := true
    TriggerHook("$*LShift")
    fails := Check(!Engine.KeyDown("LShift"), "a sent shift is not a physical press", fails)
    Engine.applied := Map()
    Engine.sendNow["LShift"] := true
    TriggerHook("$*LShift")
    fails := Check(!Engine.KeyDown("LShift"), "sendNow still blocks a late shift hotkey", fails)
    Engine.sendNow := Map()
    TriggerHook("$*LShift")
    fails := Check(Engine.KeyDown("LShift"), "a real left shift is still physical", fails)
    Engine.rawOk := true
    Engine.applied := Map()
    Engine.sendNow := Map()
    Engine.NotePhysical("LShift", false)
    TriggerHook("$*LShift")
    fails := Check(!Engine.KeyDown("LShift"), "the ll hook owns the finger while a send hotkey fires", fails)
    Engine.rawOk := false
    fails := Check(!Engine.NeedsKeyRaise(0, 1000, true, false, 9000), "idle engine leaves the keyboard hook", fails)
    fails := Check(Engine.NeedsKeyRaise(0, 0, false, true, 9000), "a quiet keyboard hook is put back", fails)
    fails := Check(!Engine.NeedsKeyRaise(0, 8000, true, true, 9000), "mouse activity keeps the keyboard hook", fails)
    fails := Check(Engine.NeedsMouseRaise(0, true, true, 9000), "a quiet mouse hook is put back", fails)
    fails := Check(!Engine.NeedsMouseRaise(8000, true, true, 9000), "a live mouse hook stays", fails)
    fails := Check(!Engine.NeedsMouseRaise(0, false, true, 9000), "no mouse trigger means no mouse hook", fails)
    sender := J("{'id':'send-t','name':'T','enabled':true,'playMode':'whileHeld','trigger':{'kind':'key','button':'t'},'steps':[{'type':'key','action':'down','key':'shift'},{'type':'wait','ms':10},{'type':'key','action':'up','key':'shift'},{'type':'wait','ms':10}]}")
    target := J("{'id':'shift-mac','name':'Shift','enabled':true,'playMode':'whileHeld','trigger':{'kind':'key','button':'LShift'},'steps':[{'type':'wait','ms':80}]}")
    Engine.profile := Map("macros", [sender, target])
    Engine.hooks["t"] := true
    Engine.prevDown := Map()
    Engine.applied := Map()
    Engine.sendNow := Map()
    Engine.NotePhysical("LShift", false)
    Engine.NotePhysical("t", true)
    Engine.PollTriggers()
    Engine.FlushPending()
    fails := Check(Engine.HasRunner("send-t"), "t macro starts from the finger", fails)
    if Engine.HasRunner("send-t") {
        for r in Engine.runners
            if (r.id = "send-t" && !r.done)
                r.Advance(0)
    }
    Engine.FlushPending()
    for k, _ in Engine.Desired(0)
        Engine.applied[k] := true
    TriggerHook("$*LShift")
    Engine.PollTriggers()
    fails := Check(!Engine.HasRunner("shift-mac"), "t sending shift does not start the shift macro", fails)
    Engine.StopAll()
    Engine.RemoveDone()
    Engine.applied := Map()
    Engine.sendNow := Map()
    Engine.hooks.Delete("LShift")
    Engine.hooks.Delete("t")
    if Engine.physDown.Has("LShift")
        Engine.physDown.Delete("LShift")
    if Engine.physDown.Has("t")
        Engine.physDown.Delete("t")
    Engine.profile := Map()
    pause := J("{'id':'pause-z','name':'Pause','enabled':true,'advanced':true,'playMode':'whileHeld','trigger':{'kind':'side','button':'XButton1'},'steps':[],'blocks':[{'type':'tapHold','key':'z','watch':['t','XButton2'],'armMs':5,'gapMs':80,'ignore':'off','ignoreMs':0,'pauseWatch':'once'}]}")
    spamT := J("{'id':'spam-t','name':'Spam T','enabled':true,'playMode':'toggle','trigger':{'kind':'key','button':'t'},'steps':[{'type':'wait','ms':50}]}")
    other := J("{'id':'other-f','name':'Other','enabled':true,'playMode':'toggle','trigger':{'kind':'key','button':'f'},'steps':[{'type':'wait','ms':5000}]}")
    Engine.profile := Map("macros", [pause, spamT, other])
    Engine.hooks["XButton1"] := true
    Engine.hooks["t"] := true
    Engine.hooks["f"] := true
    Engine.NotePhysical("XButton1", true)
    Engine.StartRunner(spamT)
    Engine.StartRunner(other)
    Engine.FlushPending()
    Engine.StartRunner(pause)
    Engine.FlushPending()
    gate := ""
    for r in Engine.runners
        if (r.id = "pause-z" && !r.done)
            gate := r
    gate.Advance(0)
    Engine.FlushPending()
    Engine.RemoveDone()
    fails := Check(Engine.HasRunner("spam-t") && !Engine.OnceOpen("spam-t") && !Engine.OnceDone("spam-t") && !Engine.PassKey("t"), "once leaves tracked macros running with no lock", fails)
    Engine.StopAll()
    Engine.RemoveDone()
    Engine.muted := Map()
    Engine.onceLock := Map()
    Engine.oncePass := Map()
    Engine.passKeys := Map()
    Engine.hooks.Delete("XButton1")
    Engine.hooks.Delete("t")
    Engine.hooks.Delete("f")
    Engine.physDown.Delete("XButton1")
    if Engine.physDown.Has("t")
        Engine.physDown.Delete("t")
    if Engine.physDown.Has("f")
        Engine.physDown.Delete("f")
    Engine.finger := Map()

    block := J("{'id':'block-z','name':'Block','enabled':true,'advanced':true,'playMode':'whileHeld','trigger':{'kind':'side','button':'XButton1'},'steps':[],'blocks':[{'type':'tapHold','key':'z','watch':['t','XButton2'],'armMs':5,'gapMs':80,'ignore':'off','ignoreMs':0,'pauseWatch':'block'}]}")
    Engine.profile := Map("macros", [block, spamT, other])
    Engine.hooks["XButton1"] := true
    Engine.hooks["t"] := true
    Engine.hooks["f"] := true
    Engine.NotePhysical("XButton1", true)
    Engine.NotePhysical("t", false)
    Engine.StartRunner(spamT)
    Engine.StartRunner(other)
    Engine.FlushPending()
    Engine.StartRunner(block)
    Engine.FlushPending()
    wall := ""
    for r in Engine.runners
        if (r.id = "block-z" && !r.done)
            wall := r
    wall.Advance(0)
    Engine.RemoveDone()
    spamPaused := false
    for r in Engine.runners
        if (r.id = "spam-t" && !r.done && r.paused)
            spamPaused := true
    fails := Check(spamPaused && Engine.IsMuted("spam-t"), "block mutes a tracked macro", fails)
    fails := Check(Engine.HasRunner("other-f") && !Engine.IsMuted("other-f"), "block leaves other macros alone", fails)
    fails := Check(!Engine.PassKey("t"), "block keeps swallowing the tracked key", fails)
    Engine.prevDown["spam-t"] := false
    Engine.NotePhysical("t", true)
    Engine.PollTriggers()
    Engine.FlushPending()
    spamLive := 0
    for r in Engine.runners
        if (r.id = "spam-t" && !r.done && !r.paused)
            spamLive++
    fails := Check(spamLive = 0 && Engine.IsMuted("spam-t"), "block does not let the tracked macro play", fails)
    Engine.StopRunner("block-z")
    Engine.RemoveDone()
    Engine.FlushPending()
    fails := Check(Engine.HasRunner("spam-t") && !Engine.IsMuted("spam-t"), "release unmutes the tracked macro", fails)
    Engine.StopAll()
    Engine.RemoveDone()
    Engine.muted := Map()
    Engine.onceLock := Map()
    Engine.oncePass := Map()
    Engine.passKeys := Map()
    Engine.hooks.Delete("XButton1")
    Engine.hooks.Delete("t")
    Engine.hooks.Delete("f")
    Engine.physDown.Delete("XButton1")
    if Engine.physDown.Has("t")
        Engine.physDown.Delete("t")
    Engine.finger := Map()
    Engine.profile := Map()

    drain := J("{'id':'drain-up','name':'Drain','enabled':true,'advanced':true,'releaseStop':'nextUp','playMode':'once','trigger':{'kind':'key','button':'r'},'steps':[],'blocks':[{'type':'steps','steps':[{'type':'key','action':'down','key':'b'},{'type':'wait','ms':50},{'type':'key','action':'up','key':'b'},{'type':'wait','ms':10},{'type':'key','action':'down','key':'k'},{'type':'wait','ms':50},{'type':'key','action':'up','key':'k'}]}]}")
    Engine.StartRunner(drain)
    Engine.FlushPending()
    cut := Engine.runners[1]
    cut.Advance(0)
    fails := Check(Engine.Desired(0).Has("b"), "stop at up starts with b down", fails)
    cut.AskStop()
    cut.Advance(50)
    fails := Check(!Engine.Desired(50).Has("b") && !Engine.Desired(50).Has("k"), "stop at up releases b and skips k", fails)
    fails := Check(cut.done, "stop at up ends on the first up", fails)
    Engine.StopAll()
    Engine.RemoveDone()

    full := J("{'id':'drain-full','name':'Full','enabled':true,'advanced':true,'releaseStop':'finish','playMode':'once','trigger':{'kind':'key','button':'r'},'steps':[],'blocks':[{'type':'steps','steps':[{'type':'key','action':'down','key':'b'},{'type':'wait','ms':50},{'type':'key','action':'up','key':'b'},{'type':'wait','ms':10},{'type':'key','action':'down','key':'k'},{'type':'wait','ms':50},{'type':'key','action':'up','key':'k'}]}]}")
    Engine.StartRunner(full)
    Engine.FlushPending()
    play := Engine.runners[1]
    play.Advance(0)
    play.AskStop()
    play.Advance(50)
    fails := Check(!play.done, "finish on release keeps going after the first up", fails)
    play.Advance(60)
    fails := Check(Engine.Desired(60).Has("k"), "finish on release still presses k", fails)
    play.Advance(110)
    fails := Check(play.done && !Engine.Desired(110).Has("k"), "finish on release ends after the last up", fails)
    Engine.StopAll()
    Engine.RemoveDone()

    fails := Check(!Engine.HoldsOnRelease(drain), "play once without a hold loop does not use on-release", fails)
    fails := Check(Engine.HoldsOnRelease(J("{'playMode':'whileHeld','blocks':[]}")), "while held playback uses on-release", fails)
    fails := Check(!Engine.HoldsOnRelease(J("{'playMode':'once','advanced':true,'blocks':[{'type':'whileHeld','mute':[],'steps':[]},{'type':'steps','steps':[]}]}")), "a while-held graph does not stop the runner on release", fails)
    shotgun := J("{'id':'shotgun','name':'Slot','enabled':true,'advanced':true,'playMode':'once','trigger':{'kind':'key','button':'LWin'},'steps':[],'blocks':[{'type':'whileHeld','mute':[],'steps':[{'type':'key','action':'down','key':'['},{'type':'wait','ms':10},{'type':'key','action':'up','key':'['},{'type':'wait','ms':10}]},{'type':'ifShort','underMs':150,'minCycles':6,'steps':[{'type':'key','action':'down','key':'['},{'type':'wait','ms':10},{'type':'key','action':'up','key':'['},{'type':'wait','ms':10}]},{'type':'steps','steps':[{'type':'repeat','count':4,'steps':[{'type':'key','action':'down','key':'a'},{'type':'wait','ms':10},{'type':'key','action':'up','key':'a'},{'type':'wait','ms':10}]}]}]}")
    Engine.hooks["LWin"] := true
    Engine.profile := Map("macros", [shotgun])
    Engine.prevDown := Map()
    Engine.NotePhysical("LWin", true)
    Engine.PollTriggers()
    Engine.FlushPending()
    slot := Engine.runners[1]
    slot.Advance(0)
    slot.Advance(10)
    slot.Advance(160)
    Engine.NotePhysical("LWin", false)
    Engine.PollTriggers()
    Engine.RemoveDone()
    fails := Check(Engine.HasRunner("shotgun"), "long hold still runs after release", fails)
    slot.Advance(170)
    fails := Check(Engine.Desired(170).Has("a") && !Engine.Desired(170).Has("["), "long hold then release runs the after-hold keys", fails)
    Engine.StopAll()
    Engine.RemoveDone()
    Engine.NotePhysical("LWin", true)
    Engine.prevDown["shotgun"] := false
    Engine.PollTriggers()
    Engine.FlushPending()
    tapSlot := Engine.runners[1]
    tapSlot.Advance(0)
    tapSlot.Advance(10)
    Engine.NotePhysical("LWin", false)
    Engine.PollTriggers()
    Engine.RemoveDone()
    tapSlot.Advance(40)
    fails := Check(Engine.Desired(40).Has("[") && !Engine.Desired(40).Has("a"), "a quick tap still sends the short path", fails)
    tapSlot.Advance(200)
    fails := Check(!Engine.Desired(200).Has("a"), "a quick tap skips the after-hold keys", fails)
    Engine.StopAll()
    Engine.RemoveDone()
    Engine.hooks.Delete("LWin")
    if Engine.physDown.Has("LWin")
        Engine.physDown.Delete("LWin")
    Engine.profile := Map()
    swapMac := J("{'id':'swap-a','name':'Swap','enabled':true,'advanced':true,'playMode':'once','trigger':{'kind':'key','button':'b'},'steps':[],'blocks':[{'type':'whileHeld','mute':[],'steps':[{'type':'key','action':'down','key':'a'},{'type':'wait','ms':10},{'type':'key','action':'up','key':'a'},{'type':'wait','ms':10}]},{'type':'swapAfter','afterMs':40,'mute':[],'steps':[{'type':'key','action':'down','key':'x'},{'type':'wait','ms':10},{'type':'key','action':'up','key':'x'},{'type':'wait','ms':10}]}]}")
    Engine.hooks["b"] := true
    Engine.profile := Map("macros", [swapMac])
    Engine.prevDown := Map()
    Engine.NotePhysical("b", true)
    Engine.PollTriggers()
    Engine.FlushPending()
    swapTap := Engine.runners[1]
    swapTap.Advance(0)
    fails := Check(Engine.Desired(0).Has("a") && !Engine.Desired(0).Has("x"), "swap after starts on the first loop", fails)
    Engine.NotePhysical("b", false)
    Engine.PollTriggers()
    Engine.RemoveDone()
    swapTap.Advance(20)
    fails := Check(!Engine.Desired(20).Has("x"), "a quick tap never reaches the swap loop", fails)
    Engine.StopAll()
    Engine.RemoveDone()
    Engine.NotePhysical("b", true)
    Engine.prevDown["swap-a"] := false
    Engine.PollTriggers()
    Engine.FlushPending()
    swapHold := Engine.runners[1]
    swapHold.Advance(0)
    swapHold.Advance(10)
    swapHold.Advance(20)
    swapHold.Advance(40)
    swapHold.Advance(50)
    fails := Check(Engine.Desired(50).Has("x") && !Engine.Desired(50).Has("a"), "holding past the time swaps to the second loop", fails)
    Engine.StopAll()
    Engine.RemoveDone()
    Engine.hooks.Delete("b")
    if Engine.physDown.Has("b")
        Engine.physDown.Delete("b")
    Engine.profile := Map()
    onceWait := J("{'id':'once-wait','name':'Once','enabled':true,'advanced':true,'playMode':'once','trigger':{'kind':'key','button':'r'},'steps':[],'blocks':[{'type':'wait','ms':80}]}")
    Engine.profile := Map("macros", [onceWait])
    Engine.hooks["r"] := true
    Engine.prevDown := Map()
    Engine.NotePhysical("r", true)
    Engine.PollTriggers()
    Engine.FlushPending()
    fails := Check(Engine.HasRunner("once-wait"), "play once starts on press", fails)
    Engine.NotePhysical("r", false)
    Engine.PollTriggers()
    fails := Check(Engine.HasRunner("once-wait"), "play once keeps going after release", fails)
    waitOnce := Engine.runners[1]
    waitOnce.Advance(0)
    waitOnce.Advance(80)
    Engine.RemoveDone()
    fails := Check(!Engine.HasRunner("once-wait"), "play once advanced finishes after one pass", fails)
    Engine.StopAll()
    Engine.RemoveDone()
    Engine.NotePhysical("r", false)
    playUp := J("{'id':'play-up','name':'Up','enabled':true,'advanced':true,'playMode':'onRelease','trigger':{'kind':'key','button':'r'},'steps':[],'blocks':[{'type':'wait','ms':80}]}")
    Engine.profile := Map("macros", [playUp])
    Engine.prevDown := Map()
    Engine.NotePhysical("r", true)
    Engine.PollTriggers()
    Engine.FlushPending()
    fails := Check(!Engine.HasRunner("play-up"), "playback on release ignores press", fails)
    Engine.NotePhysical("r", false)
    Engine.PollTriggers()
    Engine.FlushPending()
    fails := Check(Engine.HasRunner("play-up"), "playback on release starts when the key comes up", fails)
    Engine.StopAll()
    Engine.RemoveDone()
    Engine.hooks.Delete("r")
    if Engine.physDown.Has("r")
        Engine.physDown.Delete("r")
    Engine.profile := Map()

    override := J("{'id':'block-full','name':'Block','enabled':true,'advanced':true,'releaseStop':'nextUp','playMode':'whileHeld','trigger':{'kind':'key','button':'r'},'steps':[],'blocks':[{'type':'whileHeld','releaseStop':'finish','mute':[],'steps':[{'type':'key','action':'down','key':'b'},{'type':'wait','ms':50},{'type':'key','action':'up','key':'b'},{'type':'wait','ms':10},{'type':'key','action':'down','key':'k'},{'type':'wait','ms':50},{'type':'key','action':'up','key':'k'}]}]}")
    Engine.StartRunner(override)
    Engine.FlushPending()
    over := Engine.runners[1]
    over.Advance(0)
    over.AskStop()
    over.Advance(50)
    fails := Check(!over.done, "block play full overrides macro nearest up", fails)
    over.Advance(60)
    fails := Check(Engine.Desired(60).Has("k"), "block play full still presses k", fails)
    Engine.StopAll()
    Engine.RemoveDone()

    near := J("{'id':'hold-near','name':'Near','enabled':true,'advanced':true,'releaseStop':'nextUp','playMode':'once','trigger':{'kind':'key','button':'r'},'steps':[],'blocks':[{'type':'whileHeld','mute':[],'steps':[{'type':'key','action':'down','key':'b'},{'type':'wait','ms':50},{'type':'key','action':'up','key':'b'},{'type':'wait','ms':10},{'type':'key','action':'down','key':'k'},{'type':'wait','ms':50},{'type':'key','action':'up','key':'k'}]}]}")
    Engine.hooks["r"] := true
    Engine.NotePhysical("r", true)
    Engine.StartRunner(near)
    Engine.FlushPending()
    holdNear := Engine.runners[1]
    holdNear.Advance(0)
    fails := Check(Engine.Desired(0).Has("b"), "nearest up hold starts with b down", fails)
    Engine.NotePhysical("r", false)
    holdNear.Advance(50)
    fails := Check(!Engine.Desired(50).Has("k") && holdNear.done, "nearest up stops a while-held sequence at the up", fails)
    Engine.StopAll()
    Engine.RemoveDone()

    fullHold := J("{'id':'hold-full','name':'FullHold','enabled':true,'advanced':true,'releaseStop':'finish','playMode':'once','trigger':{'kind':'key','button':'r'},'steps':[],'blocks':[{'type':'whileHeld','mute':[],'steps':[{'type':'key','action':'down','key':'b'},{'type':'wait','ms':50},{'type':'key','action':'up','key':'b'},{'type':'wait','ms':10},{'type':'key','action':'down','key':'k'},{'type':'wait','ms':50},{'type':'key','action':'up','key':'k'}]}]}")
    Engine.NotePhysical("r", true)
    Engine.StartRunner(fullHold)
    Engine.FlushPending()
    holdFull := Engine.runners[1]
    holdFull.Advance(0)
    Engine.NotePhysical("r", false)
    holdFull.Advance(50)
    fails := Check(!holdFull.done, "play full keeps going after release", fails)
    holdFull.Advance(60)
    fails := Check(Engine.Desired(60).Has("k"), "play full finishes the while-held sequence", fails)
    Engine.StopAll()
    Engine.RemoveDone()

    blockNear := J("{'id':'block-near','name':'BlockNear','enabled':true,'advanced':true,'releaseStop':'finish','playMode':'once','trigger':{'kind':'key','button':'r'},'steps':[],'blocks':[{'type':'whileHeld','releaseStop':'nextUp','mute':[],'steps':[{'type':'key','action':'down','key':'b'},{'type':'wait','ms':50},{'type':'key','action':'up','key':'b'},{'type':'wait','ms':10},{'type':'key','action':'down','key':'k'},{'type':'wait','ms':50},{'type':'key','action':'up','key':'k'}]}]}")
    Engine.NotePhysical("r", true)
    Engine.StartRunner(blockNear)
    Engine.FlushPending()
    blockCut := Engine.runners[1]
    blockCut.Advance(0)
    Engine.NotePhysical("r", false)
    blockCut.Advance(50)
    fails := Check(!Engine.Desired(50).Has("k") && blockCut.done, "block nearest up overrides macro play full", fails)
    Engine.StopAll()
    Engine.RemoveDone()
    Engine.hooks.Delete("r")
    if Engine.physDown.Has("r")
        Engine.physDown.Delete("r")

    holdGate := J("{'id':'scan-hold','name':'Hold','enabled':true,'advanced':true,'playMode':'once','trigger':{'kind':'key','button':'b'},'steps':[],'blocks':[{'type':'steps','steps':[{'type':'key','action':'down','key':'o'},{'type':'wait','ms':10},{'type':'key','action':'up','key':'o'},{'type':'scanWait','ms':80}]},{'type':'steps','steps':[{'type':'key','action':'down','key':'x'},{'type':'wait','ms':10}]}]}")
    Engine.hooks["b"] := true
    Engine.NotePhysical("b", true)
    Engine.StartRunner(holdGate)
    Engine.FlushPending()
    gate := Engine.runners[1]
    gate.Advance(0)
    fails := Check(Engine.Desired(0).Has("o") && !Engine.Desired(0).Has("x"), "scan wait runs the keys first", fails)
    gate.Advance(10)
    fails := Check(!Engine.Desired(10).Has("o") && !Engine.Desired(10).Has("x"), "scan wait starts after those keys", fails)
    Engine.NotePhysical("b", false)
    gate.Advance(40)
    Engine.RemoveDone()
    fails := Check(!Engine.HasRunner("scan-hold") && !Engine.Desired(40).Has("x"), "scan wait exits if the trigger lets go", fails)
    Engine.NotePhysical("b", true)
    Engine.StartRunner(holdGate)
    Engine.FlushPending()
    heldGate := Engine.runners[1]
    heldGate.Advance(0)
    heldGate.Advance(10)
    heldGate.Advance(90)
    fails := Check(Engine.Desired(90).Has("x"), "scan wait continues after the hold", fails)
    Engine.StopAll()
    Engine.RemoveDone()
    scanOnly := J("{'id':'scan-only','name':'Scan','enabled':true,'advanced':true,'playMode':'once','trigger':{'kind':'key','button':'b'},'steps':[],'blocks':[{'type':'steps','steps':[{'type':'scanWait','ms':80}]},{'type':'steps','steps':[{'type':'key','action':'down','key':'x'},{'type':'wait','ms':10}]}]}")
    Engine.NotePhysical("b", true)
    Engine.StartRunner(scanOnly)
    Engine.FlushPending()
    scan := Engine.runners[1]
    scan.Advance(0)
    fails := Check(!Engine.Desired(0).Has("x"), "scan wait can run with no keys around it", fails)
    scan.Advance(80)
    fails := Check(Engine.Desired(80).Has("x"), "scan wait continues after the sleep", fails)
    Engine.StopAll()
    Engine.RemoveDone()
    Engine.hooks.Delete("b")
    if Engine.physDown.Has("b")
        Engine.physDown.Delete("b")

    shot := J("{'id':'edit-shot','name':'Edit','enabled':true,'advanced':true,'playMode':'whileHeld','trigger':{'kind':'key','button':'n'},'steps':[],'blocks':[{'type':'onceHeld','gapMs':70,'steps':[{'type':'key','action':'down','key':'o'},{'type':'wait','ms':10},{'type':'key','action':'up','key':'o'}]},{'type':'then','forMs':0,'steps':[{'type':'key','action':'down','key':'9'},{'type':'wait','ms':10}]}]}")
    Engine.hooks["n"] := true
    Engine.profile := Map("macros", [shot])
    Engine.NotePhysical("n", true)
    Engine.StartRunner(shot)
    Engine.FlushPending()
    quickShot := Engine.runners[1]
    quickShot.Advance(0)
    fails := Check(Engine.Desired(0).Has("o") && !Engine.Desired(0).Has("9"), "one press starts with the chord", fails)
    Engine.NotePhysical("n", false)
    quickShot.Advance(10)
    Engine.prevDown["edit-shot"] := true
    Engine.PollTriggers()
    Engine.RemoveDone()
    fails := Check(Engine.HasRunner("edit-shot") && !Engine.Desired(10).Has("9"), "a fast release keeps the runner and does not shoot yet", fails)
    quickShot.Advance(40)
    fails := Check(!Engine.Desired(40).Has("9") && quickShot.cycles = 1, "the shot waits out the gap after one chord", fails)
    quickShot.Advance(80)
    fails := Check(Engine.Desired(80).Has("9") && quickShot.cycles = 1, "a fast press shoots 70 ms after the chord", fails)
    Engine.StopAll()
    Engine.RemoveDone()
    Engine.NotePhysical("n", true)
    Engine.StartRunner(shot)
    Engine.FlushPending()
    midShot := Engine.runners[1]
    midShot.Advance(0)
    midShot.Advance(10)
    fails := Check(!Engine.Desired(10).Has("9"), "holding through the chord does not shoot", fails)
    Engine.NotePhysical("n", false)
    midShot.Advance(50)
    fails := Check(!Engine.Desired(50).Has("9") && midShot.cycles = 1, "letting go during the gap does not shoot early", fails)
    midShot.Advance(80)
    fails := Check(Engine.Desired(80).Has("9") && midShot.cycles = 1, "a press released during the gap still waits the full 70 ms", fails)
    Engine.StopAll()
    Engine.RemoveDone()
    Engine.NotePhysical("n", true)
    Engine.StartRunner(shot)
    Engine.FlushPending()
    heldShot := Engine.runners[1]
    heldShot.Advance(0)
    heldShot.Advance(10)
    heldShot.Advance(80)
    fails := Check(Engine.Desired(80).Has("o") && !Engine.Desired(80).Has("9"), "still holding when the gap ends edits again", fails)
    Engine.NotePhysical("n", false)
    Engine.prevDown["edit-shot"] := true
    Engine.PollTriggers()
    Engine.RemoveDone()
    heldShot.Advance(90)
    fails := Check(Engine.HasRunner("edit-shot") && !Engine.Desired(90).Has("9") && heldShot.cycles = 2, "release after a hold finishes that pass before the shot", fails)
    heldShot.Advance(160)
    fails := Check(Engine.Desired(160).Has("9"), "a hold shoots 70 ms after the last pass", fails)
    Engine.StopAll()
    Engine.RemoveDone()
    Engine.NotePhysical("n", true)
    Engine.StartRunner(shot)
    Engine.FlushPending()
    spam := Engine.runners[1]
    spam.Advance(0)
    spam.Advance(10)
    spam.Advance(80)
    spam.Advance(90)
    fails := Check(spam.cycles = 2 && !Engine.Desired(90).Has("9"), "a hold finishes the second cycle without shooting", fails)
    spam.Advance(91)
    fails := Check(Engine.Desired(91).Has("o") && spam.cycles = 2, "a hold starts the next cycle without waiting another 70 ms", fails)
    Engine.StopAll()
    Engine.RemoveDone()
    Engine.hooks.Delete("n")
    if Engine.physDown.Has("n")
        Engine.physDown.Delete("n")
    Engine.profile := savedProfile

    heldSeven := J("{'id':'hold-7','name':'B','enabled':true,'advanced':true,'releaseStop':'nextUp','playMode':'once','trigger':{'kind':'key','button':'b'},'steps':[],'blocks':[{'type':'steps','steps':[{'type':'key','action':'down','key':'b'},{'type':'wait','ms':18},{'type':'key','action':'up','key':'b'},{'type':'wait','ms':10},{'type':'key','action':'down','key':'7'}]},{'type':'then','forMs':55,'steps':[{'type':'key','action':'up','key':'7'}]}]}")
    Engine.hooks["b"] := true
    Engine.profile := Map("macros", [heldSeven])
    Engine.NotePhysical("b", true)
    Engine.StartRunner(heldSeven)
    Engine.FlushPending()
    hold7 := Engine.runners[1]
    hold7.Advance(0)
    hold7.Advance(18)
    hold7.Advance(28)
    fails := Check(Engine.Desired(28).Has("7") && !hold7.done, "a down stays down while the trigger is held", fails)
    hold7.Advance(100)
    fails := Check(Engine.Desired(100).Has("7") && !hold7.done, "after release does not send the up while the trigger is held", fails)
    Engine.NotePhysical("b", false)
    hold7.Advance(101)
    fails := Check(!Engine.Desired(101).Has("7") && !hold7.done, "after release sends the matching up when the trigger comes up", fails)
    Engine.StopAll()
    Engine.RemoveDone()
    Engine.hooks.Delete("b")
    if Engine.physDown.Has("b")
        Engine.physDown.Delete("b")
    Engine.profile := savedProfile

    fails := Check(AsNum("abc", 7) = 7, "bad number falls back instead of throwing", fails)
    fails := Check(AsNum("12", 0) = 12 && AsNum(3, 0) = 3, "numbers still parse", fails)
    fails := Check(JSON.Quote("a" Chr(1) "b") = '"a\u0001b"', "control chars are escaped in json", fails)
    fails := Check(JSON.Quote("x`ny") = '"x\ny"', "newline keeps the short escape", fails)
    badStep := J("{'id':'bad-ms','name':'Bad','enabled':true,'basic':false,'playMode':'once','trigger':{'kind':'key','button':'b'},'steps':[{'type':'wait','ms':'oops'},{'type':'key','action':'down','key':'a'},{'type':'wait','ms':10}]}")
    Engine.StartRunner(badStep)
    Engine.FlushPending()
    badRun := Engine.runners[1]
    badRun.Advance(0)
    badRun.Advance(1)
    fails := Check(Engine.Desired(1).Has("a"), "a step with a bad number still plays the rest", fails)
    Engine.StopAll()
    Engine.RemoveDone()

    Engine.muted := Map()
    holdW := J("{'id':'hold-w','name':'HoldW','enabled':true,'playMode':'whileHeld','trigger':{'kind':'key','button':'h'},'steps':[{'type':'key','action':'down','key':'w'}]}")
    gateW := J("{'id':'gate-w','name':'Gate','enabled':true,'advanced':true,'playMode':'once','trigger':{'kind':'key','button':'n'},'steps':[],'blocks':[{'type':'whileHeld','mute':['hold-w'],'steps':[{'type':'wait','ms':50}]}]}")
    Engine.profile := Map("macros", [holdW, gateW])
    Engine.hooks["h"] := true
    Engine.hooks["n"] := true
    Engine.NotePhysical("h", true)
    Engine.NotePhysical("n", false)
    Engine.prevDown := Map()
    Engine.prevDown["hold-w"] := false
    Engine.prevDown["gate-w"] := false
    Engine.PollTriggers()
    Engine.FlushPending()
    wRun := ""
    for r in Engine.runners
        if (r.id = "hold-w" && !r.done)
            wRun := r
    wRun.Advance(0)
    fails := Check(Engine.Desired(0).Has("w"), "plain hold macro holds w", fails)
    Engine.NotePhysical("n", true)
    Engine.prevDown["gate-w"] := false
    Engine.PollTriggers()
    Engine.FlushPending()
    gRun := ""
    for r in Engine.runners
        if (r.id = "gate-w" && !r.done)
            gRun := r
    gRun.Advance(0)
    fails := Check(wRun.paused && !Engine.Desired(0).Has("w"), "gate mutes the plain hold", fails)
    Engine.NotePhysical("h", false)
    Engine.PollTriggers()
    Engine.NotePhysical("n", false)
    gRun.Advance(60)
    fails := Check(wRun.done && !Engine.Desired(60).Has("w"), "unmute does not replay a hold whose key came up", fails)
    Engine.StopAll()
    Engine.RemoveDone()
    Engine.muted := Map()
    Engine.hooks.Delete("h")
    Engine.hooks.Delete("n")
    if Engine.physDown.Has("h")
        Engine.physDown.Delete("h")
    if Engine.physDown.Has("n")
        Engine.physDown.Delete("n")
    Engine.profile := savedProfile

    DllCall("Winmm.dll\timeBeginPeriod", "UInt", 1)
    clockMacro := J("{'id':'clock-5','name':'Clock','enabled':true,'basic':false,'playMode':'once','trigger':{'kind':'key','button':'b'},'steps':[{'type':'wait','ms':5}]}")
    Engine.StartRunner(clockMacro)
    Engine.FlushPending()
    clock := Engine.runners[1]
    t0 := Engine.Now()
    clock.Advance(t0)
    while (!clock.done && Engine.Now() - t0 < 40) {
        Engine.Pace()
        clock.Advance(Engine.Now())
    }
    elapsed := Engine.Now() - t0
    fails := Check(clock.done && elapsed >= 4.5 && elapsed <= 9, "5 ms wait landed " Round(elapsed, 2), fails)
    Engine.StopAll()
    Engine.RemoveDone()
    DllCall("Winmm.dll\timeEndPeriod", "UInt", 1)
    fails := RunOnceChecks(fails)
    fails := HeldPressChecks(fails)
    fails := SoakChecks(fails)
    fails := CreatePathChecks(fails)
    fails := HoldRepressChecks(fails)
    fails := TurnChecks(fails)
    return fails
}

; Simple Edit: whileHeld with a onceHeld block, 85 ms gap after a release.
; A re-press inside the gap was refused while the runner finished, and the
; key then stayed held with nothing playing. It is kept and starts as
; held-press once the gap ends. A tap that is up before then is dropped.
; A press that lands with a reload is still an edge. Focus coming back with
; the key held does not restart it.
HeldPressChecks(fails) {
    saved := Map("rawOk", Engine.rawOk, "out", Engine.outputMode, "profile", Engine.profile,
        "known", Engine.frontKnown, "front", Engine.frontExe, "dry", Engine.dry)
    Engine.BuildVkMap()
    Engine.dry := true
    Engine.rawOk := true
    Engine.frontKnown := true
    Engine.frontExe := "game.exe"
    Engine.hidEcho := []
    edit := J("{'id':'edit','name':'Simple Edit','enabled':true,'advanced':true,'playMode':'whileHeld','releaseStop':'finish','trigger':{'kind':'key','button':'b'},'steps':[],'blocks':[{'type':'onceHeld','gapMs':85,'steps':[{'type':'key','action':'down','key':'o'},{'type':'wait','ms':10},{'type':'key','action':'up','key':'o'}]}]}")
    for mode in ["software", "rp2350"] {
        for kind in ["re-press", "tap", "focus"] {
            Engine.outputMode := mode
            Engine.profile := Map("macros", [edit])
            if (kind = "focus")
                Engine.profile["focusExe"] := "game.exe"
            Engine.hooks := Map("b", true)
            Engine.physDown := Map()
            Engine.repeatSeen := Map()
            Engine.SeedTriggers()
            Engine.capture := []
            starts := 0, dead := 0, refocused := true
            now := 0
            while (now < 2000) {
                if (kind = "re-press") {
                    if (now = 100 || now = 425)
                        TestKey("b", true)
                    else if (now = 400 || now = 1425)
                        TestKey("b", false)
                } else if (kind = "tap") {
                    if (now = 100 || now = 420)
                        TestKey("b", true)
                    else if (now = 400 || now = 440)
                        TestKey("b", false)
                } else {
                    if (now = 100 || now = 1500)
                        TestKey("b", true)
                    else if (now = 1200 || now = 1800)
                        TestKey("b", false)
                    Engine.frontExe := (now >= 500 && now < 700) ? "explorer.exe" : "game.exe"
                    Engine.focusCache := Map()
                }
                starts += TestTick(now)
                if (kind = "re-press" && now >= 520 && now < 1425 && !Engine.HasRunner("edit"))
                    dead++
                if (kind = "focus" && now = 1000 && Engine.HasRunner("edit"))
                    refocused := false
                now++
            }
            lines := Engine.capture
            Engine.capture := ""
            held := 0, blocked := 0
            for line in lines {
                if InStr(line, "runner start") && InStr(line, "reason=held-press")
                    held++
                if InStr(line, "start blocked") && InStr(line, "reason=already-running")
                    blocked++
            }
            label := mode " " kind
            if (kind = "re-press") {
                fails := Check(blocked = 1, label ": a re-press in the gap is refused while the runner finishes", fails)
                fails := Check(held = 1 && starts = 2, label ": the refused press starts as held-press", fails)
                fails := Check(dead = 0, label ": the held key plays the whole hold after the gap", fails)
            } else if (kind = "tap") {
                fails := Check(blocked = 1 && held = 0 && starts = 1, label ": a tap that is up before the gap ends is not owed", fails)
            } else {
                fails := Check(refocused, label ": focus back with the key held does not restart it", fails)
                fails := Check(starts = 2, label ": after a focus switch the next press plays", fails)
            }
            Engine.StopAll()
            Engine.RemoveDone()
        }
    }

    ; A reload keeps prev for an unchanged trigger: a press since the last
    ; poll is still an edge. A start reads the finger: held before, no start.
    reload := J("{'id':'reload','name':'Reload','enabled':true,'advanced':true,'playMode':'whileHeld','releaseStop':'finish','trigger':{'kind':'key','button':'a'},'steps':[],'blocks':[{'type':'whileHeld','steps':[{'type':'key','action':'down','key':'a'},{'type':'wait','ms':18},{'type':'key','action':'up','key':'a'},{'type':'wait','ms':18}]}]}")
    Engine.profile := Map("macros", [reload])
    Engine.hooks := Map("a", true)
    Engine.physDown := Map()
    Engine.SeedTriggers()
    TestTick(1)
    TestKey("a", true)
    Engine.SeedTriggers(true)
    fails := Check(TestTick(2) = 1, "a press that lands with a reload starts", fails)
    TestKey("a", false)
    Engine.SeedTriggers(true)
    now := 3
    while (now < 100) {
        TestTick(now)
        now++
    }
    fails := Check(!Engine.HasRunner("reload"), "a release that lands with a reload stops it", fails)
    TestKey("a", true)
    Engine.SeedTriggers()
    fails := Check(TestTick(200) = 0, "a key held before a start does not start", fails)
    TestKey("a", false)
    Engine.StopAll()
    Engine.RemoveDone()

    fails := Check(SilenceVerdict(false, 5000, 9000) = "unknown", "no raw keyboard is unknown", fails)
    fails := Check(SilenceVerdict(true, 5000, 0) = "idle", "no raw key at all is idle", fails)
    fails := Check(SilenceVerdict(true, 5000, 5100) = "idle", "a raw key 100 ms after the hook's is the same key", fails)
    fails := Check(SilenceVerdict(true, 5000, 8000) = "dropped", "a raw key 3 s after the hook went quiet is a dropped hook", fails)
    Engine.rawBoard := Map(7, false, 8, true)
    Engine.rawKeyTick := 0
    Engine.NoteRawKey(0, 100)
    Engine.NoteRawKey(8, 200)
    fails := Check(Engine.rawKeyTick = 0, "injected and board keys are not a real key", fails)
    Engine.NoteRawKey(7, 300)
    fails := Check(Engine.rawKeyTick = 300, "a real keyboard key is stamped", fails)
    Engine.rawKeyTick := 0
    Engine.rawBoard := Map()

    Engine.hooks := Map()
    Engine.physDown := Map()
    Engine.repeatSeen := Map()
    Engine.rawOk := saved["rawOk"]
    Engine.outputMode := saved["out"]
    Engine.profile := saved["profile"]
    Engine.frontKnown := saved["known"]
    Engine.frontExe := saved["front"]
    Engine.dry := saved["dry"]
    return fails
}

; The turn step: the live config's countsPer360, exact totals over ms, a
; stop drops the rest, uncalibrated sends nothing, and a board gets counts
; that are not scaled by dpi. Same numbers as runner_test check_turn.
TurnChecks(fails) {
    saved := Map("out", Engine.outputMode, "profile", Engine.profile, "dry", Engine.dry, "live", Engine.liveId)
    Engine.dry := true
    Engine.outputMode := "software"
    Engine.profile := J("{'configs':[{'id':'a','turn':{'countsPer360':2000},'macros':[]},{'id':'b','macros':[]}],'macros':[]}")
    Engine.liveId := "a"
    fails := Check(Engine.TurnPer360() = 2000, "turn uses the live config's countsPer360", fails)
    Engine.liveId := "b"
    fails := Check(Engine.TurnPer360() = 0, "a config with no turn is uncalibrated", fails)
    Engine.profile := J("{'turn':{'countsPer360':-4},'macros':[]}")
    fails := Check(Engine.TurnPer360() = 0, "a negative countsPer360 is uncalibrated", fails)

    Engine.profile := J("{'turn':{'countsPer360':2000},'macros':[]}")
    smooth := J("{'id':'turn','name':'Turn','playMode':'once','speed':1,'steps':[{'type':'turn','degrees':180,'dir':'right','ms':100},{'type':'move','x':0,'y':1}]}")
    got := TurnPlay(smooth, 120, -1)
    fails := Check(got.sum = 1000, "a smooth turn sends exactly its counts", fails)
    fails := Check(got.half = 500, "a smooth turn is half sent at half time", fails)
    fails := Check(got.marker = 100, "the next step waits for the turn to end", fails)
    fails := Check(got.big = 0, "a smooth turn sends small even steps", fails)

    Engine.profile := J("{'turn':{'countsPer360':80000},'macros':[]}")
    flick := J("{'id':'flick','name':'Flick','playMode':'once','speed':1,'steps':[{'type':'turn','degrees':180,'dir':'left','ms':0},{'type':'move','x':0,'y':1}]}")
    got := TurnPlay(flick, 4, -1)
    fails := Check(got.xs[1] = -16000 && got.xs[2] = -16000 && got.xs[3] = -8000 && got.marker = 2, "an instant turn over the limit goes out over three reports", fails)

    Engine.profile := J("{'turn':{'countsPer360':2000},'macros':[]}")
    fast := J("{'id':'fast','name':'Fast','playMode':'once','speed':2,'steps':[{'type':'turn','degrees':180,'ms':100},{'type':'move','x':0,'y':1}]}")
    got := TurnPlay(fast, 60, -1)
    fails := Check(got.sum = 1000 && got.marker = 50, "a turn's time divides by speed", fails)

    Engine.capture := []
    smooth["id"] := "cut"
    got := TurnPlay(smooth, 120, 41)
    lines := Engine.capture
    Engine.capture := ""
    stopped := 0
    for line in lines
        if InStr(line, "turn stopped") && InStr(line, "sent=400/1000") && InStr(line, "reason=stop")
            stopped++
    fails := Check(got.sum = 400 && stopped = 1, "a stop drops the rest and logs sent=400/1000", fails)

    Engine.profile := J("{'macros':[]}")
    Engine.capture := []
    raw := J("{'id':'raw','name':'Raw','playMode':'once','speed':1,'steps':[{'type':'turn','ms':30},{'type':'move','x':0,'y':1}]}")
    got := TurnPlay(raw, 40, -1)
    lines := Engine.capture
    Engine.capture := ""
    skipped := 0
    for line in lines
        if InStr(line, "turn skipped") && InStr(line, "reason=uncalibrated")
            skipped++
    fails := Check(got.sum = 0 && got.marker = 30 && skipped = 1, "an uncalibrated turn sends nothing, keeps its time, and logs once", fails)

    Engine.profile := J("{'turn':{'countsPer360':600},'macros':[]}")
    flat := J("{'id':'flat','name':'Flat','playMode':'once','speed':1,'steps':[{'type':'turn','degrees':180,'ms':0}]}")
    for mode in ["software", "rp2350"] {
        Engine.outputMode := mode
        got := TurnPlay(flat, 1, -1)
        fails := Check(got.xs[1] = 300 && got.counts = (mode != "software"), mode ": a turn reaches the output as counts", fails)
    }

    Engine.runners := []
    Engine.pending := []
    Engine.sent := []
    Engine.outputMode := saved["out"]
    Engine.profile := saved["profile"]
    Engine.dry := saved["dry"]
    Engine.liveId := saved["live"]
    return fails
}

; Plays one macro on a 1 ms clock in dry mode. stopAt -1 never stops.
TurnPlay(macro, lastMs, stopAt) {
    Engine.runners := []
    Engine.pending := []
    Engine.applied := Map()
    Engine.sent := []
    Engine.StartRunner(macro)
    out := {sum: 0, half: 0, marker: -1, big: 0, xs: [], counts: false}
    t := 0
    while (t <= lastMs) {
        if (t = stopAt)
            Engine.StopRunner(String(macro["id"]))
        Engine.sent := []
        Engine.RemoveDone()
        Engine.FlushPending()
        for r in Engine.runners
            r.Advance(t)
        Engine.FlushPending()
        Engine.Publish(t)
        Engine.RemoveDone()
        x := 0, y := 0
        if Engine.sent.Length {
            item := Engine.sent[-1]
            x := item["x"], y := item["y"]
            if item["counts"]
                out.counts := true
        }
        out.xs.Push(x)
        out.sum += x
        if (Abs(x) > 11 && Abs(x) < 8000)
            out.big++
        if (t = 50)
            out.half := out.sum
        if (out.marker < 0 && y = 1)
            out.marker := t
        t++
    }
    return out
}

; The trigger soak: engine-native\tests\fixtures\soak.txt on soak-profile.json,
; the same script and checks as engine-soak-test. The summary must equal
; soak-expected.txt, so both engines give the same results.
SoakChecks(fails) {
    dir := A_ScriptDir "\..\engine-native\tests\fixtures"
    text := "", script := "", expected := ""
    try text := FileRead(dir "\soak-profile.json", "UTF-8")
    try script := FileRead(dir "\soak.txt", "UTF-8")
    try expected := FileRead(dir "\soak-expected.txt", "UTF-8")
    if (text = "" || script = "" || expected = "")
        return Check(false, "soak fixtures readable", fails)
    scens := SoakParse(script)
    if (scens.Length = 0)
        return Check(false, "soak script parses", fails)
    saved := Map("rawOk", Engine.rawOk, "out", Engine.outputMode, "profile", Engine.profile,
        "known", Engine.frontKnown, "front", Engine.frontExe, "dry", Engine.dry, "live", Engine.liveId)
    Engine.BuildVkMap()
    Engine.dry := true
    Engine.rawOk := true
    Engine.hidEcho := []
    summary := ""
    errs := []
    for sc in scens
        for out in ["software", "rp2350"]
            summary .= SoakRun(text, sc, out, errs)
    for e in errs
        fails := Check(false, e, fails)
    expected := StrReplace(expected, "`r")
    if (summary != expected) {
        FileAppend("soak summary:`n" summary, "*")
        fails := Check(false, "soak summary equals soak-expected.txt", fails)
    }
    Engine.capture := ""
    Engine.hooks := Map()
    Engine.physDown := Map()
    Engine.repeatSeen := Map()
    Engine.repeatLogged := Map()
    Engine.owed := Map()
    Engine.rawOk := saved["rawOk"]
    Engine.outputMode := saved["out"]
    Engine.profile := saved["profile"]
    Engine.frontKnown := saved["known"]
    Engine.frontExe := saved["front"]
    Engine.dry := saved["dry"]
    Engine.liveId := saved["live"]
    return fails
}

SoakParse(script) {
    scens := []
    sc := ""
    for raw in StrSplit(script, "`n", "`r") {
        line := Trim(raw)
        if (line = "" || SubStr(line, 1, 1) = "#")
            continue
        w := StrSplit(RegExReplace(line, "\s+", " "), " ")
        cmd := w[1]
        if (cmd = "scenario") {
            sc := {name: w[2], front: w[3], end: 0, ev: []}
            scens.Push(sc)
        } else if (cmd = "end") {
            sc.end := Integer(w[2])
            SoakSort(sc.ev)
        } else if (cmd = "push")
            sc.ev.Push({at: Integer(w[2]), kind: "push", key: "", exe: ""})
        else if (cmd = "focus")
            sc.ev.Push({at: Integer(w[2]), kind: "focus", key: "", exe: w[3]})
        else if (cmd = "down" || cmd = "up")
            sc.ev.Push({at: Integer(w[3]), kind: cmd, key: w[2], exe: ""})
        else if (cmd = "hold" || cmd = "holdr")
            SoakHold(sc, w[2], Integer(w[3]), Integer(w[4]), cmd = "holdr")
        else if (cmd = "taps") {
            loop Integer(w[6])
                SoakHold(sc, w[2], Integer(w[3]) + (A_Index - 1) * (Integer(w[4]) + Integer(w[5])), Integer(w[4]), false)
        }
    }
    return scens
}

SoakHold(sc, key, at, len, repeat) {
    sc.ev.Push({at: at, kind: "down", key: key, exe: ""})
    if repeat {
        t := at + 500
        while (t < at + len) {
            sc.ev.Push({at: t, kind: "repeat", key: key, exe: ""})
            t += 33
        }
    }
    sc.ev.Push({at: at + len, kind: "up", key: key, exe: ""})
}

; Stable by time: events at one ms keep file order.
SoakSort(list) {
    i := 2
    while (i <= list.Length) {
        e := list[i]
        j := i - 1
        while (j >= 1 && list[j].at > e.at) {
            list[j + 1] := list[j]
            j--
        }
        list[j + 1] := e
        i++
    }
}

SoakInput(key, down) {
    if IsMouseButton(key)
        return TestMouse(key, down)
    vk := GetKeyVK(key)
    info := Buffer(24, 0)
    NumPut("UInt", vk, info, 0)
    NumPut("UInt", GetKeySC(key), info, 4)
    NumPut("UInt", (down ? 0 : 0x80) | (key = "LWin" ? 1 : 0), info, 8)
    return Engine.ReadKey(down ? 0x100 : 0x101, info)
}

SoakKind(macro) {
    mode := String(Field(macro, "playMode", "once"))
    if (mode = "once")
        return Engine.IsRunOnce(macro) ? "run-once" : "once-hold"
    return mode = "whileHeld" ? "held" : "other"
}

; One scenario on one output. Each ms runs in the native playback order:
; pick the config, seed, the fingers, poll, advance, the owed presses.
SoakRun(text, sc, out, errs) {
    outName := out = "rp2350" ? "board" : out
    Engine.outputMode := out
    Engine.profile := JSON.Parse(text)
    Engine.liveId := Chr(1)
    Engine.hooks := Map()
    Engine.physDown := Map()
    Engine.repeatSeen := Map()
    Engine.repeatLogged := Map()
    Engine.lastSwallow := Map()
    Engine.redo := Map()
    Engine.lostSeen := Map()
    Engine.owed := Map()
    Engine.prevDown := Map()
    Engine.prevButton := Map()
    Engine.runners := []
    Engine.pending := []
    Engine.frontKnown := true
    front := sc.front
    tracks := Map()
    order := []
    i := 1
    now := 0
    while (now <= sc.end) {
        moved := false
        pushed := false
        while (i <= sc.ev.Length && sc.ev[i].at = now && (sc.ev[i].kind = "push" || sc.ev[i].kind = "focus")) {
            if (sc.ev[i].kind = "push") {
                pushed := true
            } else {
                front := sc.ev[i].exe
                moved := true
            }
            i++
        }
        Engine.frontExe := front
        Engine.focusCache := Map()
        if !pushed
            Engine.ApplyLiveConfig()
        live := Map()
        for m in Field(Engine.profile, "macros", []) {
            id := String(Field(m, "id", ""))
            live[id] := true
            if !tracks.Has(id) {
                trig := Field(m, "trigger", Map())
                tracks[id] := {name: String(Field(m, "name", "")), key: NormKey(Field(trig, "button", "")),
                    kind: SoakKind(m), starts: 0, held: 0, blocked: 0, stopsHeld: 0,
                    inPress: false, ok: false, at: 0, pressStarts: 0, playing: false, gap: 0}
                order.Push(id)
            }
        }
        if moved
            for id, t in tracks
                t.ok := false
        while (i <= sc.ev.Length && sc.ev[i].at = now) {
            e := sc.ev[i++]
            if (e.kind = "push" || e.kind = "focus")
                continue
            SoakInput(e.key, e.kind != "up")
            if (e.kind = "repeat")
                continue
            for id, t in tracks {
                if (t.key != NormKey(e.key))
                    continue
                if (e.kind = "down") {
                    t.inPress := true
                    t.ok := live.Has(id)
                    t.at := now
                    t.pressStarts := 0
                    t.gap := 0
                    t.playing := Engine.HasRunner(id)
                } else {
                    SoakClose(sc, outName, t, now, errs)
                    t.inPress := false
                    t.ok := false
                }
            }
        }
        ; A reload runs in Tick, after the hook callbacks of this ms.
        if pushed {
            Engine.profile := JSON.Parse(text)
            Engine.liveId := Chr(1)
            Engine.ApplyLiveConfig()
        }
        Engine.capture := []
        TestTick(now)
        for line in Engine.capture {
            if !RegExMatch(line, " id=(\S+)", &mm) || !tracks.Has(mm[1])
                continue
            t := tracks[mm[1]]
            if (SubStr(line, 1, 13) = "runner start ") {
                t.starts++
                if InStr(line, "reason=held-press")
                    t.held++
                if t.inPress
                    t.pressStarts++
            } else if (SubStr(line, 1, 14) = "start blocked ") {
                t.blocked++
            } else if (SubStr(line, 1, 12) = "runner stop ") {
                if InStr(line, "trigger=down")
                    t.stopsHeld++
            }
        }
        Engine.capture := ""
        for id, t in tracks
            if (t.kind = "held" && t.inPress && t.ok && now >= t.at + 3 && !Engine.HasRunner(id))
                t.gap++
        now++
    }
    rows := ""
    for id in order {
        t := tracks[id]
        if (t.starts || t.blocked)
            rows .= Format("{} {} {} starts={} held-press={} blocked={} stops-held={}`n",
                sc.name, outName, t.name, t.starts, t.held, t.blocked, t.stopsHeld)
    }
    Engine.capture := []
    Engine.StopAll()
    Engine.RemoveDone()
    Engine.capture := ""
    return rows
}

SoakClose(sc, outName, t, now, errs) {
    if !t.ok
        return
    where := sc.name " " outName " " t.name
    if (t.kind = "run-once") {
        want := t.playing ? 0 : 1
        if (t.pressStarts != want)
            errs.Push(where ": " (want ? "a press did not play once" : "a press while playing started it") " at " t.at " ms")
    } else if (t.kind = "once-hold") {
        if (t.pressStarts != 1)
            errs.Push(where ": a press did not start exactly once at " t.at " ms")
    } else if (t.kind = "held") {
        if (t.pressStarts > 1)
            errs.Push(where ": stopped and started again inside one hold at " t.at " ms")
        if (t.gap > 0)
            errs.Push(where ": not playing for part of the hold at " now " ms")
    }
}

; One low-level keyboard event, built like KBDLLHOOKSTRUCT.
TestKey(key, down) {
    vk := key = "LWin" ? 0x5B : Ord(StrUpper(key))
    info := Buffer(24, 0)
    NumPut("UInt", vk, info, 0)
    NumPut("UInt", (down ? 0 : 0x80) | (key = "LWin" ? 1 : 0), info, 8)
    return Engine.ReadKey(down ? 0x100 : 0x101, info)
}

; One low-level mouse button event, built like MSLLHOOKSTRUCT.
TestMouse(key, down) {
    info := Buffer(A_PtrSize = 8 ? 32 : 24, 0)
    if (key = "XButton1" || key = "XButton2") {
        NumPut("UInt", (key = "XButton1" ? 1 : 2) << 16, info, 8)
        msg := down ? 0x20B : 0x20C
    } else if (key = "MButton")
        msg := down ? 0x207 : 0x208
    else
        msg := key = "LButton" ? (down ? 0x201 : 0x202) : (down ? 0x204 : 0x205)
    return Engine.ReadMouse(msg, info)
}

TestTrigger(key, down) {
    return IsMouseButton(key) ? TestMouse(key, down) : TestKey(key, down)
}

; Tick order without the clock: poll, flush, advance, flush, drop finished.
; Returns how many runners this millisecond started.
TestTick(now) {
    started := 0
    Engine.sendNow := Map()
    Engine.focusCache := Map()
    Engine.PollTriggers()
    Engine.RemoveDone()
    started += Engine.pending.Length
    Engine.FlushPending()
    for r in Engine.runners
        r.Advance(now)
    started += Engine.pending.Length
    Engine.FlushPending()
    Engine.RemoveDone()
    Engine.PollOwed()
    return started
}

; Run Once on a held trigger. Windows repeats a held key after 500 ms, then
; every 33 ms, with no up. That is never a new edge. Only an up and then a down
; starts it again, and not while the last pass is still playing. A board
; swallows middle and side triggers like keys.
RunOnceChecks(fails) {
    saved := Map("rawOk", Engine.rawOk, "mouse", Engine.hMouseHook, "out", Engine.outputMode,
        "profile", Engine.profile, "known", Engine.frontKnown, "front", Engine.frontExe, "dry", Engine.dry)
    Engine.BuildVkMap()
    Engine.dry := true
    Engine.rawOk := true
    Engine.hMouseHook := 1
    Engine.frontKnown := true
    Engine.frontExe := "helldivers2.exe"
    Engine.hidEcho := []
    strat := "[{'type':'key','action':'down','key':'c'},{'type':'wait','ms':30},{'type':'key','action':'down','key':'f'},{'type':'wait','ms':30},{'type':'key','action':'up','key':'f'},{'type':'wait','ms':30},{'type':'key','action':'down','key':'d'},{'type':'wait','ms':30},{'type':'key','action':'up','key':'d'},{'type':'wait','ms':30},{'type':'key','action':'up','key':'c'}]"
    for key in ["4", "LWin", "XButton1", "XButton2"] {
        for mode in ["software", "rp2350"] {
            orbital := J("{'id':'orbital','name':'Orbital','enabled':true,'advanced':true,'playMode':'once','releaseStop':'nextUp','trigger':{'kind':'key','button':'" key "'},'steps':[],'blocks':[{'type':'steps','steps':" strat "}]}")
            Engine.outputMode := mode
            Engine.profile := Map("macros", [orbital])
            Engine.hooks := Map(key, true)
            Engine.physDown := Map()
            Engine.lastSwallow := Map()
            Engine.repeatSeen := Map()
            Engine.repeatLogged := Map()
            ; Raw Input is up and sees the button held the whole time: no ups.
            Engine.rawOn := true
            Engine.rawUpCount := Map()
            Engine.rawAtDown := Map()
            Engine.redo := Map()
            Engine.SeedTriggers()
            starts := 0, downs := 0, swallowed := 0
            repeatAt := 600
            now := 0
            while (now < 4000) {
                fed := false
                if (now = 100) {
                    fed := true
                    ll := TestTrigger(key, true)
                } else if (now = repeatAt && now < 3100) {
                    fed := true
                    ll := TestTrigger(key, true)
                    repeatAt += 33
                } else if (now = 3100)
                    TestTrigger(key, false)
                if fed {
                    downs++
                    ; Software swallows with the $* hotkey, a board with the LL hook.
                    if (mode = "software" ? (SubStr(HookSpec(key), 1, 1) = "$" && !ll) : ll)
                        swallowed++
                }
                starts += TestTick(now)
                now++
            }
            label := mode " " key
            fails := Check(downs = 77 && starts = 1, label " held 3 s with repeats plays exactly once", fails)
            fails := Check(!Engine.HasRunner("orbital"), label " run once finishes", fails)
            fails := Check(swallowed = downs, label " swallows every down, repeats too", fails)
            Engine.StopAll()
            Engine.RemoveDone()
        }
    }

    ; A real up and down while the pass plays is ignored. After it is done, the
    ; next press plays it again. Advanced and plain steps alike.
    for adv in [true, false] {
        body := adv ? "'advanced':true,'steps':[],'blocks':[{'type':'steps','steps':" strat "}]" : "'basic':false,'steps':" strat
        orbital := J("{'id':'orbital','name':'Orbital','enabled':true,'playMode':'once','trigger':{'kind':'side','button':'XButton1'}," body "}")
        Engine.outputMode := "rp2350"
        Engine.profile := Map("macros", [orbital])
        Engine.hooks := Map("XButton1", true)
        Engine.physDown := Map()
        Engine.SeedTriggers()
        starts := 0
        mid := 0
        now := 0
        while (now < 1500) {
            if (now = 100 || now = 200 || now = 1000)
                TestMouse("XButton1", true)
            else if (now = 150 || now = 250 || now = 1100)
                TestMouse("XButton1", false)
            starts += TestTick(now)
            if (now = 220)
                mid := starts
            now++
        }
        label := adv ? "advanced" : "plain"
        fails := Check(mid = 1, label " a second press mid-run does not restart it", fails)
        fails := Check(starts = 2 && !Engine.HasRunner("orbital"), label " a press after the pass is done plays it again", fails)
        Engine.StopAll()
        Engine.RemoveDone()
    }

    ; The hook loses mouse 4's up, and Raw Input still sees it. The next down is
    ; a new press, not a dead one. While the pass plays it is ignored like any
    ; press. Without Raw Input, or for a key, it stays a repeat: that press is
    ; dead, the one after its up plays.
    Engine.rawBoard := Map(1, false)
    for mode in ["software", "rp2350"] {
        for kind in ["after the pass", "mid-run", "no raw input", "a key"] {
            key := kind = "a key" ? "4" : "XButton1"
            again := kind = "mid-run" ? 200 : 1000
            orbital := J("{'id':'orbital','name':'Orbital','enabled':true,'advanced':true,'playMode':'once','trigger':{'kind':'key','button':'" key "'},'steps':[],'blocks':[{'type':'steps','steps':" strat "}]}")
            Engine.outputMode := mode
            Engine.profile := Map("macros", [orbital])
            Engine.hooks := Map(key, true)
            Engine.physDown := Map()
            Engine.rawOn := kind != "no raw input"
            Engine.rawUpCount := Map()
            Engine.rawAtDown := Map()
            Engine.redo := Map()
            Engine.lostSeen := Map()
            Engine.SeedTriggers()
            starts := 0, mid := -1, dipped := false
            now := 0
            while (now < 3000) {
                if (now = 100 || now = again || now = 2000)
                    TestTrigger(key, true)
                ; A lost release lets go for this one poll, then presses again.
                if (now = again)
                    dipped := !Engine.KeyDown(key)
                ; The real up: Raw Input counts it, the hook never reports it.
                if (now = again - 50)
                    Engine.NoteRaw(1, 0x80)
                if (now = again + 100 || now = 2100) {
                    Engine.NoteRaw(1, 0x80)
                    TestTrigger(key, false)
                }
                starts += TestTick(now)
                if (now = again + 20)
                    mid := Engine.HasRunner("orbital") ? starts : -1
                now++
            }
            label := mode " lost release " kind
            want := kind = "after the pass" ? 3 : 2
            fails := Check(starts = want, label ": starts " want, fails)
            lostWanted := kind = "after the pass" || kind = "mid-run"
            fails := Check(dipped = lostWanted, label (lostWanted ? ": the down is a new press" : ": the down stays a repeat"), fails)
            if (kind = "mid-run")
                fails := Check(mid = 1, label ": the pass plays on, one start", fails)
            fails := Check(!Engine.KeyDown(key) && !Engine.HasRunner("orbital"), label ": ends up and idle", fails)
            Engine.StopAll()
            Engine.RemoveDone()
        }
    }
    Engine.rawUpCount := Map()
    Engine.NoteRaw(1, 0x2A0)
    fails := Check(Engine.RawUps("MButton") = 1 && Engine.RawUps("XButton1") = 1 && Engine.RawUps("XButton2") = 1, "raw ups of middle, mouse 4 and mouse 5 count", fails)
    Engine.NoteRaw(0, 0x80)
    fails := Check(Engine.RawUps("XButton1") = 1, "an injected up has no device and does not count", fails)
    Engine.rawBoard[2] := RawNameIsBoard("\\?\HID#VID_CAFE&PID_4004&MI_01#8&1&0000")
    Engine.NoteRaw(2, 0x80)
    fails := Check(Engine.RawUps("XButton1") = 1, "the board's own mouse 4 up does not count", fails)
    fails := Check(!RawNameIsBoard("\\?\HID#VID_046D&PID_C539&MI_01#x"), "a real mouse is not the board", fails)
    Engine.rawOn := false
    Engine.rawUpCount := Map()
    Engine.rawAtDown := Map()
    Engine.rawBoard := Map()
    Engine.redo := Map()

    Engine.outputMode := "rp2350"
    Engine.hooks := Map("XButton1", true)
    Engine.physDown := Map()
    fails := Check(TestMouse("XButton1", true) && TestMouse("XButton1", false), "a board swallows a mouse 4 trigger, down and up", fails)
    fails := Check(!TestMouse("XButton2", true), "a board passes a side button that is not a trigger", fails)
    TestMouse("XButton2", false)
    Engine.hooks["LButton"] := true
    fails := Check(!TestMouse("LButton", true), "left click always passes", fails)
    TestMouse("LButton", false)
    Engine.hooks.Delete("LButton")
    Engine.QueueHidEcho("XButton1", true)
    fails := Check(!TestMouse("XButton1", true) && !Engine.KeyDown("XButton1"), "the board's own mouse 4 passes and is not a finger", fails)
    Engine.hidEcho := []
    Engine.passKeys := Map("XButton1", 1)
    fails := Check(!TestMouse("XButton1", true), "a once-lock pass lets mouse 4 through on a board", fails)
    TestMouse("XButton1", false)
    Engine.passKeys := Map()
    Engine.outputMode := "software"
    fails := Check(!TestMouse("XButton1", true) && HookSpec("XButton1") = "$*XButton1", "software leaves the swallow to the $* hotkey", fails)
    TestMouse("XButton1", false)

    logged := ""
    try logged := FileRead(Engine.logPath, "UTF-8")
    fails := Check(InStr(logged, "trigger XButton1 repeat ignored macro=" Chr(34) "Orbital"), "a held trigger logs repeat ignored", fails)
    fails := Check(InStr(logged, "action=start swallow=yes"), "a trigger line says it was swallowed", fails)
    fails := Check(InStr(logged, "reason=playing"), "a press mid-run logs reason=playing", fails)
    fails := Check(InStr(logged, "trigger XButton1 lost release, new press macro=" Chr(34) "Orbital"), "a lost release logs a new press", fails)

    Engine.hooks := Map()
    Engine.physDown := Map()
    Engine.lastSwallow := Map()
    Engine.repeatSeen := Map()
    Engine.repeatLogged := Map()
    Engine.rawOk := saved["rawOk"]
    Engine.hMouseHook := saved["mouse"]
    Engine.outputMode := saved["out"]
    Engine.profile := saved["profile"]
    Engine.frontKnown := saved["known"]
    Engine.frontExe := saved["front"]
    Engine.dry := saved["dry"]
    return fails
}

; engine-native\tests\fixtures\create-paths.json is written by the UI: one
; macro per way to make a Run Once, and Play > Repeat x3. engine-command-test
; plays the same file with the same expectations.
CreatePathChecks(fails) {
    text := ""
    try text := FileRead(A_ScriptDir "\..\engine-native\tests\fixtures\create-paths.json", "UTF-8")
    if (text = "")
        return Check(false, "create-paths.json readable", fails)
    macros := JSON.Parse(text)["macros"]
    hold := [10, 610], tap := [10, 40], twice := [10, 40, 90, 120], heldAgain := [10, 40, 90, 410]
    for path in [["key path", "g"], ["new path", "h"]] {
        name := path[1], key := path[2]
        run := CreatePathPlay(macros, key, hold)
        fails := Check(run["starts"] = 1 && run["space"] = 1 && run["e"] = 1, name ": held 600 ms plays one pass", fails)
        run := CreatePathPlay(macros, key, tap)
        fails := Check(run["starts"] = 1 && run["space"] = 1 && run["e"] = 1, name ": a 30 ms tap plays the whole pass", fails)
        run := CreatePathPlay(macros, key, twice)
        fails := Check(run["starts"] = 1 && run["space"] = 1 && run["e"] = 1, name ": a press while it plays is ignored", fails)
        run := CreatePathPlay(macros, key, heldAgain)
        fails := Check(run["starts"] = 1 && run["space"] = 1 && run["e"] = 1, name ": a held press while it plays is ignored", fails)
    }
    run := CreatePathPlay(macros, "j", tap)
    fails := Check(run["starts"] = 1 && run["space"] = 3, "repeat x3: a tap plays 3 times, not 9", fails)
    run := CreatePathPlay(macros, "j", hold)
    fails := Check(run["starts"] = 1 && run["space"] = 3, "repeat x3: held plays 3 times, not 9", fails)
    return fails
}

; engine-native\tests\fixtures\hold-repress.json: one pass is space, 150 ms,
; e, about 190 ms, releaseStop nextUp. Up at 40 and down again at 90 lands in
; the pass that finishes after the release. A While Held, Swap After or One
; Press block saved as whileHeld went silent there until the up (#95). The
; press is kept and starts as held-press when that pass ends, so it plays like
; the same macro saved as once. engine-command-test plays the same file.
HoldRepressChecks(fails) {
    text := ""
    try text := FileRead(A_ScriptDir "\..\engine-native\tests\fixtures\hold-repress.json", "UTF-8")
    if (text = "")
        return Check(false, "hold-repress.json readable", fails)
    macros := JSON.Parse(text)["macros"]
    hold := [10, 610], heldAgain := [10, 40, 90, 410]
    ; name, key, space held, starts, held-press, space, e. One Press plays its
    ; whole pass after a release and the re-press keeps the same runner going.
    cases := [["While Held, whileHeld", "k", 4, 2, 1, 3, 1],
        ["While Held, once", "l", 4, 2, 0, 3, 1],
        ["Swap After, whileHeld", "m", 4, 2, 1, 3, 1],
        ["One Press, whileHeld", "n", 3, 1, 0, 2, 2]]
    for output in ["software", "rp2350"] {
        for c in cases {
            name := c[1] " " output
            run := CreatePathPlay(macros, c[2], hold, output)
            fails := Check(run["starts"] = 1 && run["space"] = c[3] && run["e"] = 3, name ": held 600 ms plays every pass", fails)
            run := CreatePathPlay(macros, c[2], heldAgain, output)
            fails := Check(run["starts"] = c[4] && run["held"] = c[5] && run["space"] = c[6] && run["e"] = c[7], name ": a re-press while finishing plays while held", fails)
        }
    }
    return fails
}

; One trigger, one tick per ms, publishing like the real loop. edges are
; down/up times. Counts starts, held-press starts, and the space and e downs
; sent.
CreatePathPlay(macros, key, edges, output := "software") {
    saved := Map("rawOk", Engine.rawOk, "mouse", Engine.hMouseHook, "out", Engine.outputMode,
        "profile", Engine.profile, "known", Engine.frontKnown, "front", Engine.frontExe, "dry", Engine.dry)
    Engine.BuildVkMap()
    Engine.dry := true
    Engine.rawOk := true
    Engine.hMouseHook := 1
    Engine.frontKnown := true
    Engine.frontExe := "game.exe"
    Engine.hidEcho := []
    Engine.outputMode := output
    Engine.profile := Map("macros", macros)
    Engine.hooks := Map(key, true)
    Engine.physDown := Map()
    Engine.lastSwallow := Map()
    Engine.repeatSeen := Map()
    Engine.repeatLogged := Map()
    Engine.rawOn := true
    Engine.rawUpCount := Map()
    Engine.rawAtDown := Map()
    Engine.redo := Map()
    Engine.sent := []
    Engine.applied := Map()
    Engine.capture := []
    Engine.SeedTriggers()
    starts := 0
    now := 0
    while (now < 1500) {
        for i, at in edges
            if (at = now)
                TestTrigger(key, Mod(i, 2) = 1)
        starts += TestTick(now)
        Engine.Publish(now)
        now++
    }
    held := 0
    for line in Engine.capture
        if InStr(line, "runner start") && InStr(line, "reason=held-press")
            held++
    Engine.capture := ""
    space := 0, e := 0
    for item in Engine.sent {
        for k in item["down"] {
            if (k = "space")
                space++
            else if (k = "e")
                e++
        }
    }
    Engine.StopAll()
    Engine.RemoveDone()
    Engine.sent := []
    Engine.applied := Map()
    Engine.hooks := Map()
    Engine.physDown := Map()
    Engine.lastSwallow := Map()
    Engine.repeatSeen := Map()
    Engine.repeatLogged := Map()
    Engine.rawOk := saved["rawOk"]
    Engine.hMouseHook := saved["mouse"]
    Engine.outputMode := saved["out"]
    Engine.profile := saved["profile"]
    Engine.frontKnown := saved["known"]
    Engine.frontExe := saved["front"]
    Engine.dry := saved["dry"]
    return Map("starts", starts, "held", held, "space", space, "e", e)
}
