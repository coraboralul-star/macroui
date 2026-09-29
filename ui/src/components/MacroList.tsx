import { useEffect, useRef, useState } from "react";
import type { Config } from "../profile";
import { Confirm } from "./Confirm";

const ROW = 58;

export function ProfileList({
  configs,
  selected,
  live,
  onSelect,
  onAdd,
  onDelete,
}: {
  configs: Config[];
  selected: string;
  live: string;
  onSelect: (id: string) => void;
  onAdd: () => void;
  onDelete: (id: string) => void;
}) {
  const scroller = useRef<HTMLDivElement>(null);
  const [scroll, setScroll] = useState(0);
  const [height, setHeight] = useState(320);
  const [wipe, setWipe] = useState<Config | null>(null);

  useEffect(() => {
    const el = scroller.current;
    if (!el) return;
    const measure = () => setHeight(el.clientHeight);
    measure();
    const observer = new ResizeObserver(measure);
    observer.observe(el);
    return () => observer.disconnect();
  }, []);

  const start = Math.max(0, Math.floor(scroll / ROW) - 2);
  const count = Math.ceil(height / ROW) + 5;
  const slice = configs.slice(start, start + count);

  return (
    <div className="profile-list">
      <div className="rail-head">
        <button type="button" onClick={onAdd}>
          Add
        </button>
      </div>
      <div
        className="rail-scroll"
        ref={scroller}
        onScroll={(e) => setScroll(e.currentTarget.scrollTop)}
      >
        <div style={{ height: configs.length * ROW, position: "relative" }}>
          {slice.map((config, offset) => {
            const index = start + offset;
            const on = config.id === live;
            return (
              <button
                key={config.id}
                type="button"
                className={`macro-row${config.id === selected ? " is-selected" : ""}${on ? " is-running" : ""}`}
                style={{ position: "absolute", top: index * ROW, left: 0, right: 0, height: ROW - 6 }}
                onClick={() => onSelect(config.id)}
                onContextMenu={(event) => {
                  event.preventDefault();
                  if (configs.length > 1) setWipe(config);
                }}
              >
                <span className={`dot${on ? " is-on" : ""}`} />
                <span className="macro-copy">
                  <strong>{config.name}</strong>
                </span>
              </button>
            );
          })}
        </div>
      </div>
      {wipe ? (
        <Confirm
          title={`Delete ${wipe.name}?`}
          onCancel={() => setWipe(null)}
          onConfirm={() => {
            onDelete(wipe.id);
            setWipe(null);
          }}
        />
      ) : null}
    </div>
  );
}
