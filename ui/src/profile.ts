import { canonKey } from "./keyboard";
import { normalizeRecording, type Recording } from "./recording";

export type PlayMode = "once" | "repeat" | "whileHeld" | "toggle" | "onRelease";
/** After the trigger comes up, keep going until the next key/mouse up, or until this pass ends. */
export type ReleaseStop = "nextUp" | "finish";
export type TriggerKind = "key" | "mouse" | "side";
export type KeyAction = "down" | "up" | "tap";
export type InputMode = "software" | "rp2040" | "rp2350";

export const INPUT_MODES: { value: InputMode; label: string }[] = [
  { value: "software", label: "Default" },
  { value: "rp2040", label: "RP2040" },
  { value: "rp2350", label: "RP2350" },
];

export function normalizeInputMode(value: unknown): InputMode {
  return value === "rp2040" || value === "rp2350" ? value : "software";
}

export type GotoWhere = "screen" | "window" | "client";

export const GOTO_WHERE: { value: GotoWhere; label: string }[] = [
  { value: "screen", label: "Screen" },
  { value: "window", label: "Window" },
  { value: "client", label: "Client" },
];

export function normalizeGotoWhere(value: unknown): GotoWhere {
  return value === "window" || value === "client" ? value : "screen";
}

export type IgnoreLock = "off" | "macro" | "both";

export const IGNORE_LOCKS: { value: IgnoreLock; label: string }[] = [
  { value: "off", label: "Off" },
  { value: "macro", label: "Macro" },
  { value: "both", label: "Both" },
];

export function normalizeIgnoreLock(value: unknown): IgnoreLock {
  return value === "macro" || value === "both" ? value : "off";
}

export type PauseWatch = "off" | "block";

export const PAUSE_WATCH: { value: PauseWatch; label: string }[] = [
  { value: "off", label: "Off" },
  { value: "block", label: "Block" },
];

export function normalizePauseWatch(value: unknown): PauseWatch {
  if (value === "block") return "block";
  return "off";
}

export type Step =
  | { type: "key"; action: KeyAction; key: string; holdMs?: number }
  | { type: "mouse"; action: KeyAction; button: string; holdMs?: number }
  | { type: "move"; x: number; y: number }
  | { type: "goto"; x: number; y: number; ms?: number; where?: GotoWhere }
  | { type: "wait"; ms: number }
  | { type: "scanWait"; ms: number }
  | { type: "repeat"; count: number; steps: Step[] }
  | { type: "run"; macroId: string };

export type Trigger = { kind: TriggerKind; button: string };

export type Block =
  | { id: string; type: "whileHeld"; steps: Step[]; mute: string[]; releaseStop?: ReleaseStop }
  | { id: string; type: "ifShort"; underMs: number; minCycles: number; steps: Step[] }
  | { id: string; type: "then"; forMs: number; steps: Step[] }
  | { id: string; type: "repeat"; count: number; steps: Step[]; releaseStop?: ReleaseStop }
  | { id: string; type: "wait"; ms: number }
  | { id: string; type: "steps"; steps: Step[]; releaseStop?: ReleaseStop }
  | { id: string; type: "tapHold"; key: string; watch: string[]; armMs: number; gapMs: number; ignore: IgnoreLock; ignoreMs: number; pauseWatch: PauseWatch };

export type Macro = {
  id: string;
  name: string;
  enabled: boolean;
  priority: number;
  speed: number;
  playMode: PlayMode;
  repeatCount: number;
  exclusive: boolean;
  focusExe: string;
  trigger: Trigger;
  steps: Step[];
  recording: Recording | null;
  /** Playback-menu binding. The key is held for 18 ms, then gapMs is the pause before the next press. */
  basic?: boolean;
  /** Pause after the 18 ms hold, before the next press. */
  gapMs?: number;
  advanced?: boolean;
  busy?: boolean;
  blocks?: Block[];
  /** Advanced only. Hold still repeats. Release uses this instead of cutting immediately. */
  releaseStop?: ReleaseStop;
};

