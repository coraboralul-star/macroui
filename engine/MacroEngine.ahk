#Requires AutoHotkey v2.0
#SingleInstance Force
SendMode("Input")
SendLevel(1)
SetKeyDelay(-1, -1)
SetMouseDelay(-1)
SetDefaultMouseSpeed(0)
SetStoreCapsLockMode(false)
ListLines(false)
; A fast loop is normal here. This stops the "too many hotkeys" dialog.
A_HotkeyInterval := 0

#Include JSON.ahk

; One process, no window. Macros advance from a high-resolution clock.
; SetTimer cannot keep a 5 ms wait at 5 ms on Windows.
; Pipe: \\.\pipe\MacroUI  (message-mode JSON, one object per write)

if (A_Args.Length && A_Args[1] = "--self-test")
    ExitApp(SelfTest())

A_IconHidden := true
ListLines(false)
KeyHistory(0)
InstallKeybdHook()
InstallPhysical()
DllCall("Winmm.dll\timeBeginPeriod", "UInt", 1)
ProcessSetPriority("AboveNormal")
OnExit(OnEngineExit)
Engine.LoadDisk()
Engine.OpenPipe()
Engine.Loop()

OnEngineExit(*) {
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
    if (v is String && v != "")
        return Number(v)
    return def
}

