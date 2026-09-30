import type { Step } from "./profile";
import { blankStep } from "./profile";

export const LIBRARY_MIME = "application/x-macro-fn";

export type LibraryItem = {
  id: string;
  group: string;
  label: string;
  detail: string;
  create: () => Step;
};

/** Add an entry to show a new function in Advanced. */
export const LIBRARY: LibraryItem[] = [
  { id: "key", group: "Input", label: "Key", detail: "Press and release", create: () => blankStep("key") },
  { id: "down", group: "Input", label: "Key down", detail: "Hold until an up", create: () => ({ type: "key", action: "down", key: "w" }) },
  { id: "up", group: "Input", label: "Key up", detail: "Release a held key", create: () => ({ type: "key", action: "up", key: "w" }) },
  { id: "click", group: "Input", label: "Click", detail: "Mouse button", create: () => blankStep("mouse") },
  { id: "move", group: "Input", label: "Move", detail: "Cursor offset", create: () => blankStep("move") },
  { id: "goto", group: "Input", label: "Go to", detail: "Cursor to a Screen, Window, or Client point", create: () => blankStep("goto") },
  { id: "wait", group: "Timing", label: "Wait", detail: "Pause between actions", create: () => blankStep("wait") },
  { id: "repeat", group: "Flow", label: "Repeat", detail: "Run the steps inside again", create: () => blankStep("repeat") },
  { id: "run", group: "Flow", label: "Run macro", detail: "Start another macro", create: () => blankStep("run") },
];

export function createLibraryStep(id: string): Step | null {
  const item = LIBRARY.find((entry) => entry.id === id);
  return item ? item.create() : null;
}