export type Config = {
  id: string;
  name: string;
  macros: Macro[];
  focusExe?: string;
};

export type Profile = {
  version: 1;
  name: string;
  variables: Record<string, string | number>;
  activeId: string;
  configs: Config[];
  /** Bindings for the active profile. This is what the engine runs. */
  macros: Macro[];
  /** Recorded left and right clicks trade places. Those buttons are never triggers. */
  swapClicks?: boolean;
  /** Where Publish() should send keys and mouse: this PC, or a Vendetta USB board. */
  inputMode?: InputMode;
  /** If set, this profile only runs while that process is the foreground window. */
  focusExe?: string;
};

export const MIN_REPEAT_MS = 18;

export type EngineState = {
  armed: boolean;
  running: { id: string; name: string }[];
  held: string[];
  front?: string;
  windows?: { exe: string; title: string }[];
};

export const KEY_OPTIONS = [
  "w", "a", "s", "d", "q", "e", "r", "f", "c", "v", "x", "z",
  "space", "LShift", "RShift", "LCtrl", "RCtrl", "LAlt", "RAlt", "tab", "enter", "escape", "backspace",
  "1", "2", "3", "4", "5", "6", "7", "8", "9", "0",
  "f1", "f2", "f3", "f4", "f5", "f6", "f7", "f8", "f9", "f10", "f11", "f12",
  "up", "down", "left", "right",
];

export const MOUSE_OPTIONS = [
  { value: "LButton", label: "Left click" },
  { value: "RButton", label: "Right click" },
  { value: "MButton", label: "Middle click" },
];

export const SIDE_OPTIONS = [
  { value: "XButton1", label: "Mouse 4" },
  { value: "XButton2", label: "Mouse 5" },
];

export const PLAY_MODES: { value: PlayMode; label: string }[] = [
  { value: "once", label: "Once" },
  { value: "repeat", label: "Repeat" },
  { value: "whileHeld", label: "While held" },
  { value: "toggle", label: "Toggle" },
  { value: "onRelease", label: "On release" },
];

export const RELEASE_STOPS: { value: ReleaseStop; label: string }[] = [
  { value: "nextUp", label: "Nearest up" },
  { value: "finish", label: "Play full" },
];

export function normalizeReleaseStop(value: unknown): ReleaseStop {
  return value === "finish" ? "finish" : "nextUp";
}

function optionalReleaseStop(value: unknown): ReleaseStop | undefined {
  return value === "finish" || value === "nextUp" ? value : undefined;
}

const starter: Macro[] = [
    {
      id: "sprint-jump",
      name: "Sprint jump",
      enabled: true,
      priority: 0,
      speed: 1,
      playMode: "whileHeld",
      repeatCount: 1,
      exclusive: false,
      focusExe: "",
      trigger: { kind: "side", button: "XButton1" },
      recording: null,
      steps: [
        { type: "key", action: "down", key: "w" },
        {
          type: "repeat",
          count: 0,
          steps: [
            { type: "key", action: "tap", key: "space", holdMs: 20 },
            { type: "wait", ms: 50 },
          ],
        },
      ],
    },
    {
      id: "click-burst",
      name: "Click burst",
      enabled: true,
      priority: 0,
      speed: 1,
      playMode: "once",
      repeatCount: 1,
      exclusive: false,
      focusExe: "",
      trigger: { kind: "key", button: "f" },
      recording: null,
      steps: [
        { type: "mouse", action: "tap", button: "LButton", holdMs: 15 },
        { type: "wait", ms: 40 },
        { type: "mouse", action: "tap", button: "LButton", holdMs: 15 },
        { type: "wait", ms: 40 },
        { type: "mouse", action: "tap", button: "LButton", holdMs: 15 },
      ],
    },
];

export const defaultProfile: Profile = {
  version: 1,
  name: "Default",
  variables: {},
  activeId: "default",
  configs: [{ id: "default", name: "Default", macros: starter }],
  macros: starter,
  swapClicks: false,
  inputMode: "software",
};

