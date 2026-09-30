import { keyLabel } from "./keyboard";
import type { Block, Macro, PlayMode, Step } from "./profile";
import { pressLabel } from "./recording";

export function effectivePlayMode(macro: Macro): PlayMode {
  const tapHold = (macro.blocks ?? []).some((block) => block.type === "tapHold");
  if (tapHold) return "whileHeld";
  if (macro.advanced && macro.playMode === "whileHeld") return "once";
  return macro.playMode;
}

export function playModeLabel(mode: PlayMode): string {
  if (mode === "whileHeld") return "While held";
  if (mode === "onRelease") return "On release";
  if (mode === "repeat") return "Repeat";
  if (mode === "toggle") return "Toggle";
  return "Once";
}

export function triggerInspect(trigger: string, mode: PlayMode, coerced: boolean): string {
  if (!trigger) return "Bind a key or mouse button from Remap or Mouse.";
  if (mode === "whileHeld") return `${trigger} must stay down. Let go and this macro stops. Bind it from Remap or Mouse.`;
  if (mode === "onRelease") return `${trigger} going down does nothing. The macro starts when ${trigger} comes up. Bind it from Remap or Mouse.`;
  if (mode === "toggle") return `Press ${trigger} to start. Press ${trigger} again to stop. It does not need to stay held. Bind it from Remap or Mouse.`;
  if (mode === "repeat") return `One press of ${trigger} starts it. You do not keep it held. Bind it from Remap or Mouse.`;
  if (coerced) return `One press of ${trigger} starts this advanced macro. It does not stay held. Bind it from Remap or Mouse.`;
  return `One press of ${trigger} starts it. Hold length does not start it again. Bind it from Remap or Mouse.`;
}

export function shownBlocks(macro: Macro): Block[] {
  if (macro.blocks && macro.blocks.length) return macro.blocks;
  if (macro.steps.length) return [{ id: `${macro.id}-steps`, type: "steps", steps: macro.steps }];
  return [];
}

export function place(item: Macro): string {
  if (!item.trigger.button) return "";
  if (item.trigger.kind === "key") return keyLabel(item.trigger.button);
  return pressLabel(item.trigger.button);
}

export function heldName(button: string): string {
  if (button === "LShift" || button === "RShift" || button === "shift") return "Shift";
  if (button === "CapsLock") return "Caps";
  if (button === "LButton" || button === "RButton" || button === "MButton" || button === "XButton1" || button === "XButton2")
    return pressLabel(button);
  return keyLabel(button);
}

export function phrase(steps: Step[]): string {
  return steps.map(stepPhrase).filter(Boolean).join(", ");
}

export function stepPhrase(step: Step): string {
  if (step.type === "key") {
    const name = keyLabel(step.key);
    if (step.action === "down") return `${name} down`;
    if (step.action === "up") return `${name} up`;
    return `press ${name}`;
  }
  if (step.type === "mouse") {
    const name = pressLabel(step.button);
    if (step.action === "down") return `${name} down`;
    if (step.action === "up") return `${name} up`;
    return `click ${name}`;
  }
  if (step.type === "wait") return `wait ${step.ms} ms`;
  if (step.type === "move") return `move ${step.x}, ${step.y}`;
  if (step.type === "goto") return `go to ${step.x}, ${step.y} (${step.where || "screen"})`;
  if (step.type === "repeat") return `repeat ${step.count}`;
  if (step.type === "run") return "run a macro";
  return "";
}

export function watchVerb(count: number): "is" | "are" {
  return count === 1 ? "is" : "are";
}

export function repressLine(block: Extract<Block, { type: "tapHold" }>): string {
  const key = heldName(block.key) || "Trigger";
  const tracked = block.watch.map(heldName);
  const when = tracked.length ? tracked.join(" ") : "Tracked Keys";
  return `Repress ${key} ${block.gapMs}ms after ${when} ${watchVerb(tracked.length)} pressed`;
}

