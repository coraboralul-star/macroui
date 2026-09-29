import { useEffect, useRef, useState, type DragEvent, type MouseEvent } from "react";
import { createPortal } from "react-dom";
import { blockPick, createBlock } from "../blocks";
import { pressPair } from "../eventLane";
import { flowText, heldName, place, shownBlocks, watchVerb } from "../macroFlow";
import { blankMacro, type Block, type Macro, type Step } from "../profile";
import { Capture } from "./Capture";
import { EventLane } from "./EventLane";

const ADD: Block["type"][] = ["whileHeld", "ifShort", "then", "repeat", "wait", "steps", "tapHold"];

export function Advanced({
  macros,
  selected,
  onChange,
  onDelete,
}: {
  macros: Macro[];
  selected: string;
  onChange: (macro: Macro) => void;
  onDelete: () => void;
}) {
  const macro = macros.find((item) => item.id === selected) ?? null;
  const blocks = macro ? shownBlocks(macro) : [];
  const [dragId, setDragId] = useState<string | null>(null);
  const [capture, setCapture] = useState<{ id: string; slot: "hold" | "watch" } | null>(null);
  const [menu, setMenu] = useState<{ x: number; y: number; index: number } | null>(null);
  const trigger = macro ? place(macro) : "";

  useEffect(() => {
    if (!menu) return;
    const close = () => setMenu(null);
    window.addEventListener("pointerdown", close);
    return () => window.removeEventListener("pointerdown", close);
  }, [menu]);

  const openAdd = (index: number) => (event: MouseEvent) => {
    event.preventDefault();
    event.stopPropagation();
    if (!macro) return;
    const left = Math.min(event.clientX, window.innerWidth - 260);
    const top = Math.min(event.clientY, window.innerHeight - 320);
    setMenu({ x: left, y: top, index });
  };

  const commit = (next: Block[]) => {
    if (!macro) return;
    onChange({ ...macro, advanced: true, basic: false, blocks: next, steps: [], playMode: next.some((block) => block.type === "tapHold") ? "whileHeld" : macro.playMode });
  };

  const updateBlock = (id: string, next: Block) => commit(blocks.map((item) => (item.id === id ? next : item)));

  const append = (id: string, step: Step) => {
    const block = blocks.find((item) => item.id === id);
    if (!block || block.type === "wait" || block.type === "tapHold") return;
    updateBlock(id, { ...block, steps: [...block.steps, step] });
  };

  const dropOn = (index: number) => (event: DragEvent) => {
    event.preventDefault();
    if (!dragId) return;
    const from = blocks.findIndex((block) => block.id === dragId);
    setDragId(null);
    if (from < 0 || from === index || from + 1 === index) return;
    const next = blocks.slice();
    const [item] = next.splice(from, 1);
    next.splice(from < index ? index - 1 : index, 0, item);
    commit(next);
  };

  return (
    <div className="advanced">
      <div className="adv-head">
        {macro ? (
          <input
            className="name"
            aria-label="Macro name"
            value={macro.name}
            spellCheck={false}
            onChange={(event) => onChange({ ...macro, name: event.target.value })}
          />
        ) : (
          <span className="name" />
        )}
        <button type="button" className={macro?.busy ? "is-on" : "ghost"} disabled={!macro} onClick={() => macro && onChange({ ...macro, busy: !macro.busy })}>
          Skip if running
        </button>
        <button type="button" className="ghost is-danger" disabled={!macro} onClick={onDelete}>
          Delete
        </button>
      </div>
      <div className={`adv-canvas${blocks.length ? "" : " is-empty"}`} onContextMenu={openAdd(blocks.length)}>
        {blocks.length === 0 ? <p className="adv-empty">Right-click to add</p> : null}
        {blocks.map((block, index) => (
          <article
            key={block.id}
            id={`block-${block.id}`}
            className={`block block-${block.type}${dragId === block.id ? " is-dragging" : ""}`}
            onContextMenu={openAdd(index + 1)}
            onDragOver={(event) => {
              if (dragId) event.preventDefault();
            }}
            onDrop={dropOn(index)}
          >
            <header className="block-head">
              <span className="block-num">{index + 1}</span>
              <strong
                draggable
                onDragStart={(event) => {
                  event.dataTransfer.setData("text/plain", block.id);
                  event.dataTransfer.effectAllowed = "move";
                  setDragId(block.id);
                }}
                onDragEnd={() => setDragId(null)}
              >
                {flowText(block, trigger)}
              </strong>
              <button type="button" className="block-x" aria-label="Remove block" onClick={() => commit(blocks.filter((item) => item.id !== block.id))}>
                ×
              </button>
            </header>
            <BlockFields
              block={block}
              macros={macros.filter((item) => item.id !== macro?.id)}
              onChange={(next) => updateBlock(block.id, next)}
              onCapture={(slot) => setCapture({ id: block.id, slot })}
            />
            {block.type === "wait" || block.type === "tapHold" ? null : (
              <>
                <div className="block-actions">
                  <button type="button" onClick={() => setCapture({ id: block.id, slot: "watch" })}>
                    Key
                  </button>
                  <button type="button" onClick={() => setCapture({ id: block.id, slot: "watch" })}>
                    Mouse
                  </button>
                  <button type="button" onClick={() => append(block.id, { type: "wait", ms: 13 })}>
                    Wait
                  </button>
                </div>
                {block.steps.length ? (
                  <EventLane
                    steps={block.steps}
                    macros={macros.filter((item) => item.id !== macro?.id)}
                    empty=""
                    onChange={(steps) => updateBlock(block.id, { ...block, steps })}
                  />
                ) : null}
              </>
            )}
          </article>
        ))}
        <div className="block-end" onDragOver={(event) => dragId && event.preventDefault()} onDrop={dropOn(blocks.length)} />
      </div>
      {menu
        ? createPortal(
            <div className="ctx adv-menu" style={{ left: menu.x, top: menu.y }} onPointerDown={(event) => event.stopPropagation()}>
              {ADD.map((type) => (
                <button
                  key={type}
                  type="button"
                  onClick={() => {
                    const next = blocks.slice();
                    next.splice(menu.index, 0, createBlock(type));
                    setMenu(null);
                    commit(next);
                  }}
                >
                  {blockPick(type).name}
                </button>
              ))}
            </div>,
            document.body,
          )
        : null}
      {capture ? (
        <Capture
          title=""
          onCancel={() => setCapture(null)}
            onPick={(picked) => {
            const block = blocks.find((item) => item.id === capture.id);
            if (block && block.type !== "wait" && block.type !== "tapHold") {
              const kind = picked.kind === "key" ? "key" : "mouse";
              updateBlock(capture.id, { ...block, steps: [...block.steps, ...pressPair(kind, picked.button)] });
            }
            if (block?.type === "tapHold" && capture.slot === "hold")
              updateBlock(capture.id, { ...block, key: picked.button });
            if (block?.type === "tapHold" && capture.slot === "watch" && !block.watch.includes(picked.button))
              updateBlock(capture.id, { ...block, watch: [...block.watch, picked.button] });
            setCapture(null);
          }}
        />
      ) : null}
    </div>
  );
}

