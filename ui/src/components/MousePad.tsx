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
  onSwap: (next: boolean) => void;
}) {
  const bound = (kind: TriggerKind, button: string) =>
    macros.some((macro) => macro.trigger.kind === kind && macro.trigger.button === button);

  const open = (kind: TriggerKind, button: string, event: MouseEvent<HTMLButtonElement>) => {
    const rect = event.currentTarget.getBoundingClientRect();
    onAssign(kind, button, rect.left, rect.bottom + 6);
  };

  return (
    <div className="mouse-page">
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
            <div className={`mouse-face${swapped ? " is-swapped" : ""}`}>
              {swapped ? "Right" : "Left"}
            </div>
            <div className={`mouse-face${swapped ? " is-swapped" : ""}`}>
              {swapped ? "Left" : "Right"}
            </div>
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
      <label className="settings-row mouse-swap">
        <span>
          <span className="settings-row-name">Swap clicks</span>
          <span className="settings-row-note">Left and right click trade places in playback.</span>
        </span>
          <input type="checkbox" checked={swapped} onChange={(event) => onSwap(event.target.checked)} />
      </label>
    </div>
  );
}
