import type { Macro } from "../profile";
import { RecordDeck } from "./RecordDeck";

export function MacroEditor({
  macro,
  macros,
  running,
  onChange,
  onDelete,
  onOpenGraph,
  onSave,
  saveDisabled,
  saveTitle,
  issue,
}: {
  macro: Macro;
  macros: Macro[];
  running: boolean;
  onChange: (macro: Macro) => void;
  onDelete: () => void;
  onOpenGraph: () => void;
  onSave: () => void;
  saveDisabled: boolean;
  saveTitle: string;
  issue: string;
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
        <button type="button" className="studio-new is-primary" disabled={saveDisabled} title={saveTitle} onClick={onSave}>
          Save
        </button>
        <button type="button" className="studio-new is-primary" onClick={onOpenGraph}>
          Advanced
        </button>
      </div>
      {issue ? <p className="draft-note is-bad">{issue}</p> : null}
      <RecordDeck
        macro={macro}
        macros={macros.filter((item) => item.id !== macro.id)}
        onChange={onChange}
      />
    </section>
  );
}