function pushBeats(steps: Step[], out: string[]) {
  for (const step of steps) {
    if (step.type === "key" || step.type === "mouse") {
      const name = step.type === "key" ? keyLabel(step.key) : pressLabel(step.button);
      if (step.action === "down") out.push(`${name} down`);
      else if (step.action === "up") out.push(`${name} up`);
      else {
        out.push(name);
        if (step.holdMs) out.push(`${step.holdMs} ms`);
      }
    } else if (step.type === "wait") out.push(`${step.ms} ms`);
    else if (step.type === "move") out.push(`move ${step.x}, ${step.y}`);
    else if (step.type === "goto") out.push(`go to ${step.x}, ${step.y} (${step.where || "screen"})`);
    else if (step.type === "repeat") {
      out.push(step.count === 0 ? "until it stops" : `${step.count} times`);
      pushBeats(step.steps, out);
    } else if (step.type === "run") out.push("run a macro");
  }
}

function clipBeats(beats: string[], limit = 4): string {
  if (!beats.length) return "";
  if (beats.length <= limit) return beats.join(", ");
  return `${beats.slice(0, limit).join(", ")} ...`;
}

export function nodeHint(block: Block): string {
  if (block.type === "wait") return `${block.ms} ms`;
  if (block.type === "tapHold") {
    const key = heldName(block.key) || "Key";
    const tracked = block.watch.map((item) => heldName(item)).filter(Boolean);
    return clipBeats(tracked.length ? [key, "after", ...tracked] : [key]);
  }
  if (block.type === "repeat" && !block.steps.length) return block.count === 0 ? "Until it stops" : `${block.count} times`;
  const beats: string[] = [];
  if (block.type === "ifShort") beats.push(`${block.minCycles} times`);
  if (block.type === "repeat") beats.push(block.count === 0 ? "Until it stops" : `${block.count} times`);
  if ("steps" in block) pushBeats(block.steps, beats);
  if (block.type === "then" && block.forMs) beats.push(`${block.forMs} ms`);
  return clipBeats(beats);
}

export function blockMeta(block: Block): string {
  if (block.type === "wait") return `${block.ms} ms`;
  if (block.type === "tapHold") return `after ${block.gapMs} ms`;
  if (block.type === "ifShort") return `${block.minCycles} times`;
  if (block.type === "then") return block.forMs ? `for ${block.forMs} ms` : "";
  if (block.type === "repeat") return block.count === 0 ? "Until it stops" : `${block.count}×`;
  const count = block.steps.length;
  return count ? `${count} steps` : "Empty";
}

export function blockHint(block: Block): string {
  if (block.type === "wait") return `${block.ms} ms`;
  if (block.type === "tapHold") return repressLine(block);
  if (block.type === "ifShort") return `released before ${block.underMs} ms · keep going ${block.minCycles}×`;
  if (block.type === "then") return block.forMs ? `runs ${block.forMs} ms after release` : "runs after release";
  if (block.type === "repeat") return block.count === 0 ? "until it stops" : `${block.count}×`;
  return phrase(block.steps);
}

export function flowText(block: Block, trigger: string): string {
  if (block.type === "wait") return `Wait ${block.ms} ms`;
  if (block.type === "tapHold") return repressLine(block);
  const body = phrase(block.steps);
  if (block.type === "whileHeld") {
    const lead = trigger ? `While ${trigger} is held` : "While the trigger is held";
    return body ? `${lead}: ${body}` : lead;
  }
  if (block.type === "ifShort") {
    const lead = `If released before ${block.underMs} ms, keep going ${block.minCycles}×`;
    return body ? `${lead}: ${body}` : lead;
  }
  if (block.type === "then") {
    const lead = block.forMs > 0 ? `After release, for ${block.forMs} ms` : "After release";
    return body ? `${lead}: ${body}` : lead;
  }
  if (block.type === "repeat") {
    const lead = block.count === 0 ? "Repeat until it stops" : `Repeat ${block.count} times`;
    return body ? `${lead}: ${body}` : lead;
  }
  return body ? `Do once: ${body}` : "Do once";
}

export function linkLabel(from: Block | null, to: Block): string {
  if (to.type === "ifShort") return "if released before";
  if (to.type === "then") return "after release";
  if (from?.type === "repeat") return "again";
  return "then";
}

