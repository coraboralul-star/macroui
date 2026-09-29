import type { MouseEvent } from "react";
import type { Macro, TriggerKind } from "../profile";

const SIDES: { kind: TriggerKind; button: string; label: string }[] = [
  { kind: "side", button: "XButton2", label: "M5" },
  { kind: "side", button: "XButton1", label: "M4" },
];

export function MousePad({
  macros,
  picked,
  swapped,
  onAssign,
  onSwap,
}: {
  macros: Macro[];
  picked: string | null;
  swapped: boolean;
  onAssign: (kind: TriggerKind, button: string, x: number, y: number) => void;
  onSwap: () => void;
}) {
  const bound = (kind: TriggerKind, button: string) =>
    macros.some((macro) => macro.trigger.kind === kind && macro.trigger.button === button);

  const open = (kind: TriggerKind, button: string, event: MouseEvent<HTMLButtonElement>) => {
    const rect = event.currentTarget.getBoundingClientRect();
    onAssign(kind, button, rect.left, rect.bottom + 6);
  };

  return (
    <div className="board mouse-pad" aria-label="Mouse">
      <div className="mouse-side is-dock">
        {SIDES.map((side) => (
          <button
            key={side.button}
            type="button"
            className={`keycap mouse-thumb${bound(side.kind, side.button) ? " is-bound" : ""}${picked === side.button ? " is-picked" : ""}`}
            onClick={(event) => open(side.kind, side.button, event)}
            onContextMenu={(event) => event.preventDefault()}
          >
            {side.label}
          </button>
        ))}
      </div>
      <div className="mouse-shell">
        <div className="mouse-clicks">
          <button type="button" className={`mouse-face${swapped ? " is-swapped" : ""}`} title="Swap" onClick={onSwap}>
            {swapped ? "Right" : "Left"}
          </button>
          <button type="button" className={`mouse-face${swapped ? " is-swapped" : ""}`} title="Swap" onClick={onSwap}>
            {swapped ? "Left" : "Right"}
          </button>
        </div>
        <button
          type="button"
          className={`keycap mouse-wheel${bound("mouse", "MButton") ? " is-bound" : ""}${picked === "MButton" ? " is-picked" : ""}`}
          onClick={(event) => open("mouse", "MButton", event)}
          onContextMenu={(event) => event.preventDefault()}
        >
          Middle
        </button>
      </div>
    </div>
  );
}
