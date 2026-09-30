import type { GotoWhere, KeyAction, Step } from "./profile";
import { normalizeGotoWhere } from "./profile";

export type Path = number[];

export type PressNode = {
  type: "press";
  index: number;
  span: number;
  input: "key" | "mouse";
  button: string;
  action: KeyAction;
  holdMs?: number;
};

export type TapeNode =
  | PressNode
  | { type: "wait"; index: number; ms: number }
  | { type: "move"; index: number; x: number; y: number }
  | { type: "goto"; index: number; x: number; y: number; ms: number; where: GotoWhere }
  | { type: "run"; index: number; macroId: string }
  | { type: "repeat"; index: number; count: number; steps: Step[] };

export function segment(steps: Step[]): TapeNode[] {
  const nodes: TapeNode[] = [];
  let index = 0;
  while (index < steps.length) {
    const step = steps[index];
    if ((step.type === "key" || step.type === "mouse") && step.action === "down") {
      const button = step.type === "key" ? step.key : step.button;
      const hold = steps[index + 1];
      const up = steps[index + 2];
      const upButton = up && (up.type === "key" || up.type === "mouse") ? (up.type === "key" ? up.key : up.button) : "";
      if (hold?.type === "wait" && up && (up.type === "key" || up.type === "mouse") && up.type === step.type && up.action === "up" && upButton === button) {
        nodes.push({ type: "press", index, span: 3, input: step.type, button, action: "tap", holdMs: hold.ms });
        index += 3;
        continue;
      }
    }
    if (step.type === "key" || step.type === "mouse") {
      nodes.push({
        type: "press",
        index,
        span: 1,
        input: step.type,
        button: step.type === "key" ? step.key : step.button,
        action: step.action,
        holdMs: step.holdMs,
      });
    } else if (step.type === "wait") nodes.push({ type: "wait", index, ms: step.ms });
    else if (step.type === "move") nodes.push({ type: "move", index, x: step.x, y: step.y });
    else if (step.type === "goto") nodes.push({ type: "goto", index, x: step.x, y: step.y, ms: step.ms ?? 15, where: normalizeGotoWhere(step.where) });
    else if (step.type === "run") nodes.push({ type: "run", index, macroId: step.macroId });
    else nodes.push({ type: "repeat", index, count: step.count, steps: step.steps });
    index += 1;
  }
  return nodes;
}

export function spanOf(node: TapeNode) {
  return node.type === "press" ? node.span : 1;
}

export function readList(steps: Step[], path: Path): Step[] {
  let list = steps;
  for (const index of path) {
    const step = list[index];
    if (!step || step.type !== "repeat") return [];
    list = step.steps;
  }
  return list;
}

export function writeList(steps: Step[], path: Path, next: Step[]): Step[] {
  if (path.length === 0) return next;
  const [head, ...rest] = path;
  return steps.map((step, index) => {
    if (index !== head || step.type !== "repeat") return step;
    return { ...step, steps: writeList(step.steps, rest, next) };
  });
}

export function moveSpan(steps: Step[], index: number, span: number, to: number): Step[] {
  if (to >= index && to <= index + span) return steps;
  const block = steps.slice(index, index + span);
  const rest = [...steps.slice(0, index), ...steps.slice(index + span)];
  const dest = to > index ? to - span : to;
  rest.splice(Math.max(0, dest), 0, ...block);
  return rest;
}

export function moveBlock(root: Step[], from: Path, index: number, span: number, toPath: Path, to: number): Step[] {
  if (samePath(from, toPath)) return writeList(root, from, moveSpan(readList(root, from), index, span, to));
  const adjusted = shiftPath(toPath, from, index, span);
  if (!adjusted) return root;
  const block = readList(root, from).slice(index, index + span);
  const removed = writeList(root, from, cut(readList(root, from), index, span));
  const dest = readList(removed, adjusted);
  const next = dest.slice();
  next.splice(Math.max(0, Math.min(to, next.length)), 0, ...block);
  return writeList(removed, adjusted, next);
}

function cut(steps: Step[], index: number, span: number) {
  return [...steps.slice(0, index), ...steps.slice(index + span)];
}

function samePath(a: Path, b: Path) {
  return a.length === b.length && a.every((item, index) => item === b[index]);
}

function shiftPath(path: Path, from: Path, index: number, span: number): Path | null {
  if (path.length < from.length || !from.every((item, i) => item === path[i])) return path;
  if (path.length === from.length) return path;
  const at = path[from.length];
  if (at >= index && at < index + span) return null;
  if (at >= index + span) {
    const copy = path.slice();
    copy[from.length] = at - span;
    return copy;
  }
  return path;
}

export function patchPress(steps: Step[], node: PressNode, patch: { button?: string; action?: KeyAction; holdMs?: number; input?: "key" | "mouse" }): Step[] {
  const button = patch.button ?? node.button;
  const input = patch.input ?? node.input;
  const action = patch.action ?? (node.span === 3 ? "tap" : node.action);
  const hold = Math.max(1, patch.holdMs ?? node.holdMs ?? 20);
  let replacement: Step[];
  if (action === "down" || action === "up") {
    replacement = [input === "key" ? { type: "key", action, key: button } : { type: "mouse", action, button }];
  } else if (node.span === 3 && patch.action == null && patch.input == null) {
    replacement = [
      input === "key" ? { type: "key", action: "down", key: button } : { type: "mouse", action: "down", button },
      { type: "wait", ms: hold },
      input === "key" ? { type: "key", action: "up", key: button } : { type: "mouse", action: "up", button },
    ];
  } else {
    replacement = [input === "key" ? { type: "key", action: "tap", key: button, holdMs: hold } : { type: "mouse", action: "tap", button, holdMs: hold }];
  }
  return [...steps.slice(0, node.index), ...replacement, ...steps.slice(node.index + node.span)];
}

export function cyclePress(node: PressNode): KeyAction {
  if (node.span === 3 || node.action === "tap") return "down";
  if (node.action === "down") return "up";
  return "tap";
}
