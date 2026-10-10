; Minimal JSON for the engine. Objects become Maps, arrays become Arrays.
; true/false are AHK integers 1 and 0. null becomes an empty string.

class JSON {
    static Parse(text) {
        p := {s: text, i: 1, n: StrLen(text)}
        if (p.n >= 1 && Ord(SubStr(text, 1, 1)) = 0xFEFF)
            p.i := 2
        this.Skip(p)
        return this.Value(p)
    }

    static Skip(p) {
        while (p.i <= p.n) {
            c := SubStr(p.s, p.i, 1)
            if (c != " " && c != "`t" && c != "`r" && c != "`n")
                break
            p.i++
        }
    }

    static Value(p) {
        this.Skip(p)
        if (p.i > p.n)
            throw Error("Unexpected end of JSON")
        c := SubStr(p.s, p.i, 1)
        if (c = "{")
            return this.Obj(p)
        if (c = "[")
            return this.Arr(p)
        if (c = '"')
            return this.Str(p)
        if (c = "t") {
            p.i += 4
            return true
        }
        if (c = "f") {
            p.i += 5
            return false
        }
        if (c = "n") {
            p.i += 4
            return ""
        }
        return this.Num(p)
    }

    static Obj(p) {
        p.i++
        m := Map()
        this.Skip(p)
        if (SubStr(p.s, p.i, 1) = "}") {
            p.i++
            return m
        }
        loop {
            this.Skip(p)
            key := this.Str(p)
            this.Skip(p)
            if (SubStr(p.s, p.i, 1) != ":")
                throw Error("Expected ':' in object")
            p.i++
            m[key] := this.Value(p)
            this.Skip(p)
            c := SubStr(p.s, p.i, 1)
            if (c = ",") {
                p.i++
                continue
            }
            if (c = "}") {
                p.i++
                break
            }
            throw Error("Expected ',' or '}' in object")
        }
        return m
    }

    static Arr(p) {
        p.i++
        a := []
        this.Skip(p)
        if (SubStr(p.s, p.i, 1) = "]") {
            p.i++
            return a
        }
        loop {
            a.Push(this.Value(p))
            this.Skip(p)
            c := SubStr(p.s, p.i, 1)
            if (c = ",") {
                p.i++
                continue
            }
            if (c = "]") {
                p.i++
                break
            }
            throw Error("Expected ',' or ']' in array")
        }
        return a
    }

    static Str(p) {
        if (SubStr(p.s, p.i, 1) != '"')
            throw Error("Expected string")
        p.i++
        out := ""
        while (p.i <= p.n) {
            c := SubStr(p.s, p.i, 1)
            if (c = '"') {
                p.i++
                return out
            }
            if (c = "\") {
                p.i++
                e := SubStr(p.s, p.i, 1)
                p.i++
                if (e = "n")
                    out .= "`n"
                else if (e = "r")
                    out .= "`r"
                else if (e = "t")
                    out .= "`t"
                else if (e = "u") {
                    hex := SubStr(p.s, p.i, 4)
                    p.i += 4
                    out .= Chr(Integer("0x" hex))
                } else
                    out .= e
                continue
            }
            out .= c
            p.i++
        }
        throw Error("Unterminated string")
    }

    static Num(p) {
        start := p.i
        if (SubStr(p.s, p.i, 1) = "-")
            p.i++
        while (p.i <= p.n) {
            c := SubStr(p.s, p.i, 1)
            if !(c ~= "[0-9.eE+\-]")
                break
            p.i++
        }
        token := SubStr(p.s, start, p.i - start)
        if (token = "" || token = "-")
            throw Error("Expected number")
        if InStr(token, ".") || InStr(token, "e") || InStr(token, "E")
            return Float(token)
        return Integer(token)
    }

    static Quote(s) {
        s := String(s)
        s := StrReplace(s, "\", "\\")
        s := StrReplace(s, '"', '\"')
        s := StrReplace(s, "`r", "\r")
        s := StrReplace(s, "`n", "\n")
        s := StrReplace(s, "`t", "\t")
        ; Window titles reach the state message. Any other control character
        ; makes the JSON invalid, and the shell drops the pipe on a bad message.
        if RegExMatch(s, "[\x00-\x1F]") {
            out := ""
            loop parse s {
                code := Ord(A_LoopField)
                out .= code < 0x20 ? Format("\u{:04X}", code) : A_LoopField
            }
            s := out
        }
        return '"' s '"'
    }
}