function actsNoun(block: Block): string {
  if (block.type === "wait" || block.type === "tapHold") return "those actions";
  if (!block.steps.length) return "those actions";
  const keys = [...new Set(block.steps.flatMap((step) => (step.type === "key" ? [keyLabel(step.key)] : step.type === "mouse" ? [pressLabel(step.button)] : [])))];
  if (keys.length === 1) return `the ${keys[0]} key actions`;
  const short = phrase(block.steps).replace(/repeat 0/g, "repeat until it stops");
  return short.length <= 56 ? short : "those actions";
}

function sourceLine(source: Block | null, key: string): string {
  if (!source) return key === "the trigger" ? "When this macro starts, it is waiting for a trigger." : `After you press ${key}, the macro starts.`;
  if (source.type === "whileHeld") return `While you hold ${key}, it repeatedly sends ${actsNoun(source)}.`;
  if (source.type === "ifShort") {
    return `If you release ${key} before ${source.underMs} ms, it sends ${actsNoun(source)} ${source.minCycles} times.`;
  }
  if (source.type === "then") {
    return source.forMs
      ? `After you release ${key}, it sends ${actsNoun(source)} for ${source.forMs} ms.`
      : `After you release ${key}, it sends ${actsNoun(source)}.`;
  }
  if (source.type === "repeat") {
    return source.count === 0
      ? `The Repeat block keeps sending ${actsNoun(source)} until it stops.`
      : `The Repeat block sends ${actsNoun(source)} ${source.count} times.`;
  }
  if (source.type === "wait") return `The Wait block pauses for ${source.ms} ms.`;
  if (source.type === "tapHold") return `${repressLine(source)}.`;
  if (source.type === "steps") return `The Do-once block sends ${actsNoun(source)}.`;
  return "This block finishes.";
}

function hopLine(source: Block | null, target: Block, key: string): string {
  const fromName = !source ? "the start" : source.type === "whileHeld" ? "the While-held section" : source.type === "ifShort" ? "the early-release block" : source.type === "then" ? "the After-release block" : source.type === "steps" ? "the Do-once block" : source.type === "repeat" ? "the Repeat block" : source.type === "wait" ? "the Wait block" : "this block";
  if (target.type === "ifShort") return `If you release ${key} early, it switches to the early-release actions.`;
  if (target.type === "then") {
    const dur = target.forMs ? ` for ${target.forMs} ms` : "";
    return source?.type === "whileHeld"
      ? `After the While-held section finishes, it moves to the After-release block and sends ${actsNoun(target)}${dur}.`
      : `It then moves to the After-release block and sends ${actsNoun(target)}${dur}.`;
  }
  if (target.type === "whileHeld") return `It then begins the While-held block and repeatedly sends ${actsNoun(target)} while you hold ${key}.`;
  if (target.type === "steps") return `It then moves on and sends ${actsNoun(target)} once.`;
  if (target.type === "repeat") {
    return target.count === 0
      ? `It then moves to the Repeat block and keeps going until it stops.`
      : `It then moves to the Repeat block and sends ${actsNoun(target)} ${target.count} times.`;
  }
  if (target.type === "wait") return `It then waits ${target.ms} ms.`;
  if (target.type === "tapHold") return `It then moves to the Repress block.`;
  return `After ${fromName} finishes, it continues to the next block.`;
}

export function linkStory(trigger: string, blocks: Block[], edgeIndex: number): string {
  const key = trigger || "the trigger";
  const source = edgeIndex <= 0 ? null : blocks[edgeIndex - 1] ?? null;
  const target = blocks[edgeIndex];
  if (!target) return "This connection continues the macro.";
  return `${sourceLine(source, key)} ${hopLine(source, target, key)}`;
}

export type PreviewScene = {
  id: string;
  nodeId: string;
  title: string;
  why: string;
  affect: string;
  kind: Block["type"] | "heldPath";
  underMs?: number;
};

export const GRAPH_NODE_W = 248;
export const GRAPH_NODE_GAP = 84;
export const GRAPH_NODE_H = 116;
export const GRAPH_ORIGIN_X = 80;

