; Plays bench cases on a virtual clock and writes the same lines as engine-trace.
; Returns before the mutex. Does not call SendInput: Engine.dry stays on.

TracePlay(bench, outDir) {
    orderPath := bench "\order.txt"
    if !FileExist(orderPath) {
        FileAppend("missing order.txt`n", "**")
        return 1
    }
    if (outDir != "")
        DirCreate(outDir)
    failed := 0
    for line in StrSplit(FileRead(orderPath, "UTF-8"), "`n", "`r") {
        name := Trim(line)
        if (name = "" || SubStr(name, 1, 1) = "#")
            continue
        try {
            text := TraceCase(bench, name)
            if (outDir = "")
                FileAppend("=== " name " ===`n" text, "*")
            else {
                f := FileOpen(outDir "\" name ".out", "w", "UTF-8-RAW")
                if !f {
                    FileAppend(name " cannot write`n", "**")
                    failed++
                    continue
                }
                f.Write(text)
                f.Close()
            }
        } catch as err {
            FileAppend(name " " err.Message " line " err.Line "`n", "**")
            failed++
        }
    }
    if failed
        return 1
    FileAppend("ok`n", "*")
    return 0
}

TraceCase(bench, name) {
    script := TraceRead(bench "\cases\" name ".case")
    profilePath := bench "\" StrReplace(script.profile, "/", "\")
    TraceReset()
    Engine.profile := JSON.Parse(FileRead(profilePath, "UTF-8"))
    out := ""
    t := 0
    while (t <= script.until) {
        for ev in script.events {
            if (ev.time != t)
                continue
            if (ev.kind = "finger") {
                key := NormKey(ev.a)
                if (key != "") {
                    Engine.finger[key] := ev.down
                    Engine.NotePhysical(key, ev.down)
                }
            } else if (ev.kind = "start") {
                macro := Engine.FindMacro(ev.a)
                if (macro is Map)
                    Engine.StartRunner(macro)
            } else if (ev.kind = "stop")
                Engine.StopRunner(ev.a)
            else if (ev.kind = "ask")
                Engine.AskStop(ev.a)
        }
        out .= TraceTick(t)
        t++
    }
    return out
}

TraceReset() {
    Engine.runners := []
    Engine.pending := []
    Engine.applied := Map()
    Engine.sent := []
    Engine.muted := Map()
    Engine.onceLock := Map()
    Engine.oncePass := Map()
    Engine.passKeys := Map()
    Engine.prevDown := Map()
    Engine.finger := Map()
    Engine.physDown := Map()
    Engine.hooks := Map()
    Engine.lastSent := Map()
    Engine.hidEcho := []
    Engine.sendNow := Map()
    Engine.mouseAtOk := false
    Engine.mouseAtX := 0
    Engine.mouseAtY := 0
    Engine.dry := true
    Engine.rawOk := true
    Engine.armed := true
    Engine.outputMode := "software"
    Engine.frontKnown := true
    Engine.frontExe := "notepad.exe"
    Engine.profile := Map()
}

TraceTick(now) {
    abs := false
    ax := 0
    ay := 0
    Engine.RemoveDone()
    Engine.FlushPending()
    for r in Engine.runners
        r.Advance(now)
    Engine.FlushPending()
    for r in Engine.runners {
        if r.paused
            continue
        if (r.HasProp("hasGoto") && r.hasGoto) {
            abs := true
            ax := r.gotoX
            ay := r.gotoY
        }
    }
    Engine.Publish(now)
    Engine.RemoveDone()
    if !Engine.sent.Length
        return ""
    item := Engine.sent[-1]
    return TraceLines(now, item, abs, ax, ay)
}

TraceSortKeys(list) {
    if (list.Length < 2)
        return list
    text := ""
    for k in list
        text .= k "`n"
    text := Sort(SubStr(text, 1, -1), "C")
    return StrSplit(text, "`n")
}

TraceLines(now, item, abs, ax, ay) {
    out := ""
    for k in TraceSortKeys(item["down"])
        out .= now " down " k "`n"
    for k in TraceSortKeys(item["up"])
        out .= now " up " k "`n"
    if abs
        out .= now " goto " Integer(ax) " " Integer(ay) "`n"
    else if (item["x"] != 0 || item["y"] != 0)
        out .= now " move " Integer(item["x"]) " " Integer(item["y"]) "`n"
    if (item["wheel"] != 0)
        out .= now " wheel " Integer(item["wheel"]) "`n"
    return out
}

TraceRead(path) {
    script := {profile: "", until: -1, events: []}
    for raw in StrSplit(FileRead(path, "UTF-8"), "`n", "`r") {
        line := Trim(raw)
        if (line = "" || SubStr(line, 1, 1) = "#")
            continue
        parts := StrSplit(line, " ")
        if (parts[1] = "profile" && parts.Length >= 2) {
            script.profile := parts[2]
            continue
        }
        if (parts[1] = "until" && parts.Length >= 2) {
            script.until := Integer(parts[2])
            continue
        }
        if (parts[1] = "roll")
            continue
        if (parts.Length >= 2 && parts[2] = "pointer")
            continue
        if (parts.Length < 3)
            throw Error("bad event " line)
        ev := {time: Integer(parts[1]), kind: parts[2], a: parts[3], down: false}
        if (parts[2] = "finger") {
            if (parts.Length < 4)
                throw Error("bad finger " line)
            ev.down := parts[4] = "down"
        }
        script.events.Push(ev)
    }
    if (script.profile = "" || script.until < 0)
        throw Error("case needs profile and until")
    return script
}