SafeKey(name) {
    name := String(name)
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

; $* swallows the physical key with any modifier held, and Send will not re-trigger it.
HookSpec(key) {
    if (key = "``")
        return "$*``"
    return "$*" key
}

; Swallows the trigger key. The low-level hook records the real finger.
; This still records it too, so a release is seen even if that hook is behind.
TriggerHook(name) {
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
    ; The mouse hook owns side buttons. The hotkey also fires for buttons we send back out.
    if (IsMouseButton(raw) && Engine.hMouseHook)
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
    if !Engine.rawOk
        Engine.Log("physical hook failed")
}

PhysKeyProc(nCode, wParam, lParam) {
    if (nCode >= 0 && Engine.rawOk)
        try Engine.ReadKey(wParam, lParam)
    return DllCall("CallNextHookEx", "Ptr", 0, "Int", nCode, "Ptr", wParam, "Ptr", lParam, "Ptr")
}

PhysMouseProc(nCode, wParam, lParam) {
    ; Movement and the wheel are not triggers. Handling them here cuts the mouse rate.
    if (nCode >= 0 && Engine.hMouseHook && wParam != 0x200 && wParam != 0x20A && wParam != 0x20E)
        try Engine.ReadMouse(wParam, lParam)
    return DllCall("CallNextHookEx", "Ptr", 0, "Int", nCode, "Ptr", wParam, "Ptr", lParam, "Ptr")
}

IsMouseButton(key) {
    return key = "LButton" || key = "RButton" || key = "MButton" || key = "XButton1" || key = "XButton2"
}

Alias(name) {
    static table := Map(
        "Space", "space", "Tab", "tab", "Enter", "enter", "Escape", "escape", "Backspace", "backspace",
        "Up", "up", "Down", "down", "Left", "left", "Right", "right",
        "Ins", "Insert", "Del", "Delete",
        "LShift", "shift", "RShift", "shift", "Shift", "shift",
        "LControl", "ctrl", "RControl", "ctrl", "Control", "ctrl",
        "LAlt", "alt", "RAlt", "alt", "Alt", "alt"
    )
    name := String(name)
    if (name = "")
        return ""
    if table.Has(name)
        return table[name]
    if RegExMatch(name, "i)^F(\d+)$", &hit)
        return "f" hit[1]
    if (StrLen(name) = 1)
        return StrLower(name)
    return name
}

CompileBlocks(blocks) {
    steps := []
    for block in blocks {
        if !(block is Map)
            continue
        kind := String(Field(block, "type", ""))
        child := Field(block, "steps", [])
        if !(child is Array)
            child := []
        if (kind = "whileHeld") {
            mute := Field(block, "mute", [])
            if !(mute is Array)
                mute := []
            steps.Push(Map("type", "holdLoop", "steps", child, "mute", mute))
        }
        else if (kind = "ifShort")
            steps.Push(Map("type", "ifShort", "underMs", AsNum(Field(block, "underMs", 150), 150), "minCycles", AsNum(Field(block, "minCycles", 3), 3), "steps", child))
        else if (kind = "then")
            steps.Push(Map("type", "burst", "forMs", AsNum(Field(block, "forMs", 0), 0), "steps", child))
        else if (kind = "repeat")
            steps.Push(Map("type", "repeat", "count", AsNum(Field(block, "count", 1), 1), "steps", child))
        else if (kind = "wait")
            steps.Push(Map("type", "wait", "ms", AsNum(Field(block, "ms", 0), 0)))
        else if (kind = "steps")
            steps.Push(Map("type", "repeat", "count", 1, "steps", child))
        else if (kind = "tapHold") {
            watch := Field(block, "watch", [])
            if !(watch is Array)
                watch := []
            steps.Push(Map("type", "edgeHold", "key", SafeKey(Field(block, "key", "z")), "watch", watch, "armMs", AsNum(Field(block, "armMs", 5), 5), "gapMs", AsNum(Field(block, "gapMs", 80), 80)))
        }
    }
    return steps
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
        this.done := false
        this.moveX := 0
        this.moveY := 0
        mode := modeOverride != "" ? String(modeOverride) : String(Field(macro, "playMode", "once"))
        this.mode := mode
        this.trigger := ""
        trig := Field(macro, "trigger", Map())
        if (trig is Map)
            this.trigger := SafeKey(Field(trig, "button", ""))
        this.cycles := 0
        this.heldMs := 0
        this.startedAt := -1
        repeats := 1
        if (mode = "repeat")
            repeats := Max(1, Floor(AsNum(Field(macro, "repeatCount", 1), 1)))
        else if (mode = "whileHeld" || mode = "toggle")
            repeats := 0
        blocks := Field(macro, "blocks", [])
        if (Field(macro, "advanced", false) && (blocks is Array) && blocks.Length > 0)
            this.stack.Push({steps: CompileBlocks(blocks), index: 1, repeatsLeft: 1})
        else {
            steps := Field(macro, "steps", [])
            if !(steps is Array)
                steps := []
            this.stack.Push({steps: steps, index: 1, repeatsLeft: repeats})
        }
    }

    MarkDone() {
        if this.done
            return
        this.done := true
        frames := []
        if (this.stack is Array) {
            for frame in this.stack
                frames.Push(frame)
        }
        for frame in frames
            this.ReleaseFrameMute(frame)
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

    Advance(now) {
        this.moveX := 0
        this.moveY := 0
        this.pressedNow := Map()
        if this.done
            return
        for k, releaseAt in this.pulses.Clone()
            if (releaseAt <= now && this.pulses.Has(k))
                this.pulses.Delete(k)
        if (this.waitUntil != 0 && now < this.waitUntil)
            return
        this.waitUntil := 0
        if (this.startedAt < 0)
            this.startedAt := now
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
                    this.TickEdge(frame, now)
                    return
                }
                if (frame.HasProp("holdLoop") && frame.holdLoop) {
                    this.cycles++
                    this.heldMs := now - this.startedAt
                    if (this.trigger != "" && Engine.KeyDown(this.trigger)) {
                        if (steps.Length = 0) {
                            this.waitUntil := now + 1
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
            if (this.Exec(step, now) = "wait")
                return
        }
        if (this.waitUntil = 0)
            this.waitUntil := now + 1
    }

    Exec(step, now) {
        if !(step is Map)
            return ""
        kind := String(Field(step, "type", ""))
        if (kind = "wait") {
            ms := AsNum(Field(step, "ms", 0), 0) / this.speed
            this.waitUntil := now + Max(1, Round(ms))
            return "wait"
        }
        if (kind = "key" || kind = "mouse") {
            key := SafeKey(kind = "key" ? Field(step, "key", "") : Field(step, "button", ""))
            if (key = "")
                return ""
            action := String(Field(step, "action", "tap"))
            if (action = "down") {
                this.holds[key] := true
                this.pressedNow[key] := true
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
                ; A down and up in the same tick would never be sent. Hold it for one tick.
                if just {
                    this.pulses[key] := now + 1
                    this.waitUntil := now + 1
                    return "wait"
                }
            } else {
                hold := AsNum(Field(step, "holdMs", 1), 1) / this.speed
                this.pulses[key] := now + Max(1, Round(hold))
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
        if (kind = "repeat") {
            count := Floor(AsNum(Field(step, "count", 1), 1))
            if (count < 0)
                count := 0
            child := Field(step, "steps", [])
            if !(child is Array)
                child := []
            this.stack.Push({steps: child, index: 1, repeatsLeft: count})
            return ""
        }
        if (kind = "edgeHold") {
            key := SafeKey(Field(step, "key", ""))
            watch := Field(step, "watch", [])
            if !(watch is Array)
                watch := []
            prev := Map()
            for item in watch {
                name := SafeKey(Alias(String(item)))
                if (name = "")
                    name := SafeKey(item)
                if (name != "")
                    prev[name] := false
            }
            this.stack.Push({steps: [], index: 1, repeatsLeft: 1, edgeHold: true, edgeKey: key, edgePrev: prev, edgePhase: "down", edgeArm: AsNum(Field(step, "armMs", 5), 5), edgeGap: AsNum(Field(step, "gapMs", 80), 80), edgeRepeatAt: now + Engine.KeyDelayMs()})
            if (key != "")
                this.holds[key] := true
            if (this.trigger != "" && this.trigger != key)
                this.holds[this.trigger] := true
            this.waitUntil := now + 5
            return "wait"
        }
        if (kind = "holdLoop") {
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
            frame := {steps: child, index: 1, repeatsLeft: 1, holdLoop: true, mute: clean, muteOn: false}
            this.stack.Push(frame)
            this.ArmMute(frame)
            return ""
        }
        if (kind = "ifShort") {
            under := AsNum(Field(step, "underMs", 150), 150)
            if (this.heldMs >= under)
                return ""
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
                Engine.StartRunner(target, "once", false)
            return ""
        }
        return ""
    }

    DropHold(key) {
        if (key = "")
            return
        if this.holds.Has(key)
            this.holds.Delete(key)
        if this.pulses.Has(key)
            this.pulses.Delete(key)
    }

    TickEdge(frame, now) {
        key := frame.edgeKey
        if (frame.edgePhase = "arm") {
            this.DropHold(key)
            if (this.trigger != key)
                this.DropHold(this.trigger)
            frame.edgePhase := "gap"
            this.waitUntil := now + Max(1, Round(frame.edgeGap / this.speed))
            return
        }
        if (frame.edgePhase = "gap") {
            if (this.trigger != "" && !Engine.KeyDown(this.trigger)) {
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
            if (isDown && !wasDown && !pulsed) {
                frame.edgePhase := "arm"
                pulsed := true
            }
            frame.edgePrev[name] := isDown
        }
        if (frame.edgePhase = "arm") {
            this.waitUntil := now + Max(1, Round(frame.edgeArm / this.speed))
            return
        }
        this.RepeatHold(frame, now)
        this.waitUntil := now + 5
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
        if (n > 2000)
            return 2000
        if (n < -2000)
            return -2000
        return n
    }
}

class Engine {
    static runners := []
    static muted := Map()
    static pending := []
    static applied := Map()
    static prevDown := Map()
    static profile := Map()
    static armed := false
    static dry := false
    static finger := Map()
    static sent := []
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
    static echo := 0x56454E44
    static vkName := Map()
    static rawOk := false
    static keyProc := 0
    static mouseProc := 0
    static hKeyHook := 0
    static hMouseHook := 0
    static qpcFreq := 0
    static qpcOrigin := 0
    static hTimer := 0

    static Loop() {
        while true {
            Critical("On")
            this.busy := true
            try this.Tick()
            catch as err
                this.Log(err.Message)
            this.busy := false
            Critical("Off")
            this.Pace()
        }
    }

    static NotePhysical(name, down) {
        name := String(name)
        if (name = "")
            return
        this.physDown[name] := down
        folded := StrLower(name)
        if (folded != name)
            this.physDown[folded] := down
        for key, _ in this.hooks
            if (StrLower(key) = folded)
                this.physDown[key] := down
    }

    static BuildVkMap() {
        this.vkName := Map()
        loop 255 {
            vk := A_Index
            label := ""
            try label := GetKeyName(Format("vk{:X}", vk))
            button := SafeKey(Alias(label))
            if (button != "")
                this.vkName[vk] := button
        }
        for vk, button in Map(0x10, "shift", 0xA0, "shift", 0xA1, "shift", 0x11, "ctrl", 0xA2, "ctrl", 0xA3, "ctrl", 0x12, "alt", 0xA4, "alt", 0xA5, "alt", 0x20, "space", 0x0D, "enter")
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
    }

    static SyncMouseHook() {
        need := false
        for key, _ in this.hooks
            if IsMouseButton(key)
                need := true
        if (need && !this.hMouseHook && this.mouseProc) {
            module := DllCall("GetModuleHandle", "Ptr", 0, "Ptr")
            this.hMouseHook := DllCall("SetWindowsHookExW", "Int", 14, "Ptr", this.mouseProc, "Ptr", module, "UInt", 0, "Ptr")
        } else if (!need && this.hMouseHook) {
            DllCall("UnhookWindowsHookEx", "Ptr", this.hMouseHook)
            this.hMouseHook := 0
        }
    }

    ; Only the real release is taken from here. Presses still come from the hotkey.
    ; A sent key must not be able to set the finger back down after a tap.
    static ReadKey(msg, info) {
        flags := NumGet(info, 8, "UInt")
        if (flags & 0x12)
            return
        up := (msg = 0x101 || msg = 0x105 || (flags & 0x80))
        vk := NumGet(info, 0, "UInt")
        if (vk = 0x0D && (flags & 0x01))
            this.NotePhysical("NumpadEnter", !up)
        else if this.vkName.Has(vk)
            this.NotePhysical(this.vkName[vk], !up)
    }

    static ReadMouse(which, info) {
        flags := NumGet(info, 12, "UInt")
        if (flags & 0x03)
            return
        extraOff := A_PtrSize = 8 ? 24 : 20
        if (NumGet(info, extraOff, "Ptr") = this.echo)
            return
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
        if (name != "")
            this.NotePhysical(name, down)
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

    ; Block without spinning, then the last 2 ms are a short spin so the deadline is not late.
    static HrtWait(ms) {
        if (ms < 0.3)
            return
        if !this.hTimer
            this.hTimer := DllCall("CreateWaitableTimerExW", "Ptr", 0, "Ptr", 0, "UInt", 2, "UInt", 0x1F0003, "Ptr")
        if !this.hTimer {
            DllCall("Sleep", "UInt", Max(1, Round(ms)))
            return
        }
        handles := Buffer(A_PtrSize, 0)
        NumPut("Ptr", this.hTimer, handles)
        deadline := this.Now() + ms
        loop {
            remain := deadline - this.Now()
            if (remain < 0.3)
                return
            due := -Max(1, Round(remain * 10000))
            DllCall("SetWaitableTimer", "Ptr", this.hTimer, "Int64*", &due, "Int", 0, "Ptr", 0, "Ptr", 0, "Int", 0)
            result := DllCall("MsgWaitForMultipleObjects", "UInt", 1, "Ptr", handles.Ptr, "Int", 0, "UInt", Max(1, Round(remain) + 2), "UInt", 0x04FF, "UInt")
            if (result = 1)
                Sleep(0)
            else
                return
        }
    }

    static Soonest(now) {
        soon := now + 15
        for r in this.runners {
            if r.done
                continue
            if (r.waitUntil = 0 || r.waitUntil <= now)
                return now + 1
            if (r.waitUntil < soon)
                soon := r.waitUntil
            for k, at in r.pulses
                if (at > now && at < soon)
                    soon := at
        }
        if this.pending.Length
            return now + 1
        return soon
    }

    static Pace() {
        if (!this.armed && this.runners.Length = 0 && this.pending.Length = 0) {
            Sleep(10)
            return
        }
        if (this.runners.Length = 0 && this.pending.Length = 0) {
            Sleep(4)
            return
        }
        ; While held and toggle have to see the real key-up. A blocking wait
        ; stalls the keyboard hook, so those modes only sleep 1 ms at a time.
        if this.WatchingHold() {
            soon := this.Soonest(this.Now())
            remain := soon - this.Now()
            if (remain > 1.5) {
                this.HrtWait(Min(1, remain - 0.5))
                Sleep(0)
                return
            }
            while (this.Now() < soon)
                Sleep(0)
            return
        }
        soon := this.Soonest(this.Now())
        remain := soon - this.Now()
        if (remain > 2) {
            slice := remain - 2
            if (slice > 5)
                slice := 5
            this.HrtWait(slice)
            Sleep(0)
            return
        }
        while (this.Now() < soon)
            Sleep(0)
    }

    static Tick() {
        now := this.Now()
        this.PollPipe()
        this.PollPanic()
        this.ReapOrphans()
        this.SyncHooks()
        if this.armed
            this.PollTriggers()
        this.RemoveDone()
        this.FlushPending()
        for r in this.runners
            r.Advance(now)
        this.FlushPending()
        this.RemoveDone()
        this.Publish(now)
        this.Broadcast(false)
    }

    static LoadDisk() {
        this.profilePath := A_ScriptDir "\..\profiles\default.json"
        if !FileExist(this.profilePath) {
            DirCreate(A_ScriptDir "\..\profiles")
            FileAppend('{"version":1,"name":"Default","variables":{},"macros":[]}', this.profilePath, "UTF-8")
        }
        text := FileRead(this.profilePath, "UTF-8")
        this.profile := JSON.Parse(text)
        if !(this.profile is Map)
            this.profile := Map()
        this.SeedTriggers()
        this.SyncHooks()
        this.Broadcast(true)
    }

    static SetArmed(on) {
        this.armed := on
        if !on
            this.StopAll()
        this.SyncHooks()
        this.SeedTriggers()
        this.RemoveDone()
        this.Publish(this.Now())
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
        button := SafeKey(Field(trig, "button", ""))
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

    static StartRunner(macro, modeOverride := "", restart := true) {
        if !(macro is Map)
            return
        macro := this.PrepareBasic(macro)
        id := String(Field(macro, "id", ""))
        if (id = "")
            return
        mode := modeOverride != "" ? String(modeOverride) : String(Field(macro, "playMode", "once"))
        exists := this.HasRunner(id)
        if (exists && !restart)
            return
        if (exists && (mode = "whileHeld" || mode = "toggle"))
            return
        if (Field(macro, "busy", false) && exists)
            return
        if exists
            this.StopRunner(id)
        if Field(macro, "exclusive", false)
            this.StopAll()
        this.pending.Push(Runner(macro, mode))
    }

    static StopRunner(id) {
        for r in this.runners
            if (r.id = id)
                r.MarkDone()
        kept := []
        for r in this.pending
            if (r.id != id)
                kept.Push(r)
        this.pending := kept
    }

    static StopAll() {
        for r in this.runners
            r.MarkDone()
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
                slot := Map("count", 0, "resume", this.HasRunner(id))
                this.muted[id] := slot
            }
            count := Integer(slot["count"]) + 1
            slot["count"] := count
            if (count = 1)
                this.StopRunner(id)
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
            if resume {
                macro := this.FindMacro(id)
                if (macro is Map)
                    this.StartRunner(macro)
            }
        }
    }

    static IsMuted(id) {
        if !this.muted.Has(id)
            return false
        slot := this.muted[id]
        return (slot is Map) && Integer(slot["count"]) > 0
    }

    static FlushPending() {
        for r in this.pending
            this.runners.Push(r)
        this.pending := []
    }

    static RemoveDone() {
        kept := []
        for r in this.runners
            if !r.done
                kept.Push(r)
        this.runners := kept
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
                r.MarkDone()
    }

    static FocusOk(macro) {
        exe := String(Field(macro, "focusExe", ""))
        if (exe = "")
            return true
        active := ""
        try active := WinGetProcessName("A")
        catch
            return false
        active := StrLower(active)
        exe := StrLower(exe)
        if (active = exe)
            return true
        if (!InStr(exe, ".") && active = exe ".exe")
            return true
        return false
    }

    static WatchingHold() {
        for r in this.runners {
            if r.done
                continue
        if (r.mode = "whileHeld" || r.mode = "toggle")
            return true
        for frame in r.stack
            if (frame.HasProp("holdLoop") && frame.holdLoop)
                return true
        }
        return false
    }

    ; Real finger, including keys this macro is not hooked to. "P" ignores keys the macro itself sends.
    static FingerDown(name) {
        key := SafeKey(Alias(String(name)))
        if (key = "")
            key := SafeKey(String(name))
        if (key = "")
            return false
        if (this.dry && this.finger.Has(key))
            return this.finger[key]
        if (this.physDown.Has(key))
            return this.physDown[key]
        down := false
        try down := GetKeyState(key, "P")
        return down
    }

    static KeyDown(button) {
        key := SafeKey(button)
        if (key = "")
            return false
        ; Every trigger, recorded or basic, uses the physical finger while that hook is up.
        ; GetKeyState mixes in keys the macro itself is sending.
        if (this.rawOk || this.hooks.Has(key))
            return this.physDown.Has(key) && this.physDown[key]
        down := false
        try down := GetKeyState(key, "P")
        return down
    }

    static SeedTriggers() {
        this.prevDown := Map()
        macros := Field(this.profile, "macros", [])
        if !(macros is Array)
            return
        for macro in macros {
            if !(macro is Map)
                continue
            id := String(Field(macro, "id", ""))
            trig := Field(macro, "trigger", Map())
            button := (trig is Map) ? String(Field(trig, "button", "")) : ""
            this.prevDown[id] := this.KeyDown(button)
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
        macros := Field(this.profile, "macros", [])
        if !(macros is Array)
            return
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
            if !Field(macro, "enabled", true) || this.IsMuted(id) || !this.FocusOk(macro) {
                this.StopRunner(id)
                continue
            }
            mode := String(Field(macro, "playMode", "once"))
            if this.HasTapHold(macro)
                mode := "whileHeld"
            else if (Field(macro, "advanced", false) && mode = "whileHeld")
                mode := "once"
            action := this.TriggerAction(mode, down, was, this.HasRunner(id))
            if (action = "start")
                this.StartRunner(macro)
            else if (action = "stop")
                this.StopRunner(id)
        }
    }

    static HasTapHold(macro) {
        blocks := Field(macro, "blocks", [])
        if !(blocks is Array)
            return false
        for block in blocks
            if (block is Map && String(Field(block, "type", "")) = "tapHold")
                return true
        return false
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
                    key := SafeKey(button)
                    if (key = "" || key = "Pause" || key = "LButton" || key = "RButton")
                        continue
                    want[key] := true
                }
            }
        }
        for key, _ in this.hooks.Clone() {
            if want.Has(key)
                continue
            try Hotkey(HookSpec(key), "Off")
            try Hotkey(HookSpec(key) " up", "Off")
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
        this.SyncMouseHook()
    }

    static KeyDelayMs() {
        if this.dry
            return 400
        delay := 1
        DllCall("SystemParametersInfoW", "UInt", 0x16, "UInt", 0, "UInt*", &delay, "UInt", 0)
        if (delay < 0 || delay > 3)
            delay := 1
        return 250 * (delay + 1)
    }

    static KeyRepeatMs() {
        if this.dry
            return 33
        speed := 31
        DllCall("SystemParametersInfoW", "UInt", 0x0A, "UInt", 0, "UInt*", &speed, "UInt", 0)
        if (speed < 0)
            speed := 0
        if (speed > 31)
            speed := 31
        return Max(16, Round(1000 / (2.5 + speed * (27.5 / 31))))
    }

    static Desired(now) {
        want := Map()
        for r in this.runners {
            if r.done
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
        for r in this.runners {
            dx += r.moveX
            dy += r.moveY
            r.moveX := 0
            r.moveY := 0
        }
        if (dx > 2000)
            dx := 2000
        if (dx < -2000)
            dx := -2000
        if (dy > 2000)
            dy := 2000
        if (dy < -2000)
            dy := -2000
        desired := this.Desired(now)
        downs := []
        ups := []
        extra := Map()
        for r in this.runners {
            if r.done
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
        for k in downs
            this.applied[k] := true
        for k in ups
            if this.applied.Has(k)
                this.applied.Delete(k)
        if this.dry {
            this.sent.Push(Map("down", downs, "up", ups, "x", dx, "y", dy))
            return
        }
        parts := ""
        for k in downs
            parts .= SendPiece(this.OutName(k), "down")
        for k in ups
            parts .= SendPiece(this.OutName(k), "up")
        if (parts != "")
            try Send("{Blind}" parts)
        if (dx != 0 || dy != 0)
            try MouseMove(dx, dy, 0, "R")
    }

    static OutName(key) {
        if !Field(this.profile, "swapClicks", false)
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
        parts := ""
        for k, _ in this.applied
            parts .= SendPiece(this.OutName(k), "up")
        if (parts != "")
            try Send("{Blind}" parts)
        this.applied := Map()
    }

    static StateJson() {
        running := ""
        for index, r in this.runners {
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
        return '{"v":1,"type":"state","armed":' armed ',"running":[' running '],"held":[' held ']}'
    }

    static Broadcast(force := false) {
        json := this.StateJson()
        if (!force && json = this.lastState)
            return
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
        written := 0
        ok := DllCall("WriteFile", "Ptr", this.hPipe, "Ptr", buf, "UInt", size - 1, "UInt*", &written, "Ptr", 0)
        if !ok
            this.DropClient()
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
                this.SeedTriggers()
                this.SyncHooks()
                this.Broadcast(true)
            }
            this.SendRaw('{"v":1,"type":"ack","for":"profile","ok":true}')
            return
        }
        if (type = "command") {
            action := String(Field(msg, "action", ""))
            if (action = "start")
                this.SetArmed(true)
            else if (action = "stop")
                this.SetArmed(false)
            else if (action = "reload")
                this.LoadDisk()
            else if (action = "quit") {
                this.SendRaw('{"v":1,"type":"ack","for":"command","ok":true}')
                ExitApp()
            }
            this.SendRaw('{"v":1,"type":"ack","for":"command","ok":true}')
        }
    }

    static Log(message) {
        try FileAppend(FormatTime(, "HH:mm:ss") " " message "`n", A_ScriptDir "\engine.log", "UTF-8")
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
    fails := Check(SendToken("``", "down") = "{```` down}", "send backtick", fails)
    fails := Check(SendToken("q", "up") = "{q up}", "send letter", fails)
    Engine.profile := Map("swapClicks", 1)
    fails := Check(Engine.OutName("LButton") = "RButton", "swap left sends right", fails)
    fails := Check(Engine.OutName("RButton") = "LButton", "swap right sends left", fails)
    fails := Check(Engine.OutName("MButton") = "MButton", "swap leaves middle", fails)
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
    fails := Check(Engine.vkName[0xA0] = "shift", "vk shift", fails)
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
    fails := Check(Engine.IsMuted("hot") && Engine.IsMuted("idle") && !Engine.IsMuted("spam") && !Engine.HasRunner("hot"), "while held mutes the other macros", fails)
    Engine.NotePhysical("f", true)
    Engine.prevDown["hot"] := false
    Engine.prevDown["spam"] := true
    Engine.PollTriggers()
    Engine.FlushPending()
    fails := Check(!Engine.HasRunner("hot"), "muted macro does not start", fails)
    Engine.NotePhysical("b", false)
    play.Advance(200)
    Engine.FlushPending()
    fails := Check(!Engine.IsMuted("hot") && !Engine.IsMuted("idle") && Engine.HasRunner("hot") && !Engine.HasRunner("idle"), "release restores only the macro that was running", fails)
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
    Engine.hooks.Delete("XButton1")
    Engine.physDown.Delete("XButton1")
    Engine.finger := Map()

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
    return fails
}
