import { keyLabel } from "./keyboard";
import { pressLabel } from "./recording";
import {
  basicSteps,
  blankMacro,
  MIN_REPEAT_MS,
  mouseSteps,
  newId,
  writeActive,
  type Block,
  type Macro,
  type Profile,
  type Step,
  type TriggerKind,
} from "./profile";

export function upsertMacro(profile: Profile, macro: Macro): Profile {
  const exists = profile.macros.some((item) => item.id === macro.id);
  const macros = exists
    ? profile.macros.map((item) => (item.id === macro.id ? macro : item))
    : [...profile.macros, macro];
  return writeActive({ ...profile, macros }, macros);
}

export function assignTrigger(profile: Profile, kind: TriggerKind, code: string, macroId: string): Profile {
  const macros = profile.macros.map((item) => {
    if (macroId && item.id === macroId) {
      const trigger = { kind, button: code };
      return {
        ...item,
        trigger,
        steps: item.basic
          ? kind === "key"
            ? basicSteps(code, item.gapMs ?? MIN_REPEAT_MS)
            : mouseSteps(code, item.gapMs ?? MIN_REPEAT_MS)
          : item.steps,
      };
    }
    if (item.trigger.kind === kind && item.trigger.button === code)
      return { ...item, trigger: { ...item.trigger, button: "" } };
    return item;
  });
  return writeActive({ ...profile, macros }, macros);
}

export function freshTrigger(kind: TriggerKind, code: string, extra: Partial<Macro> = {}): Macro {
  return {
    ...blankMacro(),
    name: kind === "key" ? keyLabel(code) : pressLabel(code),
    playMode: "once",
    basic: false,
    gapMs: MIN_REPEAT_MS,
    trigger: { kind, button: code },
    steps: [],
    recording: null,
    ...extra,
  };
}

export function addConfig(profile: Profile): Profile {
  const config = {
    id: newId(),
    name: `Profile ${profile.configs.length + 1}`,
    macros: [] as Macro[],
    focusExe: "",
  };
  const next: Profile = {
    ...profile,
    configs: [...profile.configs, config],
    activeId: config.id,
    name: config.name,
    macros: [],
    focusExe: "",
  };
  return writeActive(next, next.macros);
}

export function deleteConfig(profile: Profile, id: string): Profile {
  if (profile.configs.length < 2) return profile;
  const configs = profile.configs.filter((config) => config.id !== id);
  const active = configs.find((config) => config.id === profile.activeId) ?? configs[0];
  const next: Profile = {
    ...profile,
    configs,
    activeId: active.id,
    name: active.name,
    macros: active.macros,
    focusExe: active.focusExe ?? "",
  };
  return writeActive(next, next.macros);
}

export function deleteMacro(profile: Profile, id: string): Profile {
  const macros = profile.macros.filter((item) => item.id !== id);
  return writeActive({ ...profile, macros }, macros);
}

export function saveStudioMacro(profile: Profile, next: Macro): Profile {
  const prev = profile.macros.find((item) => item.id === next.id);
  const edited =
    prev &&
    (JSON.stringify(prev.steps) !== JSON.stringify(next.steps) ||
      JSON.stringify(prev.recording) !== JSON.stringify(next.recording));
  return upsertMacro(profile, edited ? { ...next, basic: false } : next);
}

export function saveGraphMacro(profile: Profile, next: Macro): Profile {
  const prev = profile.macros.find((item) => item.id === next.id);
  const edited =
    prev &&
    (JSON.stringify(prev.steps) !== JSON.stringify(next.steps) ||
      JSON.stringify(prev.recording) !== JSON.stringify(next.recording) ||
      JSON.stringify(prev.blocks) !== JSON.stringify(next.blocks));
  const bound = edited ? { ...next, basic: false } : next;
  if (bound.trigger.button) {
    const macros = profile.macros.map((item) => {
      if (item.id === bound.id) return bound;
      if (item.trigger.kind === bound.trigger.kind && item.trigger.button === bound.trigger.button)
        return { ...item, trigger: { ...item.trigger, button: "" } };
      return item;
    });
    const exists = macros.some((item) => item.id === bound.id);
    const merged = exists ? macros : [...macros, bound];
    return writeActive({ ...profile, macros: merged }, merged);
  }
  return upsertMacro(profile, bound);
}

function note(issues: string[], text: string) {
  if (!issues.includes(text)) issues.push(text);
}

function stepIssues(steps: Step[], issues: string[]) {
  for (const step of steps) {
    if (step.type === "key" && !step.key.trim()) note(issues, "A key step has no key.");
    if (step.type === "mouse" && !step.button.trim()) note(issues, "A mouse step has no button.");
    if (step.type === "run" && !step.macroId.trim()) note(issues, "A run step needs a macro.");
    if (step.type === "repeat") stepIssues(step.steps, issues);
  }
}

function blockIssues(block: Block, issues: string[]) {
  if (block.type === "tapHold" || block.type === "tapSpam") {
    if (!block.key.trim()) note(issues, "A repress block needs a key.");
    if (!block.watch.length) note(issues, "A repress block needs a watched input.");
  }
  if ("steps" in block) stepIssues(block.steps, issues);
}

/** Why this draft cannot be committed. Empty means Save is allowed. */
export function macroIssues(macro: Macro): string[] {
  const issues: string[] = [];
  if (!macro.name.trim()) note(issues, "Name the macro before saving.");
  for (const block of macro.blocks ?? []) blockIssues(block, issues);
  stepIssues(macro.steps, issues);
  return issues;
}
