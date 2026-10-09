import { useEffect, useState, type DragEvent, type ReactNode } from "react";
import { LIBRARY, LIBRARY_GROUPS, LIBRARY_MIME, type LibraryItem } from "../library";

const FAV_KEY = "macroui.lib.fav";
const RECENT_KEY = "macroui.lib.recent";

function readList(key: string): string[] {
  try {
    const parsed = JSON.parse(localStorage.getItem(key) ?? "[]");
    return Array.isArray(parsed) ? parsed.filter((id) => typeof id === "string") : [];
  } catch {
    return [];
  }
}

export function Library({
  onPick,
  hasRecording = false,
}: {
  onPick: (id: string) => void;
  hasRecording?: boolean;
}) {
  const [find, setFind] = useState("");
  const [shut, setShut] = useState<Partial<Record<string, boolean>>>({});
  const [fav, setFav] = useState<string[]>(() => readList(FAV_KEY));
  const [recent, setRecent] = useState<string[]>(() => readList(RECENT_KEY));
  const term = find.trim().toLowerCase();
  const hit = (label: string) => !term || label.toLowerCase().includes(term);

  useEffect(() => {
    localStorage.setItem(FAV_KEY, JSON.stringify(fav));
  }, [fav]);

  useEffect(() => {
    localStorage.setItem(RECENT_KEY, JSON.stringify(recent));
  }, [recent]);

  const catalog = LIBRARY.filter((item) => LIBRARY_GROUPS.includes(item.group));
  const byId = new Map(catalog.map((item) => [item.id, item]));
  const visible = (id: string) => {
    const item = byId.get(id);
    return item && hit(item.label) ? item : null;
  };
  const groups = LIBRARY_GROUPS.map((group) => ({
    id: group,
    title: group,
    items: LIBRARY.filter((item) => item.group === group && hit(item.label)),
  })).filter((entry) => entry.items.length);

  const sections: { id: string; title: string; items: LibraryItem[] }[] = [];
  const favorites = fav.map(visible).filter((item): item is LibraryItem => !!item);
  const recentItems = recent.map(visible).filter((item): item is LibraryItem => !!item);
  if (favorites.length) sections.push({ id: "favorites", title: "Favorites", items: favorites });
  if (recentItems.length) sections.push({ id: "recent", title: "Recent", items: recentItems });
  sections.push(...groups);

  const remember = (id: string) => {
    setRecent((current) => [id, ...current.filter((item) => item !== id)].slice(0, 4));
  };

  const toggleFav = (id: string) => {
    setFav((current) => (current.includes(id) ? current.filter((item) => item !== id) : [id, ...current]));
  };

  const start = (event: DragEvent, id: string) => {
    event.dataTransfer.setData(LIBRARY_MIME, id);
    event.dataTransfer.setData("text/plain", id);
    event.dataTransfer.effectAllowed = "copy";
    remember(id);
  };

  return (
    <aside className="lib" aria-label="Library">
      <div className="lib-head">
        <label className="lib-find-wrap">
          <svg className="lib-find-icon" viewBox="0 0 16 16" aria-hidden="true">
            <circle cx="7" cy="7" r="4.25" />
            <path d="M10.2 10.2 L13.2 13.2" />
          </svg>
          <input
            className="lib-find"
            type="search"
            aria-label="Search the library"
            placeholder="Search blocks..."
            spellCheck={false}
            value={find}
            onChange={(event) => setFind(event.target.value)}
          />
        </label>
      </div>
      <div className="lib-body">
        {sections.map(({ id, title, items }) => {
          const open = !shut[id] || !!term;
          return (
            <section key={id} className={`lib-group${open ? " is-open" : ""}`}>
              <button
                type="button"
                className="lib-group-head"
                aria-expanded={open}
                onClick={() => setShut((current) => ({ ...current, [id]: !current[id] }))}
              >
                <span>{title}</span>
                <i aria-hidden="true" />
              </button>
              {open ? (
                <div className="lib-list">
                  {items.map((item) => {
                    const off = item.kind === "record" && item.source === "last" && !hasRecording;
                    const starred = fav.includes(item.id);
                    return (
                      <div key={`${id}-${item.id}`} className={`lib-row${off ? " is-off" : ""}`}>
                        <button
                          type="button"
                          className="lib-item"
                          title={item.detail}
                          disabled={off}
                          draggable={!off}
                          onDragStart={(event) => start(event, item.id)}
                          onClick={() => {
                            remember(item.id);
                            onPick(item.id);
                          }}
                        >
                          <LibMark id={item.id} />
                          <span>{item.label}</span>
                        </button>
                        <button
                          type="button"
                          className={`lib-star${starred ? " is-on" : ""}`}
                          aria-label={starred ? `Remove ${item.label} from favorites` : `Favorite ${item.label}`}
                          aria-pressed={starred}
                          onMouseDown={(event) => event.stopPropagation()}
                          onClick={() => toggleFav(item.id)}
                        >
                          <svg viewBox="0 0 16 16" aria-hidden="true">
                            <path d="M8 2.2 9.5 5.7l3.8.4-2.8 2.5.8 3.7L8 10.6 4.7 12.3l.8-3.7L2.7 6.1l3.8-.4L8 2.2z" />
                          </svg>
                        </button>
                      </div>
                    );
                  })}
                </div>
              ) : null}
            </section>
          );
        })}
        {sections.length ? null : <p className="lib-empty">No match</p>}
      </div>
    </aside>
  );
}

