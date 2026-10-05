import { useEffect, useState, type DragEvent as ReactDragEvent, type MouseEvent } from "react";
import { createPortal } from "react-dom";
import { LIBRARY, LANE_GROUPS, LIBRARY_MIME, libraryItem, librarySteps, matchInput } from "../library";
import type { Macro, Step } from "../profile";
import { Capture, type Captured } from "./Capture";
import { FieldSelect } from "./FieldSelect";
import { NumberField } from "./NumberField";
import { canPlace, deleteAct, dropHold, expandTaps, insertSteps, laneCells, moveStep, replaceAct, setDelay, type LaneCell } from "../eventLane";

type MenuLine = { kind: "label"; text: string } | { kind: "item"; label: string; run: () => void };

export function EventLane({
  steps,
  macros,
  onChange,
  empty = "",
  readOnly = false,
  tweak = false,
  allowScanWait = true,
}: {
  steps: Step[];
  macros: Macro[];
  onChange: (steps: Step[]) => void;
  empty?: string;
  readOnly?: boolean;
  /** Double-click a wait or a key. No insert buttons. */
  tweak?: boolean;
  allowScanWait?: boolean;
}) {
  const [menu, setMenu] = useState<{ x: number; y: number; items: MenuLine[] } | null>(null);
  const [editDelay, setEditDelay] = useState<string | null>(null);
  const [pending, setPending] = useState<number | null>(null);
  const [replace, setReplace] = useState<number | null>(null);
  const [insertAt, setInsertAt] = useState<{ index: number; id: string } | null>(null);
  const [dragFrom, setDragFrom] = useState<number | null>(null);
  const [target, setTarget] = useState<number | null>(null);
  const [libAt, setLibAt] = useState<number | null>(null);
  const cells = laneCells(steps);

  useEffect(() => {
    if (readOnly) return;
    const next = expandTaps(steps);
    if (next !== steps) onChange(next);
  }, [steps, readOnly]);

  const fromLib = (event: ReactDragEvent) => !readOnly && event.dataTransfer.types.includes(LIBRARY_MIME);

  const allowDrop = (slot: number, order: number) => (event: ReactDragEvent) => {
    if (fromLib(event)) {
      event.preventDefault();
      event.dataTransfer.dropEffect = "copy";
      setLibAt(slot);
      return;
    }
    if (readOnly || dragFrom == null || !canPlace(steps, dragFrom, slot)) return;
    event.preventDefault();
    event.dataTransfer.dropEffect = "move";
    setTarget((current) => (current === order ? current : order));
  };
  const dropAt = (slot: number) => (event: ReactDragEvent) => {
    event.preventDefault();
    // A nested lane sits inside an outer one, so a drop must not reach both.
    event.stopPropagation();
    setLibAt(null);
    const lib = event.dataTransfer.getData(LIBRARY_MIME);
    if (lib) {
      const extra = librarySteps(lib);
      if (extra.some((step) => step.type === "scanWait") && !allowScanWait) return;
      if (extra.length) onChange(insertSteps(steps, slot, extra));
      return;
    }
    const raw = event.dataTransfer.getData("text/plain");
    const from = dragFrom ?? (raw === "" ? Number.NaN : Number(raw));
    setDragFrom(null);
    setTarget(null);
    if (Number.isFinite(from)) onChange(moveStep(steps, from, slot));
  };

  useEffect(() => {
    if (!menu) return;
    const close = () => setMenu(null);
    window.addEventListener("pointerdown", close);
    return () => window.removeEventListener("pointerdown", close);
  }, [menu]);

  const openMenu = (event: MouseEvent, items: MenuLine[]) => {
    event.preventDefault();
    event.stopPropagation();
    setMenu({ x: event.clientX, y: event.clientY, items });
  };

  const insertMenu = (index: number): MenuLine[] => {
    const lines: MenuLine[] = [];
    for (const group of LANE_GROUPS) {
      const items = LIBRARY.filter(
        (item) =>
          item.group === group &&
          item.kind === "step" &&
          (item.id !== "runMacro" || macros.length > 0) &&
          (item.id !== "scanWait" || allowScanWait),
      );
      if (!items.length) continue;
      lines.push({ kind: "label", text: group });
      for (const item of items) {
        if (item.kind !== "step") continue;
        lines.push({
          kind: "item",
          label: item.label,
          run: () => {
            if (item.pick) {
              setInsertAt({ index, id: item.id });
              return;
            }
            onChange(insertSteps(steps, index, item.create()));
            if (item.id === "wait" || item.id === "scanWait") setPending(index);
          },
        });
      }
    }
    return lines;
  };

  const editKey = (index: number) => setReplace(index);

  return (
    <div
      className="lane"
      aria-label="Macro events"
      onContextMenu={(event) => {
        if (readOnly) return;
        if ((event.target as HTMLElement).closest(".ev, .ev-plus, .ev-delay, .ev-repeat, input, select, button")) return;
        openMenu(event, insertMenu(steps.length));
      }}
      onDragOver={(event) => {
        if (!fromLib(event)) return;
        event.preventDefault();
        event.dataTransfer.dropEffect = "copy";
      }}
      onDragLeave={() => setLibAt(null)}
      onDrop={dropAt(steps.length)}
    >
      {cells.length === 0 && empty ? <p className="deck-empty">{empty}</p> : null}
      {cells.map((cell, order) => (
        <CellView
          key={`${cell.type}-${"index" in cell ? cell.index : order}-${order}`}
          cell={cell}
          steps={steps}
          macros={macros}
          readOnly={readOnly}
          tweak={tweak}
          editing={
            editDelay === `${cell.type}-${order}` ||
            (pending != null && cell.type === "delay" && cell.via === "wait" && cell.index === pending)
          }
          targeted={target === order || (cell.type === "join" && libAt === cell.index)}
          onEditDelay={() => setEditDelay(`${cell.type}-${order}`)}
          onCloseDelay={() => {
            setEditDelay(null);
            setPending(null);
          }}
          onChange={onChange}
          onInsert={(index, event) => openMenu(event, insertMenu(index))}
          onMenu={openMenu}
          onEditKey={editKey}
          dragFrom={dragFrom}
          onDragIndex={(index) => setDragFrom(index)}
          onDragEnd={() => {
            setDragFrom(null);
            setTarget(null);
          }}
          onAllowDrop={(slot) => allowDrop(slot, order)}
          onDropAt={dropAt}
          allowScanWait={allowScanWait}
        />
      ))}
      {menu
        ? createPortal(
            <div className="ctx" style={{ left: menu.x, top: menu.y }} onPointerDown={(event) => event.stopPropagation()}>
              {menu.items.map((item, index) =>
                item.kind === "label" ? (
                  <p key={`${item.text}-${index}`} className="ctx-label">
                    {item.text}
                  </p>
                ) : (
                  <button
                    key={`${item.label}-${index}`}
                    type="button"
                    onClick={() => {
                      setMenu(null);
                      item.run();
                    }}
                  >
                    {item.label}
                  </button>
                ),
              )}
            </div>,
            document.body,
          )
        : null}
      {insertAt ? (
        <Capture
          title={libraryItem(insertAt.id)?.label ?? "Add key"}
          onCancel={() => setInsertAt(null)}
          onPick={(captured: Captured) => {
            const kind = captured.kind === "key" ? "key" : "mouse";
            onChange(insertSteps(steps, insertAt.index, librarySteps(matchInput(insertAt.id, kind), captured.button)));
            setInsertAt(null);
          }}
        />
      ) : null}
      {replace != null ? (
        <Capture
          title="Change key"
          onCancel={() => setReplace(null)}
          onPick={(captured: Captured) => {
            const input = captured.kind === "key" ? "key" : "mouse";
            onChange(replaceAct(steps, replace, captured.button, input));
            setReplace(null);
          }}
        />
      ) : null}
    </div>
  );
}