export type GraphPlace = {
  id: string;
  hint: string;
  x: number;
  y: number;
  kind: string;
  underMs?: number;
};

export type GraphLink = { from: string; to: string; fork?: "early" | "held" };

export type GraphSlot = { index: number; x: number; y: number };

export type GraphLabel = { id: string; text: string; x: number; y: number };

function forkLabel(from: GraphPlace, to: GraphPlace, id: string, text: string, t = 0.5, dx = 0, dy = 0): GraphLabel {
  const x1 = from.x + GRAPH_NODE_W;
  const y1 = from.y + GRAPH_NODE_H / 2;
  const x2 = to.x;
  const y2 = to.y + GRAPH_NODE_H / 2;
  const mid = (x1 + x2) / 2;
  const a = 1 - t;
  return {
    id,
    text,
    x: a * a * a * x1 + 3 * a * a * t * mid + 3 * a * t * t * mid + t * t * t * x2 + dx,
    y: a * a * a * y1 + 3 * a * a * t * y1 + 3 * a * t * t * y2 + t * t * t * y2 + dy,
  };
}

export function splitExplain(blocks: Block[], trigger: string): string {
  const key = trigger || "the bind";
  const index = blocks.findIndex((block, i) => block.type === "ifShort" && blocks[i - 1]?.type === "whileHeld");
  if (index >= 0) {
    const tap = blocks[index] as Extract<Block, { type: "ifShort" }>;
    return `This split is live because Quick tap sits right after the hold loop. Let go of ${key} before ${tap.underMs} ms and the top path runs. Hold longer and the bottom path waits, then the next block. Preview plays both so you can see which one you get.`;
  }
  const hold = blocks.some((block) => block.type === "whileHeld");
  const tap = blocks.some((block) => block.type === "ifShort");
  if (hold && !tap) return "This hold loop has no let-go-early path yet. Add Quick tap right after it and the split appears - early tap on top, keep holding on the bottom.";
  if (tap && !hold) return "Quick tap only splits if it sits right after a hold loop. Add Hold loop before it and the fork is created for you.";
  return "Hold loop and Quick tap sit next to each other. Add either one and the split is created for you - let go early vs keep holding.";
}

export function setupNote(block: Block, prev: Block | null, next: Block | null, trigger: string): string {
  const key = trigger || "the bind";
  if (block.type === "whileHeld" && next?.type === "ifShort") {
    return `Let go of ${key} before ${next.underMs} ms and it takes the top path. Hold past that and this loop just waits, then continues.`;
  }
  if (block.type === "whileHeld") return "Add Quick tap after this and a split appears - let go early vs keep holding.";
  if (block.type === "ifShort" && prev?.type === "whileHeld") {
    return `This is the let-go-early path. You get it because Quick tap sits right after the hold loop.`;
  }
  if (block.type === "ifShort") return "Put this right after a hold loop to split early-tap vs held-too-long.";
  if (block.type === "then") return `This runs after ${key} is already up. Hold length does not matter here.`;
  if (block.type === "repeat") return block.count === 0 ? "This repeats until it stops. It is not tied to holding the bind." : `This repeats ${block.count} times on its own count.`;
  if (block.type === "wait") return `This pause delays the next block by ${block.ms} ms.`;
  if (block.type === "tapHold") return repressLine(block);
  return "This block runs once, then hands off. Record keys in Edit.";
}

