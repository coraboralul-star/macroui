import { pressPair } from "./eventLane";
import type { Block, Step } from "./profile";
import { blankStep } from "./profile";

export const LIBRARY_MIME = "application/x-macro-fn";

export type LibraryGroup = "Keyboard" | "Mouse" | "Timing" | "Repeat & Run" | "Blocks" | "Recording";

export const LIBRARY_GROUPS: LibraryGroup[] = ["Keyboard", "Mouse", "Timing", "Repeat & Run", "Blocks", "Recording"];

/** A + between blocks is asking for a block first, so that group leads there. */
export const MENU_GROUPS: LibraryGroup[] = ["Blocks", "Keyboard", "Mouse", "Timing", "Repeat & Run", "Recording"];

type Common = {
  id: string;
  group: LibraryGroup;
  label: string;
  detail: string;
};

/** Steps go inside a block. Blocks go on the canvas. Recording pulls from the recorder. */
export type LibraryItem =
  | (Common & { kind: "step"; pick?: "key" | "mouse"; create: (button?: string) => Step[] })
  | (Common & { kind: "block"; block: Block["type"] })
  | (Common & { kind: "record"; source: "last" | "live" });

/** Add an entry here and it shows up in the Library panel and in every insert menu. */
export const LIBRARY: LibraryItem[] = [
  { id: "keyPress", group: "Keyboard", label: "Key Press", detail: "Down then up", kind: "step", pick: "key", create: (button = "space") => pressPair("key", button) },
  { id: "keyDown", group: "Keyboard", label: "Key Down", detail: "Hold a key", kind: "step", pick: "key", create: (button = "space") => [{ type: "key", action: "down", key: button }] },
  { id: "keyUp", group: "Keyboard", label: "Key Up", detail: "Release a key", kind: "step", pick: "key", create: (button = "space") => [{ type: "key", action: "up", key: button }] },
  { id: "mouseClick", group: "Mouse", label: "Mouse Click", detail: "Down then up", kind: "step", pick: "mouse", create: (button = "LButton") => pressPair("mouse", button) },
  { id: "mouseDown", group: "Mouse", label: "Mouse Down", detail: "Hold a button", kind: "step", pick: "mouse", create: (button = "LButton") => [{ type: "mouse", action: "down", button }] },
  { id: "mouseUp", group: "Mouse", label: "Mouse Up", detail: "Release a button", kind: "step", pick: "mouse", create: (button = "LButton") => [{ type: "mouse", action: "up", button }] },
  { id: "mouseMove", group: "Mouse", label: "Mouse Move", detail: "Move by X and Y", kind: "step", create: () => [blankStep("move")] },
  { id: "mousePosition", group: "Mouse", label: "Mouse Position", detail: "Move to a point", kind: "step", create: () => [blankStep("goto")] },
  { id: "wait", group: "Timing", label: "Wait", detail: "Pause in ms", kind: "step", create: () => [blankStep("wait")] },
  { id: "scanWait", group: "Timing", label: "Scan Wait", detail: "Pause in ms. Stops if you let go of the trigger", kind: "step", create: () => [blankStep("scanWait")] },
  { id: "repeatSteps", group: "Repeat & Run", label: "Repeat Steps", detail: "Loop the steps inside", kind: "step", create: () => [blankStep("repeat")] },
  { id: "runMacro", group: "Repeat & Run", label: "Run Macro", detail: "Start another macro", kind: "step", create: () => [blankStep("run")] },
  { id: "whileHeld", group: "Blocks", label: "While Held", detail: "Loops while the trigger is down", kind: "block", block: "whileHeld" },
  { id: "ifReleasedEarly", group: "Blocks", label: "If Released Early", detail: "Short tap path", kind: "block", block: "ifShort" },
  { id: "afterRelease", group: "Blocks", label: "After Release", detail: "Runs once you let go", kind: "block", block: "then" },
  { id: "repeatBlock", group: "Blocks", label: "Repeat Block", detail: "Runs a set number of times", kind: "block", block: "repeat" },
  { id: "waitBlock", group: "Blocks", label: "Wait Block", detail: "Pause before the next block", kind: "block", block: "wait" },
  { id: "runOnce", group: "Blocks", label: "Run Once", detail: "Runs one time", kind: "block", block: "steps" },
  { id: "repressKey", group: "Blocks", label: "Repress Key", detail: "Taps the trigger again. Tracked macros can keep running or stay blocked", kind: "block", block: "tapHold" },
  { id: "lastRecording", group: "Recording", label: "Last Recording", detail: "Add the steps you recorded", kind: "record", source: "last" },
  { id: "recordSteps", group: "Recording", label: "Record Steps", detail: "Capture keys into this block", kind: "record", source: "live" },
];

/** One-click presses. Each chip drops a down, a short hold, and an up. */
export const QUICK_KEYS: { id: string; group: LibraryGroup; label: string }[] = [
  { id: "press:key:w", group: "Keyboard", label: "W" },
  { id: "press:key:a", group: "Keyboard", label: "A" },
  { id: "press:key:s", group: "Keyboard", label: "S" },
  { id: "press:key:d", group: "Keyboard", label: "D" },
  { id: "press:key:space", group: "Keyboard", label: "Space" },
  { id: "press:key:LShift", group: "Keyboard", label: "LShift" },
  { id: "press:key:RShift", group: "Keyboard", label: "RShift" },
  { id: "press:key:LCtrl", group: "Keyboard", label: "LCtrl" },
  { id: "press:key:RCtrl", group: "Keyboard", label: "RCtrl" },
  { id: "press:key:LAlt", group: "Keyboard", label: "LAlt" },
  { id: "press:key:RAlt", group: "Keyboard", label: "RAlt" },
  { id: "press:mouse:LButton", group: "Mouse", label: "Left" },
  { id: "press:mouse:RButton", group: "Mouse", label: "Right" },
  { id: "press:mouse:MButton", group: "Mouse", label: "Middle" },
];

const PRESS = /^press:(key|mouse):(.+)$/;

/** Resolves catalog ids and the `press:kind:button` ids the quick chips drag. */
export function libraryItem(id: string): LibraryItem | null {
  const found = LIBRARY.find((item) => item.id === id);
  if (found) return found;
  const press = PRESS.exec(id);
  if (!press) return null;
  const kind = press[1] === "mouse" ? "mouse" : "key";
  const button = press[2];
  const label = QUICK_KEYS.find((chip) => chip.id === id)?.label ?? button;
  return {
    id,
    group: kind === "mouse" ? "Mouse" : "Keyboard",
    label,
    detail: "Down then up",
    kind: "step",
    create: () => pressPair(kind, button),
  };
}

const SWAP: Record<string, string> = {
  keyPress: "mouseClick",
  keyDown: "mouseDown",
  keyUp: "mouseUp",
  mouseClick: "keyPress",
  mouseDown: "keyDown",
  mouseUp: "keyUp",
};

/** Picking a mouse button for a Key Press should still make a mouse step. */
export function matchInput(id: string, kind: "key" | "mouse"): string {
  const item = libraryItem(id);
  if (!item || item.kind !== "step" || !item.pick || item.pick === kind) return id;
  return SWAP[id] ?? id;
}

/** Steps for any step entry. Blocks and recording entries have none. */
export function librarySteps(id: string, button?: string): Step[] {
  const item = libraryItem(id);
  return item && item.kind === "step" ? item.create(button) : [];
}

export function createLibraryStep(id: string): Step | null {
  return librarySteps(id)[0] ?? null;
}
