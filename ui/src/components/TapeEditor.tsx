import type { Macro, Step } from "../profile";
import { blankStep } from "../profile";
import { pressLabel } from "../recording";
import { Capture, type Captured } from "./Capture";
import { FieldSelect } from "./FieldSelect";
import { useEffect, useState, type DragEvent, type MouseEvent } from "react";
import { createPortal } from "react-dom";
import { createLibraryStep, LIBRARY_MIME } from "../library";
import { moveBlock, patchPress, readList, segment, spanOf, writeList, type Path, type TapeNode } from "../tape";

const INSERTS: { id: string; label: string; step: () => Step }[] = [
  { id: "key", label: "Key", step: () => blankStep("key") },
  { id: "down", label: "↓", step: () => ({ type: "key", action: "down", key: "w" }) },
  { id: "up", label: "↑", step: () => ({ type: "key", action: "up", key: "w" }) },
  { id: "mouse", label: "Click", step: () => blankStep("mouse") },
  { id: "move", label: "Move", step: () => blankStep("move") },
  { id: "wait", label: "Wait", step: () => blankStep("wait") },
  { id: "repeat", label: "Repeat", step: () => blankStep("repeat") },
  { id: "run", label: "Run", step: () => blankStep("run") },
];

type Drag = { path: Path; index: number; span: number };

let drag: Drag | null = null;

export function TapeEditor({
  steps,
  macros,
  onChange,
}: {
  steps: Step[];
  macros: Macro[];
  onChange: (steps: Step[]) => void;
}) {
  return (
    <div className="tape-editor">
      <TapeList root={steps} path={[]} macros={macros} onCommit={onChange} />
    </div>
  );
}

function TapeList({
  root,
  path,
  macros,
  onCommit,
}: {
  root: Step[];
  path: Path;
  macros: Macro[];
  onCommit: (steps: Step[]) => void;
}) {
  const steps = readList(root, path);
  const nodes = segment(steps);
  const [menu, setMenu] = useState<number | null>(null);
  const edit = (next: Step[]) => onCommit(writeList(root, path, next));
  const dropAt = (to: number) => {
    if (!drag) return;
    onCommit(moveBlock(root, drag.path, drag.index, drag.span, path, to));
    drag = null;
  };

  const accept = (event: DragEvent) => {
    if (event.dataTransfer.types.includes(LIBRARY_MIME) || drag) event.preventDefault();
  };
  const land = (event: DragEvent, index: number) => {
    const made = libraryStep(event);
    if (made) {
      edit(insertAt(steps, index, made));
      drag = null;
      return;
    }
    if (index === steps.length && steps.length === 0) dropAt(0);
    else if (drag) {
      onCommit(moveBlock(root, drag.path, drag.index, drag.span, path, index));
      drag = null;
    }
  };

  return (
    <div
      className="tape"
      aria-label="Steps"
      onDragOver={accept}
      onDrop={(event) => {
        const target = event.target as HTMLElement | null;
        if (target?.closest?.(".tape-plus")) return;
        event.preventDefault();
        event.stopPropagation();
        land(event, steps.length === 0 ? 0 : steps.length);
      }}
    >
      {nodes.length === 0 ? (
        <p className="deck-empty">{path.length ? "Drop a function here." : "Record a take, or drop a function here."}</p>
      ) : null}
      {nodes.map((node) => {
        const end = node.index + spanOf(node);
        return (
          <span key={`${node.type}-${node.index}`} className={`tape-item${node.type === "repeat" ? " is-group" : ""}`}>
            {node.type === "repeat" ? (
              <div className="tape-group tex">
                <div
                  className="tape-group-head"
                  draggable
                  onDragStart={(event) => {
                    event.dataTransfer.setData("text/plain", "step");
                    event.dataTransfer.effectAllowed = "move";
                    drag = { path, index: node.index, span: 1 };
                  }}
                  onContextMenu={(event) => {
                    event.preventDefault();
                    edit(steps.filter((_, index) => index !== node.index));
                  }}
                >
                  <span>Repeat</span>
                  <input
                    aria-label="Repeat count"
                    type="number"
                    min={0}
                    value={node.count}
                    onChange={(event) => {
                      const count = Math.max(0, Math.round(Number(event.target.value) || 0));
                      edit(steps.map((step, index) => (index === node.index && step.type === "repeat" ? { ...step, count } : step)));
                    }}
                  />
                  <small>{node.count === 0 ? "until stop" : "times"}</small>
                  <button type="button" aria-label="Remove repeat" onClick={() => edit(steps.filter((_, index) => index !== node.index))}>
                    ×
                  </button>
                </div>
                <TapeList root={root} path={[...path, node.index]} macros={macros} onCommit={onCommit} />
              </div>
            ) : (
              <NodeView node={node} steps={steps} path={path} macros={macros} onChange={edit} />
            )}
            <Plus
              at={end}
              open={menu === end}
              onToggle={() => setMenu(menu === end ? null : end)}
              onLand={(event) => land(event, end)}
              onInsert={(step) => { edit(insertAt(steps, end, step)); setMenu(null); }}
            />
          </span>
        );
      })}
    </div>
  );
}

