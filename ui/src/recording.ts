import type { Step } from "./profile";

export type RecEvent = {
  t: number;
  kind: "key" | "mouse";
  button: string;
  down: boolean;
};

export type Recording = {
  timing: "played" | "custom";
  holdMs: number;
  intervalMs: number;
  events: RecEvent[];
};

export type Press = {
  kind: "key" | "mouse";
  button: string;
  holdMs: number;
  downAt: number;
  upAt: number;
  gapAfter: number;
  open: boolean;
};

const CODE: Record<string, string> = {
  Space: "space",
  ShiftLeft: "shift",
  ShiftRight: "shift",
  ControlLeft: "ctrl",
  ControlRight: "ctrl",
  AltLeft: "alt",
  AltRight: "alt",
  Tab: "tab",
  Enter: "enter",
  Backspace: "backspace",
  Escape: "escape",
  ArrowUp: "up",
  ArrowDown: "down",
  ArrowLeft: "left",
  ArrowRight: "right",
};

for (let i = 1; i <= 12; i++) CODE[`F${i}`] = `f${i}`;
for (let i = 0; i <= 9; i++) CODE[`Numpad${i}`] = `Numpad${i}`;
Object.assign(CODE, {
  Minus: "-",
  Equal: "=",
  BracketLeft: "[",
  BracketRight: "]",
  Backslash: "\\",
  Semicolon: ";",
  Quote: "'",
  Comma: ",",
  Period: ".",
  Slash: "/",
  Backquote: "`",
  NumpadDecimal: "NumpadDot",
  NumpadDivide: "NumpadDiv",
  NumpadMultiply: "NumpadMult",
  NumpadSubtract: "NumpadSub",
  NumpadAdd: "NumpadAdd",
  NumpadEnter: "NumpadEnter",
  NumLock: "NumLock",
  Insert: "Insert",
  Delete: "Delete",
  Home: "Home",
  End: "End",
  PageUp: "PgUp",
  PageDown: "PgDn",
  PrintScreen: "PrintScreen",
  ScrollLock: "ScrollLock",
  Pause: "Pause",
  CapsLock: "CapsLock",
  MetaLeft: "LWin",
  MetaRight: "RWin",
});

const MOUSE = ["LButton", "MButton", "RButton", "XButton1", "XButton2"];

