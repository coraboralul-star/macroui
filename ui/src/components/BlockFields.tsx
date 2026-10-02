import { useEffect, useRef, useState } from "react";
import { heldName, watchVerb } from "../macroFlow";
import { IGNORE_LOCKS, PAUSE_WATCH, type Block, type IgnoreLock, type Macro, type PauseWatch } from "../profile";
import { FieldSelect } from "./FieldSelect";
import { Hint } from "./Hint";
import { NumberField } from "./NumberField";
import { ReleaseField } from "./ReleaseField";

function muteSummary(mute: string[], macros: Macro[]) {
  const names = mute.map((id) => macros.find((item) => item.id === id)?.name).filter((name): name is string => Boolean(name));
  if (!names.length) return macros.length ? "Choose" : "No other macros";
  if (names.length === 1) return names[0];
  return `${names.length} macros`;
}

function MutePick({
  id,
  mute,
  macros,
  onChange,
}: {
  id: string;
  mute: string[];
  macros: Macro[];
  onChange: (mute: string[]) => void;
}) {
  const root = useRef<HTMLSpanElement>(null);
  const seen = useRef(id);
  const [armed, setArmed] = useState(mute.length > 0);
  const [open, setOpen] = useState(false);
  if (seen.current !== id) {
    seen.current = id;
    setArmed(mute.length > 0);
    setOpen(false);
  }
  const on = armed || mute.length > 0;

  useEffect(() => {
    if (!open) return;
    const onDoc = (event: PointerEvent) => {
      if (!root.current?.contains(event.target as Node)) setOpen(false);
    };
    const onKey = (event: KeyboardEvent) => {
      if (event.key === "Escape") setOpen(false);
    };
    document.addEventListener("pointerdown", onDoc);
    document.addEventListener("keydown", onKey);
    return () => {
      document.removeEventListener("pointerdown", onDoc);
      document.removeEventListener("keydown", onKey);
    };
  }, [open]);

  return (
    <span className="mute-row" ref={root}>
      <label>
        <input
          type="checkbox"
          checked={on}
          onChange={(event) => {
            if (event.target.checked) {
              setArmed(true);
              setOpen(true);
              return;
            }
            setArmed(false);
            setOpen(false);
            onChange([]);
          }}
        />
        Mute while this runs
      </label>
      {on ? (
        <span className="mute-drop">
          <button type="button" className={`mute-drop-btn${open ? " is-open" : ""}`} aria-expanded={open} aria-label="Macros to mute" onClick={() => setOpen((next) => !next)}>
            <em>{muteSummary(mute, macros)}</em>
            <i aria-hidden="true" />
          </button>
          {open ? (
            <ul className="mute-menu" role="listbox" aria-label="Macros to mute">
              {macros.length ? (
                macros.map((item) => (
                  <li key={item.id}>
                    <label>
                      <input
                        type="checkbox"
                        checked={mute.includes(item.id)}
                        onChange={(event) => {
                          const next = event.target.checked ? [...mute, item.id] : mute.filter((key) => key !== item.id);
                          onChange(next);
                        }}
                      />
                      {item.name}
                    </label>
                  </li>
                ))
              ) : (
                <li className="is-empty">No other macros</li>
              )}
            </ul>
          ) : null}
        </span>
      ) : null}
    </span>
  );
}

