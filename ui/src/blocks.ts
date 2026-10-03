import { newId, type Block, type PlayMode, type Step } from "./profile";

export function createBlock(type: Block["type"]): Block {
  const id = newId();
  if (type === "whileHeld") return { id, type, steps: [], mute: [] };
  if (type === "steps") return { id, type, steps: [] };
  if (type === "ifShort") return { id, type, underMs: 150, minCycles: 3, steps: [] };
  if (type === "swapAfter") return { id, type, afterMs: 150, steps: [], mute: [] };
  if (type === "then") return { id, type, forMs: 55, steps: [] };
  if (type === "repeat") return { id, type, count: 1, steps: [] };
  if (type === "tapHold") return { id, type, key: "z", watch: ["LShift", "CapsLock", "t", "XButton2", "p"], armMs: 5, gapMs: 80, ignore: "off", ignoreMs: 150, pauseWatch: "off" };
  return { id, type: "wait", ms: 13 };
}

export function cloneBlock(block: Block): Block {
  return { ...structuredClone(block), id: newId() };
}

export function reorderBlocks(blocks: Block[], fromId: string, toIndex: number): Block[] {
  const from = blocks.findIndex((block) => block.id === fromId);
  if (from < 0 || toIndex < 0 || toIndex > blocks.length) return blocks;
  if (from === toIndex || from + 1 === toIndex) return blocks;
  const next = blocks.slice();
  const [item] = next.splice(from, 1);
  next.splice(from < toIndex ? toIndex - 1 : toIndex, 0, item);
  return next;
}

export function shiftBlock(blocks: Block[], id: string, dir: -1 | 1): Block[] {
  const from = blocks.findIndex((block) => block.id === id);
  const to = from + dir;
  if (from < 0 || to < 0 || to >= blocks.length) return blocks;
  const next = blocks.slice();
  const [item] = next.splice(from, 1);
  next.splice(to, 0, item);
  return next;
}

export function insertTyped(blocks: Block[], index: number, type: Block["type"]): { blocks: Block[]; pick: string } {
  const next = blocks.slice();
  const at = Math.max(0, Math.min(index, next.length));
  if (type === "whileHeld") {
    const hold = createBlock("whileHeld");
    next.splice(at, 0, hold);
    const after = next[at + 1]?.type;
    if (after !== "ifShort" && after !== "swapAfter") next.splice(at + 1, 0, createBlock("ifShort"));
    return { blocks: next, pick: hold.id };
  }
  if (type === "ifShort") {
    const tap = createBlock("ifShort");
    if (next[at - 1]?.type !== "whileHeld") {
      const hold = createBlock("whileHeld");
      next.splice(at, 0, hold, tap);
      return { blocks: next, pick: tap.id };
    }
    next.splice(at, 0, tap);
    return { blocks: next, pick: tap.id };
  }
  if (type === "swapAfter") {
    const swap = createBlock("swapAfter");
    const prev = next[at - 1]?.type;
    if (prev === "whileHeld" || prev === "swapAfter") {
      next.splice(at, 0, swap);
      return { blocks: next, pick: swap.id };
    }
    const hold = createBlock("whileHeld");
    next.splice(at, 0, hold, swap);
    return { blocks: next, pick: swap.id };
  }
  const item = createBlock(type);
  next.splice(at, 0, item);
  return { blocks: next, pick: item.id };
}

/** Steps need a block to live in, so a loose step dropped on the canvas gets a Run Once. */
export function insertStepBlock(blocks: Block[], index: number, steps: Step[]): { blocks: Block[]; pick: string } {
  const item: Block = { id: newId(), type: "steps", steps };
  const next = blocks.slice();
  next.splice(Math.max(0, Math.min(index, next.length)), 0, item);
  return { blocks: next, pick: item.id };
}

export function appendSteps(block: Block, steps: Step[]): Block {
  if (!takesSteps(block)) return block;
  return { ...block, steps: [...block.steps, ...steps] };
}

export function takesSteps(block: Block): block is Extract<Block, { steps: Step[] }> {
  return block.type !== "wait" && block.type !== "tapHold";
}

/** Scan wait only belongs where the trigger is still supposed to be down. */
export function allowsScanWait(block: Block | null, playMode?: PlayMode): boolean {
  if (playMode === "onRelease") return false;
  if (!block) return true;
  return block.type === "whileHeld" || block.type === "swapAfter" || block.type === "steps" || block.type === "repeat";
}
