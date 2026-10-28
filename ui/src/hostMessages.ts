import type { HostMessage } from "./bridge";
import { normalizeInputMode, type EngineState, type InputMode } from "./profile";

export type LiveOutput = { mode: InputMode; requested: InputMode };

export type BoardPort = { port: string; chip: string; name: string };
export type WindowRow = { exe: string; title: string };

export type EngineKind = "ahk" | "native";

export function parseEngineKind(value: unknown): EngineKind | "" {
  return value === "native" ? "native" : value === "ahk" ? "ahk" : "";
}

export function parseLink(message: HostMessage): { shell: boolean; pipe: boolean; engine: EngineKind | ""; choice: EngineKind } | null {
  if (message.type !== "link") return null;
  const pipe = Boolean(message.pipe);
  const engine = pipe ? parseEngineKind(message.engine) : "";
  const choice = parseEngineKind(message.choice) || "native";
  return { shell: Boolean(message.shell), pipe, engine, choice };
}

export function parseEngineState(message: HostMessage): EngineState | null {
  if (message.type !== "state") return null;
  return {
    armed: Boolean(message.armed),
    running: Array.isArray(message.running) ? (message.running as EngineState["running"]) : [],
    held: Array.isArray(message.held) ? message.held.filter((key) => typeof key === "string") : [],
    activeId: typeof message.activeId === "string" ? message.activeId : "",
    front: typeof message.front === "string" ? message.front : "",
  };
}

export function parseWindows(message: HostMessage): WindowRow[] | null {
  if (message.type !== "state" || !Array.isArray(message.windows)) return null;
  return message.windows.flatMap((item) => {
    if (!item || typeof item !== "object") return [];
    const row = item as { exe?: unknown; title?: unknown };
    if (typeof row.exe !== "string" || row.exe === "") return [];
    return [{ exe: row.exe, title: typeof row.title === "string" ? row.title : "" }];
  });
}

export type TurnState = {
  state: "off" | "armed" | "turning" | "short" | "done" | "testing" | "sent" | "error";
  counts: number;
  detail: string;
};

const TURN_STATES: TurnState["state"][] = ["off", "armed", "turning", "short", "done", "testing", "sent", "error"];

/** The shell's calibration progress. */
export function parseTurn(message: HostMessage): TurnState | null {
  if (message.type !== "turn") return null;
  const state = TURN_STATES.find((item) => item === message.state);
  if (!state) return null;
  const counts = Number(message.counts);
  return {
    state,
    counts: Number.isFinite(counts) && counts > 0 ? Math.round(counts) : 0,
    detail: typeof message.detail === "string" ? message.detail : "",
  };
}

export function parseFault(message: HostMessage): string | null {
  if (message.type !== "error") return null;
  const detail = typeof message.detail === "string" ? message.detail : "";
  const code = typeof message.code === "string" ? message.code : "error";
  return detail || code;
}

export function parseMaximized(message: HostMessage): boolean | null {
  if (message.type !== "chrome") return null;
  return Boolean(message.maximized);
}

export function parsePorts(message: HostMessage): BoardPort[] | null {
  if (message.type !== "ports" || !Array.isArray(message.devices)) return null;
  return message.devices.flatMap((item) => {
    if (!item || typeof item !== "object") return [];
    const row = item as BoardPort;
    if (typeof row.port !== "string" || typeof row.chip !== "string") return [];
    return [{ port: row.port, chip: row.chip, name: typeof row.name === "string" ? row.name : row.chip }];
  });
}

export function parseOutput(message: HostMessage): LiveOutput | null {
  if (message.type !== "output") return null;
  const mode = normalizeInputMode(message.mode);
  const requested = "requested" in message ? normalizeInputMode(message.requested) : mode;
  return { mode, requested };
}

/** Header label. A software echo from before this choice was applied keeps the choice on screen. */
export function outputChipMode(output: LiveOutput | null, selected: InputMode): InputMode {
  if (!output) return selected;
  if (output.mode !== "software") return output.mode;
  if (output.requested !== "software") return "software";
  return selected;
}

export function outputFallenBack(output: LiveOutput | null): boolean {
  return output != null && output.mode === "software" && output.requested !== "software";
}

/** Default is always available. A board counts only after the shell says it opened that board. */
export function outputConnected(output: LiveOutput | null, selected: InputMode): boolean {
  if (selected === "software") return true;
  return output?.mode === selected;
}

export function statusLabel(pipe: boolean, shell: boolean, armed: boolean): string {
  return pipe ? (armed ? "Running" : "Idle") : shell ? "Offline" : "Local";
}
