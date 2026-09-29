import { Fragment, useEffect, useMemo, useRef, useState, type DragEvent, type PointerEvent } from "react";
import { blockPick, cloneBlock, insertTyped, reorderBlocks } from "../blocks";
import { pressPair } from "../eventLane";
import { compileRecording, type RecEvent } from "../recording";
import { effectivePlayMode, GRAPH_NODE_H, GRAPH_NODE_W, graphLayout, place, shownBlocks } from "../macroFlow";
import type { Block, Macro } from "../profile";
import { BlockFields } from "./Advanced";
import { BlockLead } from "./BlockLead";
import { Capture } from "./Capture";
import { Confirm } from "./Confirm";
import { GraphPreview } from "./GraphPreview";
import { KeyFace } from "./KeyFace";
import { RecordSurface } from "./RecordSurface";
import { SlideToggle } from "./SlideToggle";

const ADD: Block["type"][] = ["whileHeld", "ifShort", "then", "repeat", "wait", "steps", "tapHold"];

export function GraphEditor({
  macros,
  selected,
  onChange,
  onClose,
  onDelete,
}: {
  macros: Macro[];
  selected: string;
  onChange: (macro: Macro) => void;
  onClose: () => void;
  onDelete: () => void;
}) {
  const macro = macros.find((item) => item.id === selected) ?? null;
  const blocks = macro ? shownBlocks(macro) : [];
  const others = macros.filter((item) => item.id !== macro?.id);
  const trigger = macro ? place(macro) : "";
  const mode = macro ? effectivePlayMode(macro) : "once";
  const [tab, setTab] = useState<"full" | "edit">("full");
  const [picked, setPicked] = useState(blocks[0]?.id ?? "");
  const [pan, setPan] = useState({ x: 0, y: 0 });
  const [menu, setMenu] = useState<number | null>(null);
  const [flowNode, setFlowNode] = useState("");
  const [previewNonce, setPreviewNonce] = useState(0);
  const [dragId, setDragId] = useState<string | null>(null);
  const [dropAt, setDropAt] = useState<number | null>(null);
  const [capture, setCapture] = useState<{ slot: "hold" | "watch" | "step" | "bind" } | null>(null);
  const [live, setLive] = useState<RecEvent[] | null>(null);
  const [wipe, setWipe] = useState<"macro" | "block" | null>(null);
  const panDrag = useRef<{ x: number; y: number; px: number; py: number } | null>(null);

  useEffect(() => {
    setPicked(blocks[0]?.id ?? "");
    setMenu(null);
    setDragId(null);
    setDropAt(null);
    setLive(null);
    setTab("full");
  }, [macro?.id]);

  useEffect(() => {
    if (picked && (picked.endsWith("-held") || blocks.some((block) => block.id === picked))) return;
    setPicked(blocks[0]?.id ?? "");
  }, [blocks, picked]);

  useEffect(() => {
    if (menu == null) return;
    const close = () => setMenu(null);
    window.addEventListener("pointerdown", close);
    return () => window.removeEventListener("pointerdown", close);
  }, [menu]);

  const layout = useMemo(() => graphLayout(blocks), [blocks]);

  const commit = (next: Block[], pick?: string) => {
    if (!macro) return;
    onChange({
      ...macro,
      advanced: true,
      basic: false,
      blocks: next,
      steps: [],
      playMode: next.some((block) => block.type === "tapHold") ? "whileHeld" : macro.playMode,
    });
    if (pick) setPicked(pick);
  };

  const selectedBlock = blocks.find((block) => block.id === picked) ?? null;
  const selectedIndex = selectedBlock ? blocks.findIndex((block) => block.id === selectedBlock.id) : -1;

  const updateBlock = (id: string, next: Block) => commit(blocks.map((item) => (item.id === id ? next : item)));

  const dropBlock = () => {
    if (!selectedBlock) return;
    const next = blocks.filter((item) => item.id !== selectedBlock.id);
    commit(next, next[Math.max(0, selectedIndex - 1)]?.id ?? "");
  };

  const addAt = (index: number, type: Block["type"]) => {
    const next = insertTyped(blocks, index, type);
    setMenu(null);
    commit(next.blocks, next.pick);
  };

  const dropOn = (index: number) => (event: DragEvent) => {
    event.preventDefault();
    const fromId = dragId || event.dataTransfer.getData("text/plain");
    setDragId(null);
    setDropAt(null);
    if (!fromId) return;
    commit(reorderBlocks(blocks, fromId, index), fromId);
  };

  const previewAt = useRef({ id: "", t: 0 });
  const previewNode = (id: string) => {
    const t = performance.now();
    if (previewAt.current.id === id && t - previewAt.current.t < 120) return;
    previewAt.current = { id, t };
    setPicked(id);
    setPreviewNonce((value) => value + 1);
  };

  const openEdit = (id: string) => {
    setPicked(id);
    setTab("edit");
  };

  useEffect(() => {
    const onKey = (event: KeyboardEvent) => {
      const typing = event.target instanceof HTMLElement && event.target.closest("input, textarea, select");
      if (typing) return;
      if (event.key === "Escape") {
        if (menu != null) {
          setMenu(null);
          return;
        }
        onClose();
        return;
      }
      if (!selectedBlock) return;
      if ((event.ctrlKey || event.metaKey) && event.key.toLowerCase() === "d") {
        event.preventDefault();
        const copy = cloneBlock(selectedBlock);
        const next = blocks.slice();
        next.splice(selectedIndex + 1, 0, copy);
        commit(next, copy.id);
        return;
      }
      if (event.key === "Delete") {
        event.preventDefault();
        setWipe("block");
      }
    };
    window.addEventListener("keydown", onKey);
    return () => window.removeEventListener("keydown", onKey);
  }, [blocks, menu, onClose, selectedBlock, selectedIndex]);

  const onCanvasDown = (event: PointerEvent<HTMLDivElement>) => {
    const target = event.target as HTMLElement;
    if (target.closest("input, textarea")) return;
    window.getSelection()?.removeAllRanges();
    event.preventDefault();
    if (target.closest(".graph-node, .graph-node-x, .graph-flow, .graph-add, .graph-add-menu, .graph-insert")) return;
    panDrag.current = { x: pan.x, y: pan.y, px: event.clientX, py: event.clientY };
    event.currentTarget.setPointerCapture(event.pointerId);
  };

  const onCanvasMove = (event: PointerEvent<HTMLDivElement>) => {
    if (panDrag.current) {
      setPan({ x: panDrag.current.x + event.clientX - panDrag.current.px, y: panDrag.current.y + event.clientY - panDrag.current.py });
    }
  };

  const width = layout.width;
  const height = layout.height;
  const canRecord = selectedBlock && selectedBlock.type !== "wait" && selectedBlock.type !== "tapHold";

  const tabs = (
    <SlideToggle
      label="Advanced views"
      value={tab}
      options={[
        { id: "full", label: "Full View" },
        { id: "edit", label: "Edit", disabled: !macro },
      ]}
      onChange={(id) => {
        if (id === "edit") {
          if (!macro) return;
          if (picked.endsWith("-held")) setPicked(picked.slice(0, -5));
          else if (!blocks.some((block) => block.id === picked)) setPicked(blocks[0]?.id ?? "");
        }
        setTab(id === "edit" ? "edit" : "full");
      }}
    />
  );

  return (
    <div className="graph-root" role="dialog" aria-label="Advanced">
      <header className="graph-bar">
        {tabs}
        {tab === "full" && macro ? (
          <input
            className="graph-bar-name"
            aria-label="Macro name"
            value={macro.name}
            spellCheck={false}
            onChange={(event) => onChange({ ...macro, name: event.target.value })}
          />
        ) : null}
        <span className="graph-bar-space" />
        <button type="button" className={macro?.busy ? "is-on" : "ghost"} disabled={!macro} onClick={() => macro && onChange({ ...macro, busy: !macro.busy })}>
          Skip if running
        </button>
        <button type="button" className="ghost is-danger" disabled={!macro} onClick={() => setWipe("macro")}>
          Delete
        </button>
        <button type="button" className="ghost" onClick={onClose}>
          Close
        </button>
      </header>
      {tab === "full" ? (
        <div className="graph-body is-full">
          <div
            className="graph-canvas"
            onPointerDown={onCanvasDown}
            onPointerMove={onCanvasMove}
            onPointerUp={() => {
              panDrag.current = null;
            }}
            onDragStart={(event) => {
              if (!(event.target as HTMLElement).closest(".graph-node")) event.preventDefault();
            }}
          >
            <div className="graph-world" style={{ transform: `translate(${pan.x}px, ${pan.y}px)`, width, height }}>
              <svg className="graph-wires" width={width} height={height} aria-hidden="true">
                <defs>
                  <marker id="graph-arrow" markerWidth="12" markerHeight="10" refX="11" refY="5" orient="auto">
                    <polygon points="0 0, 12 5, 0 10" fill="#9a9a9a" />
                  </marker>
                </defs>
                {layout.links.map((link) => {
                  const from = layout.nodes.find((node) => node.id === link.from);
                  const to = layout.nodes.find((node) => node.id === link.to);
                  if (!from || !to) return null;
                  const x1 = from.x + GRAPH_NODE_W;
                  const y1 = from.y + GRAPH_NODE_H / 2;
                  const x2 = to.x;
                  const y2 = to.y + GRAPH_NODE_H / 2;
                  const mid = (x1 + x2) / 2;
                  return (
                    <path
                      key={`${link.from}-${link.to}-${link.fork ?? "main"}`}
                      className={link.fork ? `is-${link.fork}` : undefined}
                      d={`M ${x1} ${y1} C ${mid} ${y1}, ${mid} ${y2}, ${x2} ${y2}`}
                      fill="none"
                      stroke="#8d8d8d"
                      strokeWidth="2"
                      markerEnd="url(#graph-arrow)"
                    />
                  );
                })}
              </svg>
              {layout.labels.map((label) => (
                <span key={label.id} className="graph-fork-label" style={{ left: label.x, top: label.y }}>
                  {label.text}
                </span>
              ))}
              {layout.nodes.map((node) => {
                const item = blocks.find((block) => block.id === node.id);
                const index = blocks.findIndex((block) => block.id === node.id);
                const movable = node.kind !== "heldPath";
                return (
                  <Fragment key={node.id}>
                  <button
                    type="button"
                    draggable={movable}
                    className={`graph-node block-${node.kind}${picked === node.id ? " is-on" : ""}${flowNode === node.id ? " is-preview" : ""}${dragId === node.id ? " is-lift" : ""}`}
                    style={{ left: node.x, top: node.y, width: GRAPH_NODE_W, height: GRAPH_NODE_H }}
                    onPointerDown={(event) => {
                      event.stopPropagation();
                      if (event.button === 0) previewNode(node.id);
                    }}
                    onClick={() => previewNode(node.id)}
                    onDoubleClick={() => {
                      if (node.kind === "heldPath") return;
                      openEdit(node.id);
                    }}
                    onDragStart={(event) => {
                      if (!movable) {
                        event.preventDefault();
                        return;
                      }
                      event.dataTransfer.setData("text/plain", node.id);
                      event.dataTransfer.effectAllowed = "move";
                      setPicked(node.id);
                      setDragId(node.id);
                    }}
                    onDragEnd={() => {
                      setDragId(null);
                      setDropAt(null);
                    }}
                    onDragOver={(event) => {
                      if (!movable || !dragId) return;
                      event.preventDefault();
                      setDropAt(index);
                    }}
                    onDrop={movable ? dropOn(index) : undefined}
                  >
                    <strong>
                      {node.kind === "heldPath" ? (
                        <>Held past {node.underMs} ms</>
                      ) : (
                        <BlockLead
                          type={node.kind as Block["type"]}
                          trigger={trigger}
                          beforeMs={item?.type === "ifShort" ? item.underMs : undefined}
                        />
                      )}
                    </strong>
                    {node.hint ? <em>{node.hint}</em> : null}
                  </button>
                  {movable ? (
                    <button
                      type="button"
                      className="graph-node-x"
                      aria-label="Remove block"
                      style={{ left: node.x + GRAPH_NODE_W - 11, top: node.y - 8 }}
                      onPointerDown={(event) => event.stopPropagation()}
                      onClick={(event) => {
                        event.stopPropagation();
                        setPicked(node.id);
                        setWipe("block");
                      }}
                    >
                      ×
                    </button>
                  ) : null}
                  </Fragment>
                );
              })}
              {layout.slots.map((slot) => (
                <button
                  key={`slot-${slot.index}`}
                  type="button"
                  className={`graph-insert${dropAt === slot.index ? " is-hot" : ""}`}
                  style={{ left: slot.x, top: slot.y }}
                  aria-label={`Insert at step ${slot.index + 1}`}
                  onPointerDown={(event) => event.stopPropagation()}
                  onClick={(event) => {
                    event.stopPropagation();
                    setMenu((open) => (open === slot.index ? null : slot.index));
                  }}
                  onDragOver={(event) => {
                    if (!dragId) return;
                    event.preventDefault();
                    setDropAt(slot.index);
                  }}
                  onDrop={dropOn(slot.index)}
                >
                  +
                </button>
              ))}
              {menu != null ? (
                <div
                  className="graph-add-menu"
                  style={{
                    left: (layout.slots.find((slot) => slot.index === menu)?.x ?? layout.addX) + 40,
                    top: layout.slots.find((slot) => slot.index === menu)?.y ?? layout.addY,
                  }}
                  onPointerDown={(event) => event.stopPropagation()}
                >
                  {ADD.map((type) => {
                    const pick = blockPick(type);
                    return (
                      <button key={type} type="button" onClick={() => addAt(menu, type)}>
                        <strong>{pick.name}</strong>
                        <span>{pick.blurb}</span>
                      </button>
                    );
                  })}
                </div>
              ) : null}
            </div>
          </div>
          {macro && blocks.length ? (
            <GraphPreview
              trigger={trigger}
              blocks={blocks}
              playMode={mode}
              focusId={picked}
              focusNonce={previewNonce}
              onScene={setFlowNode}
              onPick={previewNode}
            />
          ) : null}
        </div>
      ) : (
        <div className="graph-body is-edit">
          <nav className="graph-edit-list" aria-label="Blocks">
            {macro ? (
              <button type="button" className="graph-assign" onClick={() => setCapture({ slot: "bind" })}>
                <KeyFace label={trigger} empty={!trigger} />
                <span>{trigger || "Assign trigger"}</span>
              </button>
            ) : null}
            {blocks.map((block) => (
              <button
                key={block.id}
                type="button"
                className={picked === block.id ? "is-on" : undefined}
                disabled={!!live && picked !== block.id}
                onClick={() => setPicked(block.id)}
              >
                <BlockLead
                  type={block.type}
                  trigger={trigger}
                  beforeMs={block.type === "ifShort" ? block.underMs : undefined}
                />
                {block.type !== "wait" && block.type !== "tapHold" && block.steps.length ? <em>{block.steps.length}</em> : null}
              </button>
            ))}
          </nav>
          <div className="graph-edit">
            {selectedBlock ? (
              <>
                <header className="graph-edit-head">
                  <h3>
                    <BlockLead
                      type={selectedBlock.type}
                      trigger={trigger}
                      beforeMs={selectedBlock.type === "ifShort" ? selectedBlock.underMs : undefined}
                    />
                  </h3>
                  <div className="graph-edit-row">
                    <button
                      type="button"
                      className="ghost"
                      onClick={() => {
                        const copy = cloneBlock(selectedBlock);
                        const next = blocks.slice();
                        next.splice(selectedIndex + 1, 0, copy);
                        commit(next, copy.id);
                      }}
                    >
                      Duplicate
                    </button>
                    <button type="button" className="ghost is-danger" onClick={() => setWipe("block")}>
                      Remove
                    </button>
                  </div>
                </header>
                <BlockFields
                  block={selectedBlock}
                  macros={others}
                  onChange={(next) => updateBlock(selectedBlock.id, next)}
                  onCapture={(slot) => setCapture({ slot })}
                />
                {canRecord ? (
                  <RecordSurface
                    resetKey={`${macro?.id ?? ""}:${selectedBlock.id}`}
                    steps={selectedBlock.steps}
                    macros={others}
                    onLive={setLive}
                    onSteps={(steps) => updateBlock(selectedBlock.id, { ...selectedBlock, steps })}
                    onCapture={(recording) => {
                      const extra = compileRecording(recording);
                      updateBlock(selectedBlock.id, { ...selectedBlock, steps: [...selectedBlock.steps, ...extra] });
                    }}
                    onClear={() => updateBlock(selectedBlock.id, { ...selectedBlock, steps: [] })}
                  />
                ) : null}
              </>
            ) : null}
          </div>
        </div>
      )}
      {capture && macro ? (
        <Capture
          title={capture.slot === "bind" ? "Assign trigger" : ""}
          onCancel={() => setCapture(null)}
          onPick={(pickedKey) => {
            if (capture.slot === "bind") {
              onChange({
                ...macro,
                trigger: {
                  kind: pickedKey.kind === "key" ? "key" : pickedKey.kind === "side" ? "side" : "mouse",
                  button: pickedKey.button,
                },
              });
              setCapture(null);
              return;
            }
            if (!selectedBlock) {
              setCapture(null);
              return;
            }
            if (selectedBlock.type !== "wait" && selectedBlock.type !== "tapHold" && capture.slot === "step") {
              const kind = pickedKey.kind === "key" ? "key" : "mouse";
              updateBlock(selectedBlock.id, { ...selectedBlock, steps: [...selectedBlock.steps, ...pressPair(kind, pickedKey.button)] });
            }
            if (selectedBlock.type === "tapHold" && capture.slot === "hold")
              updateBlock(selectedBlock.id, { ...selectedBlock, key: pickedKey.button });
            if (selectedBlock.type === "tapHold" && capture.slot === "watch" && !selectedBlock.watch.includes(pickedKey.button))
              updateBlock(selectedBlock.id, { ...selectedBlock, watch: [...selectedBlock.watch, pickedKey.button] });
            setCapture(null);
          }}
        />
      ) : null}
      {wipe && macro ? (
        <Confirm
          title={wipe === "macro" ? `Delete ${macro.name}?` : "Remove this block?"}
          action={wipe === "macro" ? "Delete" : "Remove"}
          onCancel={() => setWipe(null)}
          onConfirm={() => {
            if (wipe === "macro") onDelete();
            else dropBlock();
            setWipe(null);
          }}
        />
      ) : null}
    </div>
  );
}