export function isEngineKey(name: string) {
  return /^[A-Za-z0-9]+$/.test(name) || /^[-`=[\]\\;',./]$/.test(name);
}

export function keyFromCode(code: string): string | null {
  if (code.startsWith("Key") && code.length === 4) return code.slice(3).toLowerCase();
  if (code.startsWith("Digit") && code.length === 6) return code.slice(5);
  const named = CODE[code];
  return named && isEngineKey(named) ? named : null;
}

export function mouseFromButton(button: number): string | null {
  return MOUSE[button] ?? null;
}

export function pressLabel(button: string): string {
  if (button === "LButton") return "Left";
  if (button === "RButton") return "Right";
  if (button === "MButton") return "Middle";
  if (button === "XButton1") return "M4";
  if (button === "XButton2") return "M5";
  return button;
}

export function normalizeRecording(value: unknown): Recording | null {
  if (!value || typeof value !== "object") return null;
  const raw = value as Partial<Recording>;
  if (!Array.isArray(raw.events)) return null;
  const events: RecEvent[] = [];
  for (const item of raw.events) {
    if (!item || typeof item !== "object") continue;
    const event = item as Partial<RecEvent>;
    const kind = event.kind === "mouse" ? "mouse" : event.kind === "key" ? "key" : null;
    const button = typeof event.button === "string" ? event.button : "";
    if (!kind || !isEngineKey(button)) continue;
    events.push({
      t: Math.max(0, Math.round(Number(event.t) || 0)),
      kind,
      button,
      down: Boolean(event.down),
    });
  }
  return {
    timing: raw.timing === "custom" ? "custom" : "played",
    holdMs: Math.max(1, Math.round(Number(raw.holdMs) || 40)),
    intervalMs: Math.max(0, Math.round(Number(raw.intervalMs) || 0)),
    events,
  };
}

export function pairPresses(events: RecEvent[]): Press[] {
  const open: { kind: RecEvent["kind"]; button: string; index: number }[] = [];
  const presses: Press[] = [];
  for (const event of events) {
    if (event.down) {
      presses.push({
        kind: event.kind,
        button: event.button,
        holdMs: 1,
        downAt: event.t,
        upAt: event.t,
        gapAfter: 0,
        open: true,
      });
      open.push({ kind: event.kind, button: event.button, index: presses.length - 1 });
      continue;
    }
    const index = open.findIndex((item) => item.kind === event.kind && item.button === event.button);
    if (index < 0) continue;
    const press = presses[open[index].index];
    press.upAt = event.t;
    press.holdMs = Math.max(1, event.t - press.downAt);
    press.open = false;
    open.splice(index, 1);
  }
  for (let i = 0; i < presses.length - 1; i++) presses[i].gapAfter = presses[i + 1].downAt - presses[i].upAt;
  return presses;
}

export function closeHeld(events: RecEvent[], end: number): RecEvent[] {
  const held = new Map<string, RecEvent>();
  for (const event of events) {
    const id = event.kind + event.button;
    if (event.down) held.set(id, event);
    else held.delete(id);
  }
  if (held.size === 0) return events;
  const at = Math.max(end, (events.at(-1)?.t ?? 0) + 1);
  return [
    ...events,
    ...[...held.values()].map((event) => ({ t: at, kind: event.kind, button: event.button, down: false })),
  ];
}

export function compileRecording(recording: Recording): Step[] {
  if (recording.events.length === 0) return [];
  if (recording.timing === "custom") return compileCustom(recording);
  return compilePlayed(recording.events);
}

function compilePlayed(events: RecEvent[]): Step[] {
  const steps: Step[] = [];
  let last = 0;
  let started = false;
  for (const event of events) {
    if (started && event.t > last) steps.push({ type: "wait", ms: event.t - last });
    steps.push(event.down ? downStep(event) : upStep(event));
    last = event.t;
    started = true;
  }
  return steps;
}

function compileCustom(recording: Recording): Step[] {
  const presses = pairPresses(recording.events);
  const hold = Math.max(1, recording.holdMs);
  const interval = Math.max(0, recording.intervalMs);
  const steps: Step[] = [];
  presses.forEach((press, index) => {
    steps.push(downStep(press));
    steps.push({ type: "wait", ms: hold });
    steps.push(upStep(press));
    if (index < presses.length - 1 && interval > 0) steps.push({ type: "wait", ms: interval });
  });
  return steps;
}

function downStep(event: { kind: "key" | "mouse"; button: string }): Step {
  return event.kind === "key"
    ? { type: "key", action: "down", key: event.button }
    : { type: "mouse", action: "down", button: event.button };
}

function upStep(event: { kind: "key" | "mouse"; button: string }): Step {
  return event.kind === "key"
    ? { type: "key", action: "up", key: event.button }
    : { type: "mouse", action: "up", button: event.button };
}

export function glance(steps: Step[], limit = 16): string[] {
  const out: string[] = [];
  const walk = (list: Step[]) => {
    for (const step of list) {
      if (out.length >= limit) return;
      if (step.type === "key") {
        out.push(step.action === "down" ? `hold ${step.key}` : step.action === "up" ? `up ${step.key}` : step.key);
      } else if (step.type === "mouse") {
        const name = pressLabel(step.button);
        out.push(step.action === "down" ? `hold ${name}` : step.action === "up" ? `up ${name}` : name);
      } else if (step.type === "wait") out.push(`${step.ms}ms`);
      else if (step.type === "move") out.push("move");
      else if (step.type === "goto") out.push("go to");
      else if (step.type === "run") out.push("run");
      else if (step.type === "repeat") {
        out.push(step.count === 0 ? "loop" : `×${step.count}`);
        walk(step.steps);
      }
    }
  };
  walk(steps);
  if (out.length >= limit) out.push("…");
  return out;
}
