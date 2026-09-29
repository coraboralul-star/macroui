import type { Step } from "./profile";
import type { RecEvent } from "./recording";
import { pressLabel } from "./recording";

export type LaneCell =
  | { type: "act"; index: number; part: "down" | "up" | "single"; title: string; subtitle: string }
  | { type: "delay"; index: number; ms: number; via: "wait" | "hold" }
  | { type: "join"; index: number }
  | { type: "move"; index: number; x: number; y: number }
  | { type: "run"; index: number; macroId: string }
  | { type: "repeat"; index: number; count: number; steps: Step[] };

export function liveSteps(events: RecEvent[], mode: "recorded" | "fixed" | "none", fixedMs: number): Step[] {
  const steps: Step[] = [];
  let last = 0;
  events.forEach((event, index) => {
    if (index > 0) {
      if (mode === "fixed") steps.push({ type: "wait", ms: Math.max(0, fixedMs) });
      else if (mode === "recorded" && event.t > last) steps.push({ type: "wait", ms: event.t - last });
    }
    steps.push(
      event.kind === "key"
        ? { type: "key", action: event.down ? "down" : "up", key: event.button }
        : { type: "mouse", action: event.down ? "down" : "up", button: event.button },
    );
    last = event.t;
  });
  return steps;
}

export function laneCells(steps: Step[]): LaneCell[] {
  const cells: LaneCell[] = [];
  let index = 0;
  while (index < steps.length) {
    const step = steps[index];
    if ((step.type === "key" || step.type === "mouse") && step.action === "tap") {
      const name = label(step.type === "key" ? step.key : step.button);
      cells.push({ type: "act", index, part: "down", title: name, subtitle: "Down" });
      cells.push({ type: "delay", index, ms: step.holdMs ?? 20, via: "hold" });
      cells.push({ type: "act", index, part: "up", title: name, subtitle: "Up" });
      index += 1;
      if (index < steps.length && steps[index].type !== "wait") cells.push({ type: "join", index });
      continue;
    }
    if (step.type === "key" || step.type === "mouse") {
      const name = label(step.type === "key" ? step.key : step.button);
      cells.push({
        type: "act",
        index,
        part: step.action === "down" || step.action === "up" ? step.action : "single",
        title: name,
        subtitle: step.action === "down" ? "Down" : step.action === "up" ? "Up" : "Press",
      });
      index += 1;
      if (index < steps.length && steps[index].type === "wait") {
        const wait = steps[index];
        if (wait.type === "wait") cells.push({ type: "delay", index, ms: wait.ms, via: "wait" });
        index += 1;
      } else if (index < steps.length) cells.push({ type: "join", index });
      continue;
    }
    if (step.type === "wait") {
      cells.push({ type: "delay", index, ms: step.ms, via: "wait" });
      index += 1;
      continue;
    }
    if (step.type === "move") cells.push({ type: "move", index, x: step.x, y: step.y });
    else if (step.type === "run") cells.push({ type: "run", index, macroId: step.macroId });
    else cells.push({ type: "repeat", index, count: step.count, steps: step.steps });
    index += 1;
    if (index < steps.length && steps[index].type !== "wait") cells.push({ type: "join", index });
  }
  if (steps.length === 0) return [{ type: "join", index: 0 }];
  if (cells[0]?.type !== "join" || cells[0].index !== 0) cells.unshift({ type: "join", index: 0 });
  const last = cells[cells.length - 1];
  if (last?.type !== "join" || last.index !== steps.length) cells.push({ type: "join", index: steps.length });
  return cells;
}

export function deleteAct(steps: Step[], index: number): Step[] {
  if (!steps[index]) return steps;
  return steps.filter((_, at) => at !== index);
}

export function dropHold(steps: Step[], index: number): Step[] {
  const step = steps[index];
  if (!step || (step.type !== "key" && step.type !== "mouse") || step.action !== "tap") return deleteAct(steps, index);
  const down: Step = step.type === "key"
    ? { type: "key", action: "down", key: step.key }
    : { type: "mouse", action: "down", button: step.button };
  const up: Step = step.type === "key"
    ? { type: "key", action: "up", key: step.key }
    : { type: "mouse", action: "up", button: step.button };
  return [...steps.slice(0, index), down, up, ...steps.slice(index + 1)];
}

export function insertDelay(steps: Step[], index: number): Step[] {
  return insertSteps(steps, index, [{ type: "wait", ms: 15 }]);
}

export function insertSteps(steps: Step[], index: number, extra: Step[]): Step[] {
  const next = steps.slice();
  next.splice(index, 0, ...extra);
  return next;
}

export function pressPair(kind: "key" | "mouse", button: string): Step[] {
  if (kind === "key") {
    return [
      { type: "key", action: "down", key: button },
      { type: "wait", ms: 18 },
      { type: "key", action: "up", key: button },
    ];
  }
  return [
    { type: "mouse", action: "down", button },
    { type: "wait", ms: 18 },
    { type: "mouse", action: "up", button },
  ];
}