export function BlockFields({
  block,
  macros = [],
  onChange,
  onCapture,
}: {
  block: Block;
  macros?: Macro[];
  onChange: (block: Block) => void;
  onCapture: (slot: "hold" | "watch") => void;
}) {
  if (block.type === "ifShort") {
    return (
      <span className="block-fields">
        <label>
          Released before
          <NumberField ariaLabel="Released before ms" min={0} value={block.underMs} onChange={(underMs) => onChange({ ...block, underMs })} />
        </label>
        <label>
          Keep going
          <NumberField ariaLabel="Keep going times" min={0} value={block.minCycles} onChange={(minCycles) => onChange({ ...block, minCycles })} />
        </label>
      </span>
    );
  }
  if (block.type === "then") {
    return (
      <span className="block-fields">
        <label>
          After release
          <NumberField ariaLabel="After release ms" min={0} value={block.forMs} onChange={(forMs) => onChange({ ...block, forMs })} />
        </label>
      </span>
    );
  }
  if (block.type === "repeat") {
    return (
      <span className="block-fields">
        <label>
          Times
          <NumberField ariaLabel="Repeat times" min={0} value={block.count} onChange={(count) => onChange({ ...block, count })} />
        </label>
        <BlockRelease block={block} onChange={onChange} />
      </span>
    );
  }
  if (block.type === "tapHold") {
    return (
      <span className="block-fields is-repress">
        <span className="repress-line">
          Repress
          <button type="button" onClick={() => onCapture("hold")}>
            {heldName(block.key) || "Trigger"}
          </button>
          <NumberField ariaLabel="Repress after ms" min={0} value={block.gapMs} onChange={(gapMs) => onChange({ ...block, gapMs })} />
          ms after
          <span className="watch-row">
            {block.watch.length ? (
              block.watch.map((item) => (
                <button key={item} type="button" onClick={() => onChange({ ...block, watch: block.watch.filter((key) => key !== item) })}>
                  {heldName(item)} <span className="act-x">×</span>
                </button>
              ))
            ) : (
              <button type="button" onClick={() => onCapture("watch")}>
                Tracked Keys
              </button>
            )}
            <button type="button" onClick={() => onCapture("watch")}>
              +
            </button>
          </span>
          {watchVerb(block.watch.length)} pressed
        </span>
        <span className="repress-lock">
          Tracked macros
          <FieldSelect
            ariaLabel="Tracked macros"
            value={block.pauseWatch}
            options={PAUSE_WATCH}
            onChange={(value) => onChange({ ...block, pauseWatch: value as PauseWatch })}
          />
          <Hint text="Off lets those macros keep running while you hold. Block turns them off until this Repress ends. The timer for extra presses is Ignore extra, not this." />
        </span>
        <span className="repress-lock">
          Ignore extra
          <FieldSelect
            ariaLabel="Ignore extra"
            value={block.ignore}
            options={IGNORE_LOCKS}
            onChange={(value) => {
              const ignore = value as IgnoreLock;
              onChange({
                ...block,
                ignore,
                ignoreMs: ignore === "off" ? block.ignoreMs : Math.max(block.ignoreMs, 150),
              });
            }}
          />
          {block.ignore !== "off" ? (
            <>
              <NumberField ariaLabel="Ignore extra ms" min={0} value={block.ignoreMs} onChange={(ignoreMs) => onChange({ ...block, ignoreMs })} />
              ms
            </>
          ) : null}
          <Hint text="Repress only. Takes the first press of each tracked key, then skips extra presses for this many ms, so a looping T or M5 macro does not keep retriggering Z. Macro skips only keys another macro sent. Both also skips physical presses. After this window, the next press can repress again." />
        </span>
        <label>
          Repress delay
          <NumberField ariaLabel="Repress delay ms" min={0} value={block.armMs} onChange={(armMs) => onChange({ ...block, armMs })} />
        </label>
      </span>
    );
  }
  if (block.type === "whileHeld") {
    return (
      <span className="block-fields mute-fields">
        <MutePick id={block.id} mute={block.mute} macros={macros} onChange={(mute) => onChange({ ...block, mute })} />
        <BlockRelease block={block} onChange={onChange} />
      </span>
    );
  }
  if (block.type === "steps") {
    return (
      <span className="block-fields">
        <BlockRelease block={block} onChange={onChange} />
      </span>
    );
  }
  if (block.type === "wait") {
    return (
      <span className="block-fields">
        <label>
          ms
          <NumberField ariaLabel="Wait ms" min={0} value={block.ms} onChange={(ms) => onChange({ ...block, ms })} />
        </label>
      </span>
    );
  }
  return null;
}

function BlockRelease({
  block,
  onChange,
}: {
  block: Extract<Block, { type: "whileHeld" | "steps" | "repeat" }>;
  onChange: (block: Block) => void;
}) {
  return (
    <ReleaseField
      label="Block on release"
      allowInherit
      value={block.releaseStop}
      onChange={(releaseStop) => onChange({ ...block, releaseStop })}
    />
  );
}
