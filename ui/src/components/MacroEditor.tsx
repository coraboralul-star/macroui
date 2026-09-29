import type { Macro } from "../profile";
import { RecordDeck } from "./RecordDeck";

export function MacroEditor({
  macro,
  macros,
  running,
  onChange,
  onDelete,
}: {
  macro: Macro;
  macros: Macro[];
  running: boolean;
  onChange: (macro: Macro) => void;
  onDelete: () => void;
}) {
  return (
    <section className="editor timeline">
      <div className="editor-head">
        <input
          className="name"
          aria-label="Macro name"
          value={macro.name}
          spellCheck={false}
          onChange={(e) => onChange({ ...macro, name: e.target.value })}
        />
        <span className={`run-pill${running ? " is-on" : ""}`}>{running ? "Running" : "Idle"}</span>
        <button type="button" className="ghost is-danger" onClick={onDelete}>
          Delete
        </button>
      </div>
      <RecordDeck
        macro={macro}
        macros={macros.filter((item) => item.id !== macro.id)}
        onChange={onChange}
      />
    </section>
  );
}