function LibMark({ id }: { id: string }) {
  return (
    <svg className="lib-mark" viewBox="0 0 16 16" aria-hidden="true">
      {mark(id)}
    </svg>
  );
}

function mark(id: string): ReactNode {
  if (id === "whileHeld") return <path d="M4.6 8c0-1.5 1-2.5 2.2-2.5 1.4 0 1.9 1.4 2.6 2.5S10.8 10.5 12.2 10.5c1.2 0 2.2-1 2.2-2.5S13.4 5.5 12.2 5.5c-1.4 0-1.9 1.4-2.6 2.5S8.2 10.5 6.8 10.5C5.6 10.5 4.6 9.5 4.6 8z" />;
  if (id === "ifReleasedEarly") return <path d="M3.2 3.2v9.6M3.2 8h4.2M7.4 8c2.2-2.4 3.2-3.6 5.4-3.6M7.4 8c2.2 2.4 3.2 3.6 5.4 3.6" />;
  if (id === "swapAfter") return <path d="M3.2 5.2h8.2M9.2 3.2l2.4 2-2.4 2M12.8 10.8H4.6M6.8 8.8l-2.4 2 2.4 2" />;
  if (id === "afterRelease") return <path d="M3.4 4.2h6.2a3 3 0 0 1 0 6H6.2M6.2 8.2 4.2 10.2 6.2 12.2" />;
  if (id === "repeatBlock" || id === "repeatSteps") return <path d="M12.6 6.2A4.6 4.6 0 0 0 4 5.2M3.4 3.2v2.4h2.4M3.4 9.8A4.6 4.6 0 0 0 12 10.8M12.6 12.8V10.4H10.2" />;
  if (id === "waitBlock" || id === "wait") return <><circle cx="8" cy="8" r="4.6" /><path d="M8 5.4V8l1.8 1.4" /></>;
  if (id === "scanWait") return <><circle cx="8" cy="8" r="4.6" /><path d="M8 8l2.4-1.6M8 3.6V5" /></>;
  if (id === "runOnce") return <path d="M6 4.4v7.2l5.2-3.6L6 4.4z" />;
  if (id === "runMacro") return <><rect x="3" y="3.2" width="10" height="9.6" rx="1.4" /><path d="M7 6.2v3.6l2.8-1.8L7 6.2z" /></>;
  if (id === "repressKey") return <path d="M3.2 6.2h3.2v3.6H3.2zM8 6.2h4.8M8 8h3.2M8 9.8h4.8" />;
  if (id === "repressSpam") return <path d="M3 6.2h2.1v3.6H3zM6.1 6.2h2.1v3.6H6.1zM9.2 7.1h3.6M9.2 8.9h2.4" />;
  if (id === "lastRecording" || id === "recordSteps") return <><circle cx="8" cy="8" r="4.6" /><circle cx="8" cy="8" r="1.6" /></>;
  return <circle cx="8" cy="8" r="2.2" />;
}