export function freshAdvanced(): Macro {
  return { ...blankMacro(), advanced: true, busy: true, blocks: [], steps: [] };
}

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
          <input
            aria-label="Released before ms"
            type="number"
            min={0}
            value={block.underMs}
            onChange={(event) => onChange({ ...block, underMs: Math.max(0, Math.round(Number(event.target.value) || 0)) })}
          />
        </label>
        <label>
          Keep going
          <input
            aria-label="Keep going times"
            type="number"
            min={0}
            value={block.minCycles}
            onChange={(event) => onChange({ ...block, minCycles: Math.max(0, Math.round(Number(event.target.value) || 0)) })}
          />
        </label>
      </span>
    );
  }
  if (block.type === "then") {
    return (
      <span className="block-fields">
        <label>
          After release
          <input
            aria-label="After release ms"
            type="number"
            min={0}
            value={block.forMs}
            onChange={(event) => onChange({ ...block, forMs: Math.max(0, Math.round(Number(event.target.value) || 0)) })}
          />
        </label>
      </span>
    );
  }
  if (block.type === "repeat") {
    return (
      <span className="block-fields">
        <label>
          Times
          <input
            aria-label="Repeat times"
            type="number"
            min={0}
            value={block.count}
            onChange={(event) => onChange({ ...block, count: Math.max(0, Math.round(Number(event.target.value) || 0)) })}
          />
        </label>
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
          <input
            aria-label="Repress after ms"
            type="number"
            min={0}
            value={block.gapMs}
            onChange={(event) => onChange({ ...block, gapMs: Math.max(0, Math.round(Number(event.target.value) || 0)) })}
          />
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
        <label>
          Repress delay
          <input
            aria-label="Repress delay ms"
            type="number"
            min={0}
            value={block.armMs}
            onChange={(event) => onChange({ ...block, armMs: Math.max(0, Math.round(Number(event.target.value) || 0)) })}
          />
        </label>
      </span>
    );
  }
  if (block.type === "whileHeld") {
    return (
      <span className="block-fields mute-fields">
        <MutePick id={block.id} mute={block.mute} macros={macros} onChange={(mute) => onChange({ ...block, mute })} />
      </span>
    );
  }
  if (block.type === "wait") {
    return (
      <span className="block-fields">
        <label>
          ms
          <input
            aria-label="Wait ms"
            type="number"
            min={0}
            value={block.ms}
            onChange={(event) => onChange({ ...block, ms: Math.max(0, Math.round(Number(event.target.value) || 0)) })}
          />
        </label>
      </span>
    );
  }
  return null;
}