export function newId(): string {
  return crypto.randomUUID();
}

export function blankMacro(): Macro {
  return {
    id: newId(),
    name: "New macro",
    enabled: true,
    priority: 0,
    speed: 1,
    playMode: "whileHeld",
    repeatCount: 1,
    exclusive: false,
    focusExe: "",
    trigger: { kind: "key", button: "" },
    steps: [],
    recording: null,
    basic: false,
    gapMs: MIN_REPEAT_MS,
    releaseStop: "nextUp",
  };
}

export function basicTiming(repeatMs: number): { hold: number; gap: number } {
  return { hold: MIN_REPEAT_MS, gap: Math.max(0, Math.round(repeatMs) || 0) };
}

export function basicSteps(key: string, everyMs: number): Step[] {
  const { hold, gap } = basicTiming(everyMs);
  return [
    { type: "key", action: "down", key },
    { type: "wait", ms: hold },
    { type: "key", action: "up", key },
    { type: "wait", ms: gap },
  ];
}

export function mouseSteps(button: string, everyMs: number): Step[] {
  const { hold, gap } = basicTiming(everyMs);
  return [
    { type: "mouse", action: "down", button },
    { type: "wait", ms: hold },
    { type: "mouse", action: "up", button },
    { type: "wait", ms: gap },
  ];
}

export function applyGap(macro: Macro, gapMs: number): Macro {
  const ms = Math.max(0, Math.round(gapMs) || 0);
  const button = macro.trigger.button;
  if (!macro.basic) return { ...macro, gapMs: ms };
  if (macro.trigger.kind !== "key" && button) return { ...macro, gapMs: ms, steps: mouseSteps(button, ms) };
  return { ...macro, gapMs: ms, steps: basicSteps(button || "space", ms) };
}

export function writeActive(profile: Profile, macros: Macro[]): Profile {
  const configs = profile.configs.map((config) =>
    config.id === profile.activeId ? { ...config, macros, focusExe: profile.focusExe ?? "" } : config,
  );
  return { ...profile, configs, macros };
}

export function setFocusExe(profile: Profile, focusExe: string): Profile {
  const configs = profile.configs.map((config) =>
    config.id === profile.activeId ? { ...config, focusExe } : config,
  );
  return { ...profile, configs, focusExe };
}

export function switchConfig(profile: Profile, id: string): Profile {
  const config = profile.configs.find((item) => item.id === id);
  if (!config) return profile;
  return { ...profile, activeId: id, name: config.name, macros: config.macros, focusExe: config.focusExe ?? "" };
}

export function renameActive(profile: Profile, name: string): Profile {
  const configs = profile.configs.map((config) =>
    config.id === profile.activeId ? { ...config, name } : config,
  );
  return { ...profile, name, configs };
}

export function blankStep(type: Step["type"]): Step {
  switch (type) {
    case "key":
      return { type: "key", action: "tap", key: "space", holdMs: 20 };
    case "mouse":
      return { type: "mouse", action: "tap", button: "LButton", holdMs: 15 };
    case "move":
      return { type: "move", x: 0, y: 0 };
    case "goto":
      return { type: "goto", x: 0, y: 0, ms: 15, where: "screen" };
    case "wait":
      return { type: "wait", ms: 50 };
    case "scanWait":
      return { type: "scanWait", ms: 80 };
    case "repeat":
      return { type: "repeat", count: 2, steps: [] };
    case "run":
      return { type: "run", macroId: "" };
  }
}

export function defaultButton(kind: TriggerKind): string {
  if (kind === "mouse") return "LButton";
  if (kind === "side") return "XButton1";
  return "f";
}

export function isProfile(value: unknown): value is Profile {
  if (!value || typeof value !== "object") return false;
  const v = value as Profile;
  return v.version === 1 && typeof v.name === "string" && Array.isArray(v.macros);
}