export function expandTaps(steps: Step[]): Step[] {
  let changed = false;
  const next = steps.flatMap((step): Step[] => {
    if ((step.type === "key" || step.type === "mouse") && step.action === "tap") {
      changed = true;
      const hold = Math.max(1, step.holdMs ?? 18);
      if (step.type === "key") {
        return [
          { type: "key", action: "down", key: step.key },
          { type: "wait", ms: hold },
          { type: "key", action: "up", key: step.key },
        ];
      }
      return [
        { type: "mouse", action: "down", button: step.button },
        { type: "wait", ms: hold },
        { type: "mouse", action: "up", button: step.button },
      ];
    }
    if (step.type === "repeat") {
      const inner = expandTaps(step.steps);
      if (inner !== step.steps) {
        changed = true;
        return [{ ...step, steps: inner }];
      }
    }
    return [step];
  });
  return changed ? next : steps;
}

export function setDelay(steps: Step[], index: number, ms: number, via: "wait" | "hold"): Step[] {
  return steps.map((step, at) => {
    if (at !== index) return step;
    if (via === "hold" && (step.type === "key" || step.type === "mouse") && step.action === "tap") {
      return { ...step, holdMs: Math.max(1, ms) };
    }
    if (via === "wait" && step.type === "wait") return { ...step, ms: Math.max(0, ms) };
    return step;
  });
}

export function replaceAct(steps: Step[], index: number, button: string, input: "key" | "mouse"): Step[] {
  const step = steps[index];
  if (!step || (step.type !== "key" && step.type !== "mouse")) return steps;
  const next = steps.slice();
  next[index] = retarget(step, button, input);
  return next;
}

export function applyDelayMode(steps: Step[], mode: "fixed" | "none", ms: number): Step[] {
  if (mode === "none") {
    return steps.flatMap((step): Step[] => {
      if (step.type === "wait") return [];
      if (step.type === "repeat") return [{ ...step, steps: applyDelayMode(step.steps, mode, ms) }];
      if ((step.type === "key" || step.type === "mouse") && step.action === "tap") {
        return step.type === "key"
          ? [{ type: "key", action: "down", key: step.key }, { type: "key", action: "up", key: step.key }]
          : [{ type: "mouse", action: "down", button: step.button }, { type: "mouse", action: "up", button: step.button }];
      }
      return [step];
    });
  }
  return steps.map((step) => {
    if (step.type === "wait") return { ...step, ms };
    if (step.type === "repeat") return { ...step, steps: applyDelayMode(step.steps, mode, ms) };
    if ((step.type === "key" || step.type === "mouse") && step.action === "tap") return { ...step, holdMs: ms };
    return step;
  });
}

export function moveStep(steps: Step[], from: number, slot: number): Step[] {
  if (!canPlace(steps, from, slot) || slot === from || slot === from + 1) return steps;
  const step = steps[from];
  const next = steps.filter((_, index) => index !== from);
  next.splice(slot > from ? slot - 1 : slot, 0, step);
  return next;
}

export function canPlace(steps: Step[], from: number, slot: number): boolean {
  if (from < 0 || from >= steps.length || slot < 0 || slot > steps.length) return false;
  if (slot === from || slot === from + 1) return true;
  const step = steps[from];
  const mate = partner(steps, from);
  if (mate == null || (step.type !== "key" && step.type !== "mouse")) return true;
  const dest = slot > from ? slot - 1 : slot;
  const mateAfter = mate > from ? mate - 1 : mate;
  const mateFinal = dest <= mateAfter ? mateAfter + 1 : mateAfter;
  if (step.action === "down" && dest >= mateFinal) return false;
  if (step.action === "up" && dest <= mateFinal) return false;
  return true;
}

function partner(steps: Step[], index: number): number | null {
  const step = steps[index];
  if (!step || (step.type !== "key" && step.type !== "mouse") || (step.action !== "down" && step.action !== "up")) return null;
  if (step.action === "down") {
    let depth = 0;
    for (let at = index + 1; at < steps.length; at += 1) {
      const other = steps[at];
      if (!samePress(step, other) || (other.type !== "key" && other.type !== "mouse")) continue;
      if (other.action === "down") depth += 1;
      else if (other.action === "up") {
        if (depth === 0) return at;
        depth -= 1;
      }
    }
    return null;
  }
  let depth = 0;
  for (let at = index - 1; at >= 0; at -= 1) {
    const other = steps[at];
    if (!samePress(step, other) || (other.type !== "key" && other.type !== "mouse")) continue;
    if (other.action === "up") depth += 1;
    else if (other.action === "down") {
      if (depth === 0) return at;
      depth -= 1;
    }
  }
  return null;
}

function label(button: string) {
  const name = pressLabel(button);
  if (name.length === 1) return name.toUpperCase();
  return name.charAt(0).toUpperCase() + name.slice(1);
}

function samePress(a: Step, b: Step) {
  if ((a.type !== "key" && a.type !== "mouse") || (b.type !== "key" && b.type !== "mouse")) return false;
  const left = a.type === "key" ? a.key : a.button;
  const right = b.type === "key" ? b.key : b.button;
  return a.type === b.type && left === right;
}

function retarget(step: Step, button: string, input: "key" | "mouse"): Step {
  if (step.type !== "key" && step.type !== "mouse") return step;
  if (input === "key") return { type: "key", action: step.action, key: button, holdMs: step.holdMs };
  return { type: "mouse", action: step.action, button, holdMs: step.holdMs };
}
