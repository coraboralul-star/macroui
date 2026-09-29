import { useState, type DragEvent } from "react";
import { LIBRARY, LIBRARY_MIME } from "../library";

export function Library() {
  const groups = [...new Set(LIBRARY.map((item) => item.group))];
  const [closed, setClosed] = useState<Record<string, boolean>>({});
  const start = (event: DragEvent, id: string) => {
    event.dataTransfer.setData(LIBRARY_MIME, id);
    event.dataTransfer.setData("text/plain", id);
    event.dataTransfer.effectAllowed = "copy";
  };
  return (
    <aside className="fn-lib" data-chrome="" aria-label="Functions">
      <p className="deck-kicker">Functions</p>
      {groups.map((group) => (
        <section key={group} className="fn-group">
          <button type="button" aria-expanded={!closed[group]} onClick={() => setClosed((current) => ({ ...current, [group]: !current[group] }))}>
            {group}
          </button>
          {closed[group] ? null : (
            <div className="fn-list">
              {LIBRARY.filter((item) => item.group === group).map((item) => (
                <div key={item.id} className="fn-item" draggable onDragStart={(event) => start(event, item.id)}>
                  <strong>{item.label}</strong>
                  <small>{item.detail}</small>
                </div>
              ))}
            </div>
          )}
        </section>
      ))}
    </aside>
  );
}
