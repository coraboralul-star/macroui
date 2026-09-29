import { newId, type Block } from "./profile";

export function createBlock(type: Block["type"]): Block {
  const id = newId();
  if (type === "whileHeld") return { id, type, steps: [], mute: [] };
  if (type === "steps") return { id, type, steps: [] };
  if (type === "ifShort") return { id, type, underMs: 150, minCycles: 3, steps: [] };
  if (type === "then") return { id, type, forMs: 55, steps: [] };
  if (type === "repeat") return { id, type, count: 1, steps: [] };
  if (type === "tapHold") return { id, type, key: "z", watch: ["LShift", "CapsLock", "t", "XButton2", "p"], armMs: 5, gapMs: 80 };
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

export function blockTitle(type: Block["type"]): string {
  if (type === "whileHeld") return "While held";
  if (type === "ifShort") return "If released before";
  if (type === "then") return "After release";
  if (type === "repeat") return "Repeat";
  if (type === "wait") return "Wait";
  if (type === "tapHold") return "Repress";
  return "Do once";
}

export function blockPick(type: Block["type"]): { name: string; blurb: string } {
  if (type === "whileHeld") return { name: "While held", blurb: "Loops while down" };
  if (type === "ifShort") return { name: "Quick tap", blurb: "Let go early" };
  if (type === "then") return { name: "On release", blurb: "After let go" };
  if (type === "repeat") return { name: "Repeat", blurb: "N times" };
  if (type === "wait") return { name: "Wait", blurb: "Delay" };
  if (type === "tapHold") return { name: "Repress", blurb: "Tap again" };
  return { name: "Once", blurb: "Run once" };
}

export function insertTyped(blocks: Block[], index: number, type: Block["type"]): { blocks: Block[]; pick: string } {
  const next = blocks.slice();
  const at = Math.max(0, Math.min(index, next.length));
  if (type === "whileHeld") {
    const hold = createBlock("whileHeld");
    next.splice(at, 0, hold);
    if (next[at + 1]?.type !== "ifShort") next.splice(at + 1, 0, createBlock("ifShort"));
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
  const item = createBlock(type);
  next.splice(at, 0, item);
  return { blocks: next, pick: item.id };
}