export function normalizeProfile(value: unknown): Profile {
  if (!isProfile(value)) return structuredClone(defaultProfile);
  const macros = onePerTrigger(value.macros.map(normalizeMacro));
  const saved = Array.isArray(value.configs) ? value.configs : [];
  const configs = saved.length
    ? saved.map((item, index) => ({
        id: item?.id || newId(),
        name: item?.name || `Profile ${index + 1}`,
        macros: Array.isArray(item?.macros) ? onePerTrigger(item.macros.map(normalizeMacro)) : [],
        focusExe: typeof item?.focusExe === "string" ? item.focusExe : "",
      }))
    : [{ id: "default", name: value.name || "Default", macros, focusExe: typeof (value as Profile).focusExe === "string" ? (value as Profile).focusExe : "" }];
  const activeId = configs.some((config) => config.id === value.activeId) ? value.activeId : configs[0].id;
  const active = configs.find((config) => config.id === activeId) ?? configs[0];
  const live = saved.length ? active.macros : macros;
  return {
    version: 1,
    name: active.name || value.name || "Default",
    variables: value.variables && typeof value.variables === "object" ? value.variables : {},
    activeId,
    configs: configs.map((config) => (config.id === activeId ? { ...config, macros: live } : config)),
    macros: live,
    swapClicks: Boolean((value as Profile).swapClicks),
    inputMode: normalizeInputMode((value as Profile).inputMode),
    focusExe: active.focusExe || String((value as Profile).focusExe ?? ""),
  };
}

function onePerTrigger(macros: Macro[]): Macro[] {
  const seen = new Set<string>();
  return macros.map((macro) => {
    const button = macro.trigger.button;
    if (!button) return macro;
    const token = `${macro.trigger.kind}:${button}`;
    if (seen.has(token)) return { ...macro, trigger: { ...macro.trigger, button: "" } };
    seen.add(token);
    return macro;
  });
}

function normalizeMacro(value: Macro): Macro {
  const trigger = value.trigger ?? { kind: "key" as const, button: "f" };
  const kind = trigger.kind ?? "key";
  const rawButton = typeof trigger.button === "string" ? trigger.button : defaultButton(kind);
  const sided = kind === "key" ? canonKey(rawButton) : rawButton;
  const button = sided === "LButton" || sided === "RButton" ? "" : sided;
  const gap = Number(value.gapMs);
  const gapMs = Number.isFinite(gap) && gap >= 0 ? Math.round(gap) : MIN_REPEAT_MS;
  const basic = Boolean(value.basic);
  const steps = basic && button
    ? kind === "key"
      ? basicSteps(button, gapMs)
      : mouseSteps(button, gapMs)
    : Array.isArray(value.steps) ? normalizeSteps(value.steps) : [];
  return {
    id: value.id || newId(),
    name: value.name || "Macro",
    enabled: value.enabled !== false,
    priority: Number(value.priority) || 0,
    speed: Number(value.speed) > 0 ? Number(value.speed) : 1,
    playMode: value.playMode || "once",
    repeatCount: Math.max(1, Number(value.repeatCount) || 1),
    exclusive: Boolean(value.exclusive),
    focusExe: value.focusExe ?? "",
    trigger: { kind, button },
    steps,
    recording: normalizeRecording(value.recording),
    basic,
    gapMs,
    advanced: Boolean(value.advanced),
    busy: Boolean(value.busy),
    blocks: normalizeBlocks(value.blocks),
    releaseStop: normalizeReleaseStop(value.releaseStop),
  };
}

function muteIds(value: unknown): string[] {
  if (!Array.isArray(value)) return [];
  return value.filter((item): item is string => typeof item === "string" && item.length > 0);
}

function normalizeSteps(steps: Step[]): Step[] {
  return steps.flatMap((step): Step[] => {
    if (step.type === "key") return [{ ...step, key: canonKey(step.key) }];
    if (step.type === "scanWait") {
      const ms = Number(step.ms);
      return [{ type: "scanWait", ms: Number.isFinite(ms) && ms >= 0 ? Math.round(ms) : 0 }];
    }
    if (step.type === "repeat" && Array.isArray(step.steps)) return [{ ...step, steps: normalizeSteps(step.steps) }];
    return [step];
  });
}

