import { useState, type DragEvent } from "react";
import { LIBRARY, LIBRARY_GROUPS, LIBRARY_MIME, QUICK_KEYS, type LibraryGroup } from "../library";

export function Library({
  onPick,
  hasRecording = false,
}: {
  onPick: (id: string) => void;
  hasRecording?: boolean;
}) {
  const [find, setFind] = useState("");
  const [shut, setShut] = useState<Partial<Record<LibraryGroup, boolean>>>({});
  const term = find.trim().toLowerCase();
  const hit = (label: string) => !term || label.toLowerCase().includes(term);
  const chips = QUICK_KEYS.filter((chip) => hit(chip.label));
  const groups = LIBRARY_GROUPS.map((group) => ({
    group,
    items: LIBRARY.filter((item) => item.group === group && hit(item.label)),
  })).filter((entry) => entry.items.length);

  const start = (event: DragEvent, id: string) => {
    event.dataTransfer.setData(LIBRARY_MIME, id);
    event.dataTransfer.setData("text/plain", id);
    event.dataTransfer.effectAllowed = "copy";
  };

  return (
    <aside className="lib" aria-label="Library">
      <div className="lib-head">
        <input
          className="lib-find"
          type="search"
          aria-label="Search the library"
          placeholder="Search"
          spellCheck={false}
          value={find}
          onChange={(event) => setFind(event.target.value)}
        />
      </div>
      <div className="lib-body">
        {chips.length ? (
          <div className="lib-chips lib-quick">
            {chips.map((chip) => (
              <button
                key={chip.id}
                type="button"
                className="lib-chip"
                title={`${chip.label} down then up`}
                draggable
                onDragStart={(event) => start(event, chip.id)}
                onClick={() => onPick(chip.id)}
              >
                {chip.label}
              </button>
            ))}
          </div>
        ) : null}
        {groups.map(({ group, items }) => {
          const open = !shut[group] || !!term;
          return (
            <section key={group} className={`lib-group${open ? " is-open" : ""}`}>
              <button
                type="button"
                className="lib-group-head"
                aria-expanded={open}
                onClick={() => setShut((current) => ({ ...current, [group]: !current[group] }))}
              >
                <i aria-hidden="true" />
                {group}
              </button>
              {open ? (
                <div className="lib-list">
                  {items.map((item) => {
                    const off = item.kind === "record" && item.source === "last" && !hasRecording;
                    return (
                      <button
                        key={item.id}
                        type="button"
                        className={`lib-item is-${item.kind}`}
                        title={item.detail}
                        disabled={off}
                        draggable={!off}
                        onDragStart={(event) => start(event, item.id)}
                        onClick={() => onPick(item.id)}
                      >
                        {item.label}
                      </button>
                    );
                  })}
                </div>
              ) : null}
            </section>
          );
        })}
        {groups.length || chips.length ? null : <p className="lib-empty">No match</p>}
      </div>
    </aside>
  );
}