export function graphLayout(blocks: Block[]): {
  nodes: GraphPlace[];
  links: GraphLink[];
  slots: GraphSlot[];
  labels: GraphLabel[];
  width: number;
  height: number;
  addX: number;
  addY: number;
} {
  const split = blocks.some((block, index) => block.type === "ifShort" && blocks[index - 1]?.type === "whileHeld");
  const mid = split ? 196 : 64;
  const rise = 148;
  const xOf = (col: number) => GRAPH_ORIGIN_X + col * (GRAPH_NODE_W + GRAPH_NODE_GAP);
  const nodes: GraphPlace[] = [];
  const links: GraphLink[] = [];
  const labels: GraphLabel[] = [];
  let col = 0;

  for (let i = 0; i < blocks.length; i++) {
    const block = blocks[i];
    const prev = i > 0 ? blocks[i - 1] ?? null : null;
    const next = blocks[i + 1] ?? null;
    const fork = block.type === "ifShort" && prev?.type === "whileHeld";

    if (fork && prev) {
      nodes.push({
        id: block.id,
        hint: nodeHint(block),
        x: xOf(col),
        y: mid - rise,
        kind: block.type,
      });
      nodes.push({
        id: `${prev.id}-held`,
        hint: "Wait for release",
        x: xOf(col),
        y: mid + rise,
        kind: "heldPath",
        underMs: block.underMs,
      });
      links.push({ from: prev.id, to: block.id, fork: "early" });
      links.push({ from: prev.id, to: `${prev.id}-held`, fork: "held" });
      const holdNode = nodes.find((node) => node.id === prev.id);
      const earlyNode = nodes[nodes.length - 2];
      const heldNode = nodes[nodes.length - 1];
      if (holdNode && earlyNode && heldNode) {
        labels.push(forkLabel(holdNode, earlyNode, `${block.id}-lab-early`, `Let go before ${block.underMs} ms`, 0.46, -78, -16));
        labels.push(forkLabel(holdNode, heldNode, `${prev.id}-lab-held`, "Keep holding", 0.52, -10, 8));
      }
      if (next) {
        links.push({ from: block.id, to: next.id, fork: "early" });
        links.push({ from: `${prev.id}-held`, to: next.id, fork: "held" });
      }
      col += 1;
      continue;
    }

    const afterFork = prev?.type === "ifShort" && blocks[i - 2]?.type === "whileHeld";
    nodes.push({
      id: block.id,
      hint: nodeHint(block),
      x: xOf(col),
      y: mid,
      kind: block.type,
    });
    if (prev && !afterFork) links.push({ from: prev.id, to: block.id });
    col += 1;
  }

  const maxX = nodes.reduce((value, node) => Math.max(value, node.x), 0);
  const maxY = nodes.reduce((value, node) => Math.max(value, node.y), mid);
  const addX = (nodes.length ? maxX : GRAPH_ORIGIN_X - GRAPH_NODE_W - GRAPH_NODE_GAP) + GRAPH_NODE_W + GRAPH_NODE_GAP;
  const addY = mid + GRAPH_NODE_H / 2 - 18;
  const pinY = (node: GraphPlace) => node.y + GRAPH_NODE_H / 2 - 18;
  const real = (id: string) => nodes.find((node) => node.id === id);
  const slots: GraphSlot[] = [];
  if (!blocks.length) {
    slots.push({ index: 0, x: addX, y: addY });
  } else {
    const first = real(blocks[0].id);
    if (first) slots.push({ index: 0, x: first.x - GRAPH_NODE_GAP / 2 - 18, y: pinY(first) });
    for (let i = 0; i < blocks.length - 1; i++) {
      const from = real(blocks[i].id);
      const to = real(blocks[i + 1].id);
      if (!from || !to) continue;
      slots.push({
        index: i + 1,
        x: (from.x + GRAPH_NODE_W + to.x) / 2 - 18,
        y: (pinY(from) + pinY(to)) / 2,
      });
    }
    slots.push({ index: blocks.length, x: addX, y: addY });
  }
  return {
    nodes,
    links,
    slots,
    labels,
    width: Math.max((nodes.length ? maxX : GRAPH_ORIGIN_X) + GRAPH_NODE_W + GRAPH_NODE_GAP + 80, 720),
    height: Math.max(maxY + GRAPH_NODE_H + 56, mid + GRAPH_NODE_H + 56),
    addX,
    addY,
  };
}

function sceneTitle(block: Block, key: string): string {
  const bind = !key || key === "the trigger" ? "" : key;
  if (block.type === "whileHeld") return bind ? `While ${bind} held` : "While held";
  if (block.type === "ifShort") return bind ? `If ${bind} released before ${block.underMs} ms` : `If released before ${block.underMs} ms`;
  if (block.type === "then") {
    const tail = block.forMs ? `, for ${block.forMs} ms` : "";
    return bind ? `After ${bind} release${tail}` : `After release${tail}`;
  }
  if (block.type === "repeat") return block.count === 0 ? "Repeat until it stops" : `Repeat ${block.count} times`;
  if (block.type === "wait") return `Wait ${block.ms} ms`;
  if (block.type === "tapHold") return repressLine(block);
  return "Do once";
}

