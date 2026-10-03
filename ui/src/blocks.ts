import { basicSteps, MIN_REPEAT_MS, mouseSteps, newId, type Block, type Macro, type PlayMode, type Step } from "./profile";

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
  const item = createBlock(type);
  next.splice(at, 0, item);
  return { blocks: next, pick: item.id };
}

const BRANCH_TYPES = new Set<Block["type"]>(["ifShort", "swapAfter", "then"]);

export function isBranchBlock(block: Block): boolean {
  return BRANCH_TYPES.has(block.type);
}

/** Drop a grabbed wire on a node: that node becomes the next block after the source. */
export function moveBlockAfter(blocks: Block[], id: string, afterId: string): Block[] {
  if (!id || id === afterId) return blocks;
  const moving = blocks.find((block) => block.id === id);
  if (!moving) return blocks;
  const rest = blocks.filter((block) => block.id !== id);
  const at = rest.findIndex((block) => block.id === afterId);
  if (at < 0) return blocks;
  rest.splice(at + 1, 0, moving);
  return rest;
}

/** Add another condition beside this block. It does not insert a companion. */
export function splitFrom(blocks: Block[], id: string): { blocks: Block[]; pick: string } {
  const index = blocks.findIndex((block) => block.id === id);
  if (index < 0) return { blocks, pick: id };
  let trunk = index;
  while (trunk > 0 && isBranchBlock(blocks[trunk])) trunk -= 1;
  let at = trunk + 1;
  while (at < blocks.length && isBranchBlock(blocks[at])) at += 1;
  return insertTyped(blocks, at, "ifShort");
}

function triggerSteps(macro: Macro): Step[] {
  const button = macro.trigger.button;
  if (!button) return [];
  const ms = macro.gapMs ?? MIN_REPEAT_MS;
  return macro.trigger.kind === "key" ? basicSteps(button, ms) : mouseSteps(button, ms);
}

/** Playback choices install the block that actually performs them. Toggle and On release stay playback modes. */
export function applyPlayback(macro: Macro, mode: PlayMode): Macro {
  if (mode === "whileHeld") return withPlaybackBlock(macro, "whileHeld", mode);
  if (mode === "repeat") return withPlaybackBlock(macro, "repeat", mode);
  if (mode === "once") return withPlaybackBlock(macro, "steps", mode);
  return { ...macro, playMode: mode };
}

function withPlaybackBlock(macro: Macro, type: "whileHeld" | "repeat" | "steps", mode: PlayMode): Macro {
  const blocks = macro.blocks ?? [];
  if (blocks.some((block) => block.type === type))
    return { ...macro, playMode: mode, advanced: true, basic: false };
  const steps = macro.steps.length ? macro.steps : triggerSteps(macro);
  const created = createBlock(type);
  const block: Block =
    created.type === "repeat"
      ? { ...created, steps, count: Math.max(1, macro.repeatCount || 1) }
      : created.type === "whileHeld" || created.type === "steps"
        ? { ...created, steps }
        : created;
  return {
    ...macro,
    playMode: mode,
    advanced: true,
    basic: false,
    blocks: [block, ...blocks],
    steps: [],
    releaseStop: macro.releaseStop === "finish" ? "finish" : "nextUp",
  };
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
