import { useState } from "react";
import { place } from "../macroFlow";
import type { Macro } from "../profile";
import { Confirm } from "./Confirm";
import { MacroBrief } from "./MacroBrief";
import { MacroEditor } from "./MacroEditor";

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
  const macro = macros.find((item) => item.id === selected) ?? null;
  const [wipe, setWipe] = useState(false);

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
                onClick={() => onSelect(item.id)}
              >
                <span className={`dot${running.has(item.id) ? " is-on" : ""}`} />
                <span className="studio-name">{item.name}</span>
                <span className={`studio-place${place(item) ? " is-key" : ""}`}>{place(item) || (item.advanced ? "Advanced" : "Unassigned")}</span>
              </button>
            ))}
          </div>
          <div className="studio-adds">
            <button type="button" className="studio-new is-primary" onClick={onAdd}>
              New
            </button>
          </div>
        </aside>
        <div className="studio-canvas">
          {macro?.advanced ? (
            <MacroBrief
              key={macro.id}
              macro={macro}
              onChange={onChange}
              onDelete={() => setWipe(true)}
              onOpenGraph={() => onOpenGraph(macro.id)}
            />
          ) : macro ? (
            <div className="brief-wrap">
              <div className="brief-head is-simple">
                <button type="button" className="studio-new is-primary" onClick={() => onOpenGraph(macro.id)}>
                  Open advanced
                </button>
              </div>
              <MacroEditor
                key={macro.id}
                macro={macro}
                macros={macros}
                running={running.has(macro.id)}
                onChange={onChange}
                onDelete={() => setWipe(true)}
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
            onDelete();
            setWipe(false);
          }}
        />
      ) : null}
    </div>
  );
}