function emptyActs(block: Block) {
  return block.type !== "wait" && block.type !== "tapHold" && !block.steps.length;
}

function sceneCopy(block: Block, prev: Block | null, next: Block | null, key: string): { why: string; affect: string } {
  const acts = actsNoun(block);
  const vacant = emptyActs(block) ? " This block has no keys yet, so the preview only shows the rule." : "";
  if (block.type === "whileHeld") {
    return {
      why: `This is the hold loop. It exists so ${acts} only fire while ${key} is down.`,
      affect: next?.type === "ifShort"
        ? `A long hold stays here. A tap shorter than ${next.underMs} ms leaves this loop and uses the next block instead.${vacant}`
        : `When ${key} comes up this loop stops, which is what lets the next block run.${vacant}`,
    };
  }
  if (block.type === "ifShort") {
    return {
      why: prev?.type === "whileHeld"
        ? `This is the short-tap path. The hold loop cannot do this - that loop only runs while ${key} is still down.`
        : `This block only exists for a tap that lets go of ${key} before ${block.underMs} ms.`,
      affect: `Release ${key} before ${block.underMs} ms and it sends ${acts} ${block.minCycles} times. Hold longer and this block is skipped.${vacant}`,
    };
  }
  if (block.type === "then") {
    const dur = block.forMs ? ` for ${block.forMs} ms` : "";
    return {
      why: `This is the after-release path. It does not care how long you held - only that ${key} is already up.`,
      affect: prev?.type === "ifShort"
        ? `After the early-tap burst, it still sends ${acts}${dur}. That is separate from both the hold loop and the short-tap path.${vacant}`
        : `Once ${key} is released, it sends ${acts}${dur}. That is not the hold loop.${vacant}`,
    };
  }
  if (block.type === "repeat") {
    return {
      why: `This repeats on its own count, not because ${key} is held.`,
      affect: block.count === 0
        ? `It keeps sending ${acts} until it stops, then the next block can run.${vacant}`
        : `It sends ${acts} ${block.count} times, then hands off.${vacant}`,
    };
  }
  if (block.type === "wait") {
    return {
      why: "This pause is here so the next block does not start on the same frame.",
      affect: `Everything after this is delayed by ${block.ms} ms.`,
    };
  }
  if (block.type === "tapHold") {
    return {
      why: "This watches other keys and only represses after that gap - it is not a hold loop.",
      affect: `${repressLine(block)}.`,
    };
  }
  return {
    why: prev
      ? `This runs once in the chain. It is not a hold rule and not a release rule.`
      : `Right after ${key} starts the macro, this block runs once.`,
    affect: `It sends ${acts} a single time, then the next block is allowed to run.${vacant}`,
  };
}

export function previewScenes(trigger: string, blocks: Block[]): PreviewScene[] {
  const key = trigger || "the trigger";
  const scenes: PreviewScene[] = [];
  blocks.forEach((block, index) => {
    const prev = index === 0 ? null : blocks[index - 1] ?? null;
    const next = blocks[index + 1] ?? null;
    const copy = sceneCopy(block, prev, next, key);
    scenes.push({
      id: block.id,
      nodeId: block.id,
      title: sceneTitle(block, key),
      why: copy.why,
      affect: copy.affect,
      kind: block.type,
    });
    if (block.type === "ifShort" && prev?.type === "whileHeld") {
      scenes.push({
        id: `${prev.id}-held`,
        nodeId: `${prev.id}-held`,
        title: `Held past ${block.underMs} ms`,
        why: `This is the other way out of the hold loop. You kept ${key} down past ${block.underMs} ms.`,
        affect: `The hold loop just waits until ${key} comes up. The short-tap path is skipped, then the next block can run.`,
        kind: "heldPath",
        underMs: block.underMs,
      });
    }
  });
  return scenes;
}
