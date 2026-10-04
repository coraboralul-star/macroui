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
    if (v is String && v != "")
        return Number(v)
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
    if !Engine.rawOk
        Engine.Log("physical hook failed")
}

PhysKeyProc(nCode, wParam, lParam) {
    block := false
    if (nCode >= 0 && Engine.rawOk)
        try block := Engine.ReadKey(wParam, lParam)
    if block
        return 1
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
        else if (kind = "tapHold") {
            watch := Field(block, "watch", [])
            if !(watch is Array)
                watch := []
            steps.Push(Map("type", "edgeHold", "key", NormKey(Field(block, "key", "z")), "watch", watch, "armMs", AsNum(Field(block, "armMs", 5), 5), "gapMs", AsNum(Field(block, "gapMs", 80), 80), "ignore", NormalizeIgnore(Field(block, "ignore", "off")), "ignoreMs", AsNum(Field(block, "ignoreMs", 0), 0), "pauseWatch", NormalizePause(Field(block, "pauseWatch", "off"))))
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
            if (kind = "holdLoop" || kind = "swapLoop")
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
        this.hasGoto := false
        this.gotoX := 0
        this.gotoY := 0
        this.didGoto := false
        this.releaseStop := String(Field(macro, "releaseStop", "nextUp"))
        if (this.releaseStop != "finish")
            this.releaseStop := "nextUp"
        this.stopping := false
        this.sawUp := false
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
        if (this.trigger = "" || Engine.KeyDown(this.trigger))
            return
        if (this.StopMode() != "nextUp")
            return
        i := this.stack.Length
        while (i >= 1) {
            frame := this.stack[i]
            i--
            if !(frame is Object)
                continue
            if (frame.HasProp("holdLoop") && frame.holdLoop)
                frame.drain := true
        }
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

    MarkDone() {
        if this.done
            return
        this.done := true
        this.paused := false
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
        this.hasGoto := false
    }

    Advance(now) {
        this.moveX := 0
        this.moveY := 0
        this.hasGoto := false
        this.pressedNow := Map()
        if (this.done || this.paused)
            return
        this.NoteHoldRelease()
        for k, releaseAt in this.pulses.Clone()
            if (releaseAt <= now && this.pulses.Has(k))
                this.pulses.Delete(k)
        if (this.waitUntil != 0 && now < this.waitUntil) {
            if this.HoldShouldStop() {
                this.MarkDone()
                return
            }
            return
        }
        this.waitUntil := 0
        this.scanWait := false
        if (this.stopping && this.StopMode() = "nextUp" && this.sawUp) {
            this.MarkDone()
            return
        }
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
                    this.TickEdge(frame, now)
                    return
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
                    if (this.trigger != "" && Engine.KeyDown(this.trigger)) {
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
        if (kind = "wait" || kind = "scanWait") {
            if (kind = "scanWait") {
                if (this.trigger != "" && !Engine.FingerDown(this.trigger)) {
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
            frame := {steps: [], index: 1, repeatsLeft: 1, edgeHold: true, edgeKey: key, edgePrev: prev, edgePhase: "down", edgeArm: AsNum(Field(step, "armMs", 5), 5), edgeGap: AsNum(Field(step, "gapMs", 80), 80), edgeIgnore: NormalizeIgnore(Field(step, "ignore", "off")), edgeIgnoreMs: AsNum(Field(step, "ignoreMs", 0), 0), edgeLock: Map(), edgeRepeatAt: now + Engine.KeyDelayMs(), mute: mute, muteOn: false, pauseOnce: pauseOnce, pauseOn: false}
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
        if (kind = "holdLoop") {
            if this.stopping
                return ""
            if (this.trigger != "" && Engine.physDown.Has(this.trigger) && !Engine.KeyDown(this.trigger))
                return ""
            this.PushHoldFrame(step)
            return ""
        }
        if (kind = "swapLoop") {
            if this.stopping
                return ""
            if (this.trigger != "" && Engine.physDown.Has(this.trigger) && !Engine.KeyDown(this.trigger))
                return ""
            after := AsNum(Field(step, "afterMs", 150), 150)
            still := this.trigger != "" && Engine.KeyDown(this.trigger)
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
            still := this.trigger != "" && Engine.KeyDown(this.trigger)
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
        return this.trigger != "" && Engine.KeyDown(this.trigger) && this.heldMs >= swapAt
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
            return this.trigger != "" && !Engine.FingerDown(this.trigger)
        }
        if !watching
            return false
        if this.stopping
            return true
        return this.trigger != "" && !Engine.KeyDown(this.trigger)
    }

    TickEdge(frame, now) {
        if this.stopping {
            this.MarkDone()
            return
        }
        if (this.trigger != "" && !Engine.KeyDown(this.trigger)) {
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
    static profile := Map()
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
        this.NotePhysical(name, !up)
        return this.PassBoard() && this.Hooked(name) && this.SwallowTriggers()
    }

    static Hooked(name) {
        if this.hooks.Has(name)
            return true
        folded := StrLower(String(name))
        if this.hookFold.Has(folded)
            return true
        return this.hooks.Has(folded)
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
        if (name = "")
            return
        if this.MatchHidEcho(name, down)
            return
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
        this.PollPanic()
        this.SyncSwallow()
        if this.hooksDirty {
            this.SyncHooks()
            this.hooksDirty := false
        }
        if this.armed
            this.PollTriggers()
        this.RemoveDone()
        this.FlushPending()
        now := this.Now()
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
        this.ReapOrphans()
        this.hooksDirty := true
        this.SyncHooks()
        this.hooksDirty := false
        this.winAt := -100000
        this.Broadcast(true)
    }

    static SetArmed(on) {
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
            if (this.HasRunner(id) || this.HasPending(id))
                continue
            if (resume || this.HoldReplay(id)) {
                macro := this.FindMacro(id)
                if (macro is Map)
                    this.StartRunner(macro)
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
                if (kind = "whileHeld" || kind = "swapAfter")
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
        for r in this.runners
            if !r.done
                kept.Push(r)
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
                r.MarkDone()
    }

    static FocusOk(macro) {
        exe := String(Field(macro, "focusExe", ""))
        if (exe = "")
            exe := String(Field(this.profile, "focusExe", ""))
        if (exe = "")
            return true
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

    static SeedTriggers() {
        this.prevDown := Map()
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
            this.prevDown[id] := this.KeyDown(button)
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
            if this.IsMuted(id)
                continue
            if !Field(macro, "enabled", true) || !this.FocusOk(macro) {
                this.StopRunner(id)
                continue
            }
            if this.OnceDone(id) {
                if this.HasRunner(id) {
                    mode := String(Field(macro, "playMode", "once"))
                    if this.HasTapHold(macro)
                        mode := "whileHeld"
                    if (!down && (mode = "whileHeld"))
                        this.AskStop(id)
                    continue
                }
                this.EnsurePass(id)
                continue
            }
            once := this.OnceOpen(id)
            mode := String(Field(macro, "playMode", "once"))
            if this.HasTapHold(macro)
                mode := "whileHeld"
            ; Playback "On release" means start when the key comes up. That is not
            ; the editor On release (nearest up / play full) stop rule.
            if (Field(macro, "advanced", false) && mode != "onRelease") {
                if (down && !was) {
                    if once
                        this.StartOncePlay(macro)
                    else
                        this.StartRunner(macro)
                    if once
                        this.SpendOnce(id)
                } else if (!down && this.HasRunner(id) && !once) {
                    if this.HoldsOnRelease(macro)
                        this.AskStop(id)
                }
                continue
            }
            action := this.TriggerAction(mode, down, was, this.HasRunner(id))
            if (action = "start") {
                if once
                    this.StartOncePlay(macro)
                else
                    this.StartRunner(macro)
                if once
                    this.SpendOnce(id)
            } else if (action = "stop")
                this.StopRunner(id)
        }
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
            if (block is Map && String(Field(block, "type", "")) = "tapHold")
                return true
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
        if (blocks is Array) {
            for block in blocks {
                if !(block is Map)
                    continue
                kind := String(Field(block, "type", ""))
                if (kind = "whileHeld" || kind = "swapAfter")
                    hasHold := true
                else if (kind = "tapHold")
                    hasTap := true
            }
        }
        ; A While-held block already exits when the trigger comes up. Asking the
        ; whole runner to stop would skip Run-once / After-release blocks after it.
        if hasHold
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
        hasAbs := false
        absX := 0
        absY := 0
        for r in this.runners {
            if (r.done || r.paused)
                continue
            dx += r.moveX
            dy += r.moveY
            if r.hasGoto {
                hasAbs := true
                absX := r.gotoX
                absY := r.gotoY
                r.hasGoto := false
            }
            r.moveX := 0
            r.moveY := 0
        }
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
            this.sent.Push(Map("down", downs, "up", ups, "x", dx, "y", dy))
            return
        }
        if (hasAbs && !this.PassBoard()) {
            this.Emit(downs, ups, 0, 0)
            this.MoveAbs(absX, absY)
        } else if (hasAbs && this.PassBoard())
            this.Emit(downs, ups, 0, 0, true, absX, absY)
        else
            this.Emit(downs, ups, dx, dy)
    }

    static Emit(downs, ups, dx, dy, abs := false, ax := 0, ay := 0) {
        if (this.outputMode != "software") {
            if (!downs.Length && !ups.Length && dx = 0 && dy = 0 && !abs)
                return
            if (abs)
                this.SendRaw(this.HidJson(downs, ups, 0, 0, true, ax, ay))
            else {
                this.HidScale(&dx, &dy)
                this.SendRaw(this.HidJson(downs, ups, dx, dy))
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
        if (parts != "") {
            this.sendNow := Map()
            for k in downs
                this.sendNow[k] := true
            for k in ups
                this.sendNow[k] := true
            try Send("{Blind}" parts)
            ; Leave sendNow set until the next tick so a late $* hotkey is still echo.
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

    static HidJson(downs, ups, dx, dy, abs := false, ax := 0, ay := 0) {
        extra := abs ? ',"abs":true,"ax":' Integer(ax) ',"ay":' Integer(ay) : ""
        return '{"v":1,"type":"hid","down":' this.HidKeys(downs) ',"up":' this.HidKeys(ups) ',"x":' Integer(dx) ',"y":' Integer(dy) extra '}'
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
        return '{"v":1,"type":"state","armed":' armed ',"running":[' running '],"held":[' held '],"front":' JSON.Quote(this.ActiveExe()) ',"windows":' this.WindowListJson() '}'
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
        written := 0
        ok := DllCall("WriteFile", "Ptr", this.hPipe, "Ptr", buf, "UInt", size - 1, "UInt*", &written, "Ptr", 0)
        if !ok {
            err := A_LastError
            if (err = 232)
                return
            this.DropClient()
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
                this.SeedTriggers()
                this.ReapOrphans()
                this.hooksDirty := true
                this.SyncHooks()
                this.hooksDirty := false
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
    fails := Check(Engine.SwallowTriggers(), "no focus restriction swallows", fails)
    fails := Check(HookSpec("a") = "$*a", "software swallows without focus exe", fails)
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