function NodeView({
  node,
  steps,
  path,
  macros,
  onChange,
}: {
  node: Exclude<TapeNode, { type: "repeat" }>;
  steps: Step[];
  path: Path;
  macros: Macro[];
  onChange: (steps: Step[]) => void;
}) {
  const [menu, setMenu] = useState<{ x: number; y: number } | null>(null);
  const [replace, setReplace] = useState(false);
  const startDrag = (event: DragEvent) => {
    event.stopPropagation();
    event.dataTransfer.setData("text/plain", "step");
    event.dataTransfer.effectAllowed = "move";
    drag = { path, index: node.index, span: spanOf(node) };
  };
  const openMenu = (event: MouseEvent) => {
    event.preventDefault();
    event.stopPropagation();
    setMenu({ x: event.clientX, y: event.clientY });
  };
  const remove = () => onChange(steps.filter((_, index) => index < node.index || index >= node.index + spanOf(node)));
  useEffect(() => {
    if (!menu) return;
    const close = () => setMenu(null);
    window.addEventListener("pointerdown", close);
    return () => window.removeEventListener("pointerdown", close);
  }, [menu]);
  const menuNode = menu
    ? createPortal(
        <div className="ctx" style={{ left: menu.x, top: menu.y }} onPointerDown={(event) => event.stopPropagation()}>
          <button
            type="button"
            onClick={() => {
              setMenu(null);
              remove();
            }}
          >
            Delete
          </button>
        </div>,
        document.body,
      )
    : null;
  if (node.type === "wait") {
    return (
      <>
        <input
          className="gap-tick"
          aria-label="Wait ms"
          type="number"
          min={0}
          draggable
          onDragStart={startDrag}
          onContextMenu={openMenu}
          value={node.ms}
          onChange={(event) => onChange(swap(steps, node, { type: "wait", ms: Math.max(0, Math.round(Number(event.target.value) || 0)) }))}
        />
        {menuNode}
      </>
    );
  }
  return (
    <span className={`cap${node.type === "press" && node.action !== "tap" ? " is-edge" : ""}`} onContextMenu={openMenu}>
      <b
        draggable
        onDragStart={startDrag}
        onDoubleClick={
          node.type === "press"
            ? (event) => {
                event.preventDefault();
                setReplace(true);
              }
            : undefined
        }
      >
        {node.type === "press" ? pressLabel(node.button) : node.type === "move" ? "move" : "run"}
      </b>
      {node.type === "press" && node.action === "down" ? (
        <button type="button" className="arrow" aria-label="Down" onClick={() => onChange(patchPress(steps, node, { action: "up" }))}>↓</button>
      ) : null}
      {node.type === "press" && node.action === "up" ? (
        <button type="button" className="arrow" aria-label="Up" onClick={() => onChange(patchPress(steps, node, { action: "tap" }))}>↑</button>
      ) : null}
      {node.type === "press" && node.action === "tap" ? (
        <input
          className="hold"
          aria-label="Hold ms"
          type="number"
          min={1}
          value={node.holdMs ?? 20}
          onChange={(event) => onChange(patchPress(steps, node, { holdMs: Math.max(1, Math.round(Number(event.target.value) || 1)) }))}
        />
      ) : null}
      {node.type === "move" ? (
        <span className="move-xy">
          <input aria-label="Move x" type="number" value={node.x} onChange={(event) => onChange(swap(steps, node, { type: "move", x: Number(event.target.value) || 0, y: node.y }))} />
          <input aria-label="Move y" type="number" value={node.y} onChange={(event) => onChange(swap(steps, node, { type: "move", x: node.x, y: Number(event.target.value) || 0 }))} />
        </span>
      ) : null}
      {node.type === "run" ? (
        <FieldSelect
          ariaLabel="Macro to run"
          value={node.macroId}
          placeholder="Choose"
          options={macros.map((macro) => ({ value: macro.id, label: macro.name }))}
          onChange={(macroId) => onChange(swap(steps, node, { type: "run", macroId }))}
        />
      ) : null}
      <button type="button" className="cap-x" aria-label="Remove step" onClick={remove}>
        ×
      </button>
      {menuNode}
      {replace && node.type === "press" ? (
        <Capture
          title=""
          onCancel={() => setReplace(false)}
          onPick={(captured: Captured) => {
            setReplace(false);
            const input = captured.kind === "key" ? "key" : "mouse";
            onChange(patchPress(steps, node, { button: captured.button, input }));
          }}
        />
      ) : null}
    </span>
  );
}

function libraryStep(event: DragEvent): Step | null {
  const id = event.dataTransfer.getData(LIBRARY_MIME);
  return id ? createLibraryStep(id) : null;
}

function Plus({
  at,
  open,
  onToggle,
  onLand,
  onInsert,
}: {
  at: number;
  open: boolean;
  onToggle: () => void;
  onLand: (event: DragEvent) => void;
  onInsert: (step: Step) => void;
}) {
  const [hot, setHot] = useState(false);
  return (
    <span
      className={`tape-plus${hot ? " is-hot" : ""}`}
      onDragOver={(event) => {
        event.preventDefault();
        setHot(true);
      }}
      onDragLeave={() => setHot(false)}
      onDrop={(event) => {
        event.preventDefault();
        event.stopPropagation();
        setHot(false);
        onLand(event);
      }}
    >
      <button
        type="button"
        aria-label={`Add step ${at}`}
        aria-expanded={open}
        onClick={onToggle}
        onContextMenu={(event) => {
          event.preventDefault();
          onInsert({ type: "wait", ms: 15 });
        }}
      >
        +
      </button>
      {open ? (
        <span className="plus-menu">
          {INSERTS.map((item) => (
            <button key={item.id} type="button" onClick={() => onInsert(item.step())}>{item.label}</button>
          ))}
        </span>
      ) : null}
    </span>
  );
}

function insertAt(steps: Step[], index: number, step: Step) {
  const next = steps.slice();
  next.splice(index, 0, step);
  return next;
}

function swap(steps: Step[], node: TapeNode, step: Step) {
  return [...steps.slice(0, node.index), step, ...steps.slice(node.index + spanOf(node))];
}
