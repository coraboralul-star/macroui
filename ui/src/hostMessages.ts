import type { HostMessage } from "./bridge";
import type { EngineState } from "./profile";

export type BoardPort = { port: string; chip: string; name: string };
export type WindowRow = { exe: string; title: string };

export function parseLink(message: HostMessage): { shell: boolean; pipe: boolean } | null {
  if (message.type !== "link") return null;
  return { shell: Boolean(message.shell), pipe: Boolean(message.pipe) };
}

export function parseEngineState(message: HostMessage): EngineState | null {
  if (message.type !== "state") return null;
  return {
    armed: Boolean(message.armed),
    running: Array.isArray(message.running) ? (message.running as EngineState["running"]) : [],
    held: Array.isArray(message.held) ? message.held.filter((key) => typeof key === "string") : [],
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

export function statusLabel(pipe: boolean, shell: boolean, armed: boolean): string {
  return pipe ? (armed ? "Running" : "Idle") : shell ? "Offline" : "Local";
}
