import { useState } from "react";
import { place } from "../macroFlow";
import { macroIssues } from "../profileEdits";
import type { Macro } from "../profile";
import { Confirm } from "./Confirm";
import { MacroBrief } from "./MacroBrief";
import { MacroEditor } from "./MacroEditor";

/** Survives leaving the editor so an unsaved draft is still here when you come back. */
let studioDraft: Macro | null = null;

export function MacroStudio({
  macros,
  selected,
  running,
  onSelect,
  onAdd,
  onChange,
  onDelete,
  onOpenGraph,
}: {
  macros: Macro[];
  selected: string;
  running: Set<string>;
  onSelect: (id: string) => void;
  onAdd: () => void;
  onChange: (macro: Macro) => void;
  onDelete: () => void;
  onOpenGraph: (id: string) => void;
}) {
  const committed = macros.find((item) => item.id === selected) ?? null;
  const [draft, setDraft] = useState<Macro | null>(studioDraft && studioDraft.id === selected ? studioDraft : null);
  const [wipe, setWipe] = useState(false);
  const [pending, setPending] = useState<string | null>(null);
  const macro = draft && draft.id === committed?.id ? draft : committed;
  const issues = macro ? macroIssues(macro) : [];
  const dirty = !!draft && !!committed && draft.id === committed.id && JSON.stringify(draft) !== JSON.stringify(committed);
  const issue = dirty ? issues[0] ?? "" : "";

  const edit = (next: Macro) => {
    studioDraft = next;
    setDraft(next);
  };

  const clearDraft = () => {
    studioDraft = null;
    setDraft(null);
  };

  const save = () => {
    if (!macro || !dirty || issues.length) return;
    onChange(macro);
    clearDraft();
  };

  const openGraph = () => {
    if (!macro) return;
    if (dirty && issues.length) return;
    if (dirty) {
      onChange(macro);
      clearDraft();
    }
    onOpenGraph(macro.id);
  };

  const selectRow = (id: string) => {
    if (id === selected) return;
    if (dirty) setPending(id);
    else onSelect(id);
  };

  const addRow = () => {
    if (dirty) setPending("add");
    else onAdd();
  };

  return (
    <div className="studio is-page" aria-label="Macro Editor">
      <div className="studio-body">
        <aside className="studio-list">
          <div className="studio-scroll">
            {macros.map((item) => (
              <button
                key={item.id}
                type="button"
                className={`studio-row${item.id === selected ? " is-selected" : ""}`}
                onClick={() => selectRow(item.id)}
              >
                <span className={`dot${running.has(item.id) ? " is-on" : ""}`} />
                <span className="studio-name">{item.name}</span>
                <span className={`studio-place${place(item) ? " is-key" : ""}`}>{place(item) || (item.advanced ? "Advanced" : "Unassigned")}</span>
              </button>
            ))}
          </div>
          <div className="studio-adds">
            <button type="button" className="studio-new is-primary" onClick={addRow}>
              New
            </button>
          </div>
        </aside>
        <div className="studio-canvas">
          {macro?.advanced ? (
            <MacroBrief
              key={macro.id}
              macro={macro}
              macros={macros.filter((item) => item.id !== macro.id)}
              onChange={edit}
              onDelete={() => setWipe(true)}
              onOpenGraph={openGraph}
              onSave={save}
              saveDisabled={!dirty || issues.length > 0}
              saveTitle={issue || (dirty ? "Save this macro" : "No changes to save")}
              issue={issue}
            />
          ) : macro ? (
            <div className="brief-wrap">
              <MacroEditor
                key={macro.id}
                macro={macro}
                macros={macros}
                running={running.has(macro.id)}
                onChange={edit}
                onDelete={() => setWipe(true)}
                onOpenGraph={openGraph}
                onSave={save}
                saveDisabled={!dirty || issues.length > 0}
                saveTitle={issue || (dirty ? "Save this macro" : "No changes to save")}
                issue={issue}
              />
            </div>
          ) : (
            <div />
          )}
        </div>
      </div>
      {wipe && macro ? (
        <Confirm
          title={`Delete ${macro.name}?`}
          onCancel={() => setWipe(false)}
          onConfirm={() => {
            clearDraft();
            onDelete();
            setWipe(false);
          }}
        />
      ) : null}
      {pending ? (
        <Confirm
          title="Discard unsaved changes?"
          note="The saved macro stays as it is."
          action="Discard"
          onCancel={() => setPending(null)}
          onConfirm={() => {
            const next = pending;
            clearDraft();
            setPending(null);
            if (next === "add") onAdd();
            else onSelect(next);
          }}
        />
      ) : null}
    </div>
  );
}