function normalizeBlocks(value: unknown): Block[] {
  if (!Array.isArray(value)) return [];
  return value.flatMap((item): Block[] => {
    if (!item || typeof item !== "object") return [];
    const raw = item as {
      id?: string;
      type?: string;
      steps?: Step[];
      underMs?: number;
      minCycles?: number;
      forMs?: number;
      count?: number;
      ms?: number;
      key?: string;
      button?: string;
      holdMs?: number;
      watch?: string[];
      armMs?: number;
      gapMs?: number;
      ignore?: unknown;
      ignoreMs?: number;
      pauseWatch?: unknown;
      mute?: unknown;
      releaseStop?: unknown;
    };
    const id = raw.id || newId();
    const steps = Array.isArray(raw.steps) ? normalizeSteps(raw.steps) : [];
    const releaseStop = optionalReleaseStop(raw.releaseStop);
    if (raw.type === "steps") return [{ id, type: "steps", steps, ...(releaseStop ? { releaseStop } : {}) }];
    if (raw.type === "whileHeld") return [{ id, type: "whileHeld", steps, mute: muteIds(raw.mute), ...(releaseStop ? { releaseStop } : {}) }];
    if (raw.type === "ifShort") {
      const under = Number(raw.underMs);
      const cycles = Number(raw.minCycles);
      return [{ id, type: "ifShort", underMs: Number.isFinite(under) && under >= 0 ? Math.round(under) : 150, minCycles: Number.isFinite(cycles) && cycles >= 0 ? Math.round(cycles) : 3, steps }];
    }
    if (raw.type === "then") {
      const forMs = Number(raw.forMs);
      return [{ id, type: "then", forMs: Number.isFinite(forMs) && forMs >= 0 ? Math.round(forMs) : 0, steps }];
    }
    if (raw.type === "repeat") {
      const count = Number(raw.count);
      return [{ id, type: "repeat", count: Number.isFinite(count) && count >= 0 ? Math.round(count) : 1, steps, ...(releaseStop ? { releaseStop } : {}) }];
    }
    if (raw.type === "wait") {
      const ms = Number(raw.ms);
      return [{ id, type: "wait", ms: Number.isFinite(ms) && ms >= 0 ? Math.round(ms) : 0 }];
    }
    if (raw.type === "tapHold") {
      const arm = Number(raw.armMs);
      const gap = Number(raw.gapMs);
      const watch = Array.isArray(raw.watch) ? raw.watch.filter((item) => typeof item === "string" && item).map(canonKey) : [];
      const ignoreMs = Number(raw.ignoreMs);
      return [{
        id,
        type: "tapHold",
        key: canonKey(raw.key || "z"),
        watch,
        armMs: Number.isFinite(arm) && arm >= 0 ? Math.round(arm) : 5,
        gapMs: Number.isFinite(gap) && gap >= 0 ? Math.round(gap) : 80,
        ignore: normalizeIgnoreLock(raw.ignore),
        ignoreMs: Number.isFinite(ignoreMs) && ignoreMs >= 0 ? Math.round(ignoreMs) : 0,
        pauseWatch: normalizePauseWatch(raw.pauseWatch),
      }];
    }
    if (raw.type === "pressHold") {
      const holdMs = Number(raw.holdMs);
      const ms = Number.isFinite(holdMs) && holdMs >= 0 ? Math.round(holdMs) : 80;
      const key = canonKey(raw.key || "");
      const migrated: Step[] = [];
      if (key) {
        migrated.push({ type: "key", action: "down", key }, { type: "wait", ms: 18 }, { type: "key", action: "up", key });
      }
      migrated.push({ type: "scanWait", ms });
      return [{ id, type: "steps", steps: migrated }];
    }
    return [];
  });
}
