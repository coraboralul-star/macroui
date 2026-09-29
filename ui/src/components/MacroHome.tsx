import type { Macro } from "../profile";
import { MOUSE_OPTIONS, SIDE_OPTIONS } from "../profile";

export function MacroHome({
  macro,
  running,
  onEdit,
  onDelete,
}: {
  macro: Macro;
  running: boolean;
  onEdit: () => void;
  onDelete: () => void;
}) {
  return (
    <section className="home">
      <div className="home-card">
        <p className="home-kicker">Macro</p>
        <h2>{macro.name}</h2>
        <p className={`run-pill${running ? " is-on" : ""}`}>{running ? "Running" : "Idle"}</p>
        <dl className="home-facts">
          <div>
            <dt>Trigger</dt>
            <dd>{triggerLabel(macro)}</dd>
          </div>
          <div>
            <dt>Play</dt>
            <dd>{playLabel(macro.playMode)}</dd>
          </div>
          <div>
            <dt>Steps</dt>
            <dd>{macro.steps.length}</dd>
          </div>
        </dl>
        <div className="home-actions">
          <button type="button" className="record" onClick={onEdit}>
            Edit macro
          </button>
          <button type="button" className="ghost is-danger" onClick={onDelete}>
            Delete
          </button>
        </div>
      </div>
    </section>
  );
}

function triggerLabel(macro: Macro) {
  const button = macro.trigger.button;
  if (macro.trigger.kind === "mouse") return MOUSE_OPTIONS.find((item) => item.value === button)?.label ?? button;
  if (macro.trigger.kind === "side") return SIDE_OPTIONS.find((item) => item.value === button)?.label ?? button;
  return button;
}

function playLabel(mode: Macro["playMode"]) {
  if (mode === "whileHeld") return "While held";
  if (mode === "onRelease") return "On release";
  if (mode === "repeat") return "Repeat";
  if (mode === "toggle") return "Toggle";
  return "Once";
}