function CellView({
  cell,
  steps,
  macros,
  readOnly,
  tweak,
  editing,
  targeted,
  onEditDelay,
  onCloseDelay,
  onChange,
  onInsert,
  onMenu,
  onEditKey,
  dragFrom,
  onDragIndex,
  onDragEnd,
  onAllowDrop,
  onDropAt,
  allowScanWait,
}: {
  cell: LaneCell;
  steps: Step[];
  macros: Macro[];
  readOnly: boolean;
  tweak: boolean;
  editing: boolean;
  targeted: boolean;
  onEditDelay: () => void;
  onCloseDelay: () => void;
  onChange: (steps: Step[]) => void;
  onInsert: (index: number, event: MouseEvent) => void;
  onMenu: (event: MouseEvent, items: MenuLine[]) => void;
  onEditKey: (index: number) => void;
  dragFrom: number | null;
  onDragIndex: (index: number) => void;
  onDragEnd: () => void;
  onAllowDrop: (slot: number) => (event: ReactDragEvent) => void;
  onDropAt: (slot: number) => (event: ReactDragEvent) => void;
  allowScanWait: boolean;
}) {
  if ((readOnly || tweak) && cell.type === "join") return null;
  if (readOnly && !tweak && cell.type === "delay") return <span className="ev-delay">{cell.ms} ms</span>;
  if (readOnly && !tweak && cell.type === "act") {
    return (
      <span className="ev ev-act">
        <b>{cell.title}</b>
        <small>{cell.subtitle}</small>
      </span>
    );
  }
  if (cell.type === "join") {
    if (readOnly) return null;
    return (
      <button
        type="button"
        className={`ev-plus${targeted ? " is-target" : ""}`}
        aria-label="Insert"
        onClick={(event) => onInsert(cell.index, event)}
        onDragOver={onAllowDrop(cell.index)}
        onDrop={onDropAt(cell.index)}
      >
        +
      </button>
    );
  }
  if (cell.type === "delay") {
    return editing ? (
      <NumberField
        className="ev-delay"
        ariaLabel="Delay ms"
        autoFocus
        min={0}
        value={cell.ms}
        onChange={(ms) => onChange(setDelay(steps, cell.index, ms, cell.via))}
        onBlur={onCloseDelay}
      />
    ) : (
      <button
        type="button"
        className={`ev-delay${dragFrom === cell.index && cell.via === "wait" ? " is-dragging" : ""}${targeted ? " is-target" : ""}`}
        draggable={!readOnly && cell.via === "wait"}
        onDragStart={(event) => {
          if (cell.via !== "wait") return;
          event.dataTransfer.setData("text/plain", String(cell.index));
          event.dataTransfer.effectAllowed = "move";
          onDragIndex(cell.index);
        }}
        onDragEnd={onDragEnd}
        onDragOver={onAllowDrop(cell.index)}
        onDrop={onDropAt(cell.index)}
        onDoubleClick={onEditDelay}
        onContextMenu={(event) => {
          if (readOnly) return;
          onMenu(event, [
            { kind: "item", label: "Edit", run: onEditDelay },
            { kind: "item", label: "Delete", run: () => onChange(cell.via === "hold" ? dropHold(steps, cell.index) : deleteAct(steps, cell.index)) },
          ]);
        }}
      >
        {cell.ms} ms
      </button>
    );
  }
  if (cell.type === "repeat") {
    return (
      <div className="ev-repeat">
        <div
          className="ev-repeat-head"
          onContextMenu={(event) =>
            readOnly ? undefined : onMenu(event, [{ kind: "item", label: "Delete", run: () => onChange(deleteAct(steps, cell.index)) }])
          }
        >
          <span>Repeat</span>
          {readOnly ? (
            <em>{cell.count === 0 ? "until stop" : `${cell.count}×`}</em>
          ) : (
            <NumberField
              ariaLabel="Repeat count"
              min={0}
              value={cell.count}
              onChange={(count) => onChange(steps.map((step, index) => (index === cell.index && step.type === "repeat" ? { ...step, count } : step)))}
            />
          )}
        </div>
        <EventLane
          steps={cell.steps}
          macros={macros}
          empty=""
          readOnly={readOnly}
          tweak={tweak}
          allowScanWait={allowScanWait}
          onChange={(inner) => onChange(steps.map((step, index) => (index === cell.index && step.type === "repeat" ? { ...step, steps: inner } : step)))}
        />
      </div>
    );
  }
  if (cell.type === "move") {
    return (
      <span
        className={`ev${dragFrom === cell.index ? " is-dragging" : ""}${targeted ? " is-target" : ""}`}
        draggable={!readOnly}
        onDragStart={(event) => {
          event.dataTransfer.setData("text/plain", String(cell.index));
          event.dataTransfer.effectAllowed = "move";
          onDragIndex(cell.index);
        }}
        onDragEnd={onDragEnd}
        onDragOver={onAllowDrop(cell.index)}
        onDrop={onDropAt(cell.index)}
        onContextMenu={(event) => onMenu(event, [{ kind: "item", label: "Delete", run: () => onChange(deleteAct(steps, cell.index)) }])}
      >
        <b>Move</b>
        <small>
          <NumberField
            ariaLabel="Move x"
            signed
            value={cell.x}
            onChange={(x) => onChange(steps.map((step, index) => (index === cell.index && step.type === "move" ? { ...step, x } : step)))}
          />
          <NumberField
            ariaLabel="Move y"
            signed
            value={cell.y}
            onChange={(y) => onChange(steps.map((step, index) => (index === cell.index && step.type === "move" ? { ...step, y } : step)))}
          />
        </small>
      </span>
    );
  }
  if (cell.type === "goto") {
    return (
      <span
        className={`ev${dragFrom === cell.index ? " is-dragging" : ""}${targeted ? " is-target" : ""}`}
        draggable={!readOnly}
        onDragStart={(event) => {
          event.dataTransfer.setData("text/plain", String(cell.index));
          event.dataTransfer.effectAllowed = "move";
          onDragIndex(cell.index);
        }}
        onDragEnd={onDragEnd}
        onDragOver={onAllowDrop(cell.index)}
        onDrop={onDropAt(cell.index)}
        onContextMenu={(event) => onMenu(event, [{ kind: "item", label: "Delete", run: () => onChange(deleteAct(steps, cell.index)) }])}
      >
        <b>Go to</b>
        <small>
          <select
            aria-label="Go to space"
            value={cell.where}
            onChange={(event) => onChange(steps.map((step, index) => (index === cell.index && step.type === "goto" ? { ...step, where: event.target.value as "screen" | "window" | "client" } : step)))}
          >
            <option value="screen">Screen</option>
            <option value="window">Window</option>
            <option value="client">Client</option>
          </select>
          <NumberField
            ariaLabel="Go to x"
            signed
            value={cell.x}
            onChange={(x) => onChange(steps.map((step, index) => (index === cell.index && step.type === "goto" ? { ...step, x } : step)))}
          />
          <NumberField
            ariaLabel="Go to y"
            signed
            value={cell.y}
            onChange={(y) => onChange(steps.map((step, index) => (index === cell.index && step.type === "goto" ? { ...step, y } : step)))}
          />
          <NumberField
            ariaLabel="Go to ms"
            min={0}
            value={cell.ms}
            onChange={(ms) => onChange(steps.map((step, index) => (index === cell.index && step.type === "goto" ? { ...step, ms } : step)))}
          />
        </small>
      </span>
    );
  }
  if (cell.type === "run") {
    return (
      <span
        className={`ev${dragFrom === cell.index ? " is-dragging" : ""}${targeted ? " is-target" : ""}`}
        draggable={!readOnly}
        onDragStart={(event) => {
          event.dataTransfer.setData("text/plain", String(cell.index));
          event.dataTransfer.effectAllowed = "move";
          onDragIndex(cell.index);
        }}
        onDragEnd={onDragEnd}
        onDragOver={onAllowDrop(cell.index)}
        onDrop={onDropAt(cell.index)}
        onContextMenu={(event) => onMenu(event, [{ kind: "item", label: "Delete", run: () => onChange(deleteAct(steps, cell.index)) }])}
      >
        <b>Run</b>
        <FieldSelect
          ariaLabel="Macro to run"
          value={cell.macroId}
          placeholder="Choose"
          options={macros.map((macro) => ({ value: macro.id, label: macro.name }))}
          onChange={(macroId) => onChange(steps.map((step, index) => (index === cell.index && step.type === "run" ? { ...step, macroId } : step)))}
        />
      </span>
    );
  }
  return (
    <button
      type="button"
      className={`ev ev-act${dragFrom === cell.index ? " is-dragging" : ""}${targeted ? " is-target" : ""}`}
      draggable={!readOnly}
      onDragStart={(event) => {
        event.dataTransfer.setData("text/plain", String(cell.index));
        event.dataTransfer.effectAllowed = "move";
        onDragIndex(cell.index);
      }}
      onDragEnd={onDragEnd}
      onDragOver={onAllowDrop(cell.index)}
      onDrop={onDropAt(cell.index)}
      onDoubleClick={() => onEditKey(cell.index)}
      onContextMenu={(event) => {
        if (readOnly) return;
        onMenu(event, [
          { kind: "item", label: "Edit", run: () => onEditKey(cell.index) },
          { kind: "item", label: "Delete", run: () => onChange(deleteAct(steps, cell.index)) },
        ]);
      }}
    >
      <b>{cell.title}</b>
      <small>{cell.subtitle}</small>
    </button>
  );
}
