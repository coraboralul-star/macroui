import { Fragment, useEffect, useMemo, useRef, useState, type DragEvent, type PointerEvent } from "react";
import { post } from "../bridge";
import { appendSteps, cloneBlock, insertStepBlock, insertTyped, moveBlockAfter, reorderBlocks, splitFrom, takesSteps, allowsScanWait } from "../blocks";
import { pressPair, applyDelayMode } from "../eventLane";
import { LIBRARY, LIBRARY_MIME, MENU_GROUPS, libraryItem, librarySteps, matchInput } from "../library";
import { compileRecording, type RecEvent } from "../recording";
import { effectivePlayMode, GRAPH_NODE_H, GRAPH_NODE_W, graphLayout, place, shownBlocks, usesReleaseStop } from "../macroFlow";
import type { Block, Macro, Step } from "../profile";
import { BlockFields } from "./BlockFields";
import { BlockLead } from "./BlockLead";
import { Capture } from "./Capture";
import { Confirm } from "./Confirm";
import { GraphPreview } from "./GraphPreview";
import { KeyFace } from "./KeyFace";
import { Library } from "./Library";
import { RecordSurface } from "./RecordSurface";
import { DelayGear, modeOf, type DelayMode } from "./RecordDeck";
import { ReleaseField } from "./ReleaseField";
import { SlideToggle } from "./SlideToggle";

/** Where a library item lands: inside a block, or between blocks. */
type Spot = { block: string } | { at: number };

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
  const [libOver, setLibOver] = useState<string | null>(null);
  const [libPick, setLibPick] = useState<{ id: string; spot: Spot } | null>(null);
  const [delayMode, setDelayMode] = useState<DelayMode>("recorded");
  const [fixedMs, setFixedMs] = useState(20);
  const [wire, setWire] = useState<{ from: string; x: number; y: number; over: string | null } | null>(null);
  const panDrag = useRef<{ x: number; y: number; px: number; py: number } | null>(null);
  const worldRef = useRef<HTMLDivElement>(null);
  const wireRef = useRef(wire);
  wireRef.current = wire;

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
    setDelayMode(modeOf(macro?.recording ?? null));
    const hold = macro?.recording?.holdMs;
    setFixedMs(hold && hold > 1 ? hold : 20);
  }, [macro?.id, macro?.recording]);

  useEffect(() => {
    if (menu == null) return;
    const close = () => setMenu(null);
    window.addEventListener("pointerdown", close);
    return () => window.removeEventListener("pointerdown", close);
  }, [menu]);

  const layout = useMemo(() => graphLayout(blocks), [blocks]);

  const commit = (next: Block[], pick?: string, patch?: Partial<Macro>) => {
    if (!macro) return;
    onChange({
      ...macro,
      advanced: true,
      basic: false,
      blocks: next,
      steps: [],
      playMode: next.some((block) => block.type === "tapHold") ? "whileHeld" : macro.playMode,
      releaseStop: macro.releaseStop === "finish" ? "finish" : "nextUp",
      ...patch,
    });
    if (pick) setPicked(pick);
  };

  const worldPoint = (event: { clientX: number; clientY: number }) => {
    const rect = worldRef.current?.getBoundingClientRect();
    if (!rect) return { x: 0, y: 0 };
    return { x: event.clientX - rect.left, y: event.clientY - rect.top };
  };
  const blocksRef = useRef(blocks);
  const commitRef = useRef(commit);
  blocksRef.current = blocks;
  commitRef.current = commit;

  useEffect(() => {
    if (!wire) return;
    const move = (event: globalThis.PointerEvent) => {
      const point = worldPoint(event);
      const hit = document.elementFromPoint(event.clientX, event.clientY);
      const node = hit instanceof Element ? hit.closest("[data-node]") : null;
      const over = node instanceof HTMLElement ? node.dataset.node ?? null : null;
      setWire((current) => (current ? { ...current, x: point.x, y: point.y, over: over && over !== current.from ? over : null } : current));
    };
    const up = () => {
      const current = wireRef.current;
      setWire(null);
      if (!current?.over) return;
      commitRef.current(moveBlockAfter(blocksRef.current, current.over, current.from), current.over);
    };
    window.addEventListener("pointermove", move);
    window.addEventListener("pointerup", up);
    return () => {
      window.removeEventListener("pointermove", move);
      window.removeEventListener("pointerup", up);
    };
  }, [wire?.from]);

  const selectedBlock = blocks.find((block) => block.id === picked) ?? null;
  const selectedIndex = selectedBlock ? blocks.findIndex((block) => block.id === selectedBlock.id) : -1;

  const updateBlock = (id: string, next: Block) => commit(blocks.map((item) => (item.id === id ? next : item)));

  const applyDelay = (mode: DelayMode, ms = fixedMs) => {
    if (!macro || !selectedBlock || !takesSteps(selectedBlock)) return;
    setDelayMode(mode);
    if (mode === "recorded") {
      if (!macro.recording) return;
      const recording = { ...macro.recording, timing: "played" as const };
      commit(
        blocks.map((block) => (block.id === selectedBlock.id && takesSteps(block) ? { ...block, steps: compileRecording(recording) } : block)),
        selectedBlock.id,
        { recording },
      );
      return;
    }
    if (mode === "custom") return;
    const amount = mode === "none" ? 0 : Math.max(0, ms);
    const steps = applyDelayMode(selectedBlock.steps, mode, amount);
    const recording = macro.recording
      ? { ...macro.recording, timing: "custom" as const, holdMs: mode === "none" ? 1 : Math.max(1, amount), intervalMs: mode === "fixed" ? Math.max(1, amount) : 0 }
      : macro.recording;
    commit(
      blocks.map((block) => (block.id === selectedBlock.id && takesSteps(block) ? { ...block, steps } : block)),
      selectedBlock.id,
      { recording },
    );
  };

  const dropBlock = () => {
    if (!selectedBlock) return;
    const next = blocks.filter((item) => item.id !== selectedBlock.id);
    commit(next, next[Math.max(0, selectedIndex - 1)]?.id ?? "");
  };

  const dropOn = (index: number) => (event: DragEvent) => {
    event.preventDefault();
    const fromId = dragId || event.dataTransfer.getData("text/plain");
    setDragId(null);
    setDropAt(null);
    if (!fromId) return;
    commit(reorderBlocks(blocks, fromId, index), fromId);
  };

  const fromLib = (event: DragEvent) => event.dataTransfer.types.includes(LIBRARY_MIME);

  const slotOf = (spot: Spot) => ("at" in spot ? spot.at : blocks.findIndex((block) => block.id === spot.block) + 1);

  /** Steps go into the block they landed on. Anything else gets a Run Once to live in. */
  const addSteps = (steps: Step[], spot: Spot) => {
    if (!steps.length || !macro) return;
    const host = "block" in spot ? blocks.find((block) => block.id === spot.block) ?? null : null;
    const scan = steps.some((step) => step.type === "scanWait");
    if (scan && macro.playMode === "onRelease") return;
    if (host && takesSteps(host)) {
      if (scan && !allowsScanWait(host, macro.playMode)) {
        const next = insertStepBlock(blocks, slotOf(spot), steps);
        commit(next.blocks, next.pick);
        return;
      }
      commit(
        blocks.map((block) => (block.id === host.id ? appendSteps(block, steps) : block)),
        host.id,
      );
      return;
    }
    const next = insertStepBlock(blocks, slotOf(spot), steps);
    commit(next.blocks, next.pick);
  };

  /** One path for every insert: the + menus, the Library clicks, and the drops. */
  const useItem = (id: string, spot: Spot, ask = false) => {
    const item = libraryItem(id);
    if (!item || !macro) return;
    if (item.kind === "block") {
      const next = insertTyped(blocks, slotOf(spot), item.block);
      commit(next.blocks, next.pick);
      return;
    }
    if (item.kind === "step") {
      if (ask && item.pick) {
        setLibPick({ id, spot });
        return;
      }
      addSteps(item.create(), spot);
      return;
    }
    if (item.source === "last") {
      addSteps(macro.recording ? compileRecording(macro.recording) : [], spot);
      return;
    }
    const host = "block" in spot ? blocks.find((block) => block.id === spot.block) ?? null : null;
    if (host && takesSteps(host)) {
      openEdit(host.id);
      return;
    }
    const next = insertStepBlock(blocks, slotOf(spot), []);
    commit(next.blocks, next.pick);
    setTab("edit");
  };

  /** Library clicks land in the selected block, or right after it. */
  const pickItem = (id: string) => {
    const spot: Spot =
      selectedBlock && takesSteps(selectedBlock)
        ? { block: selectedBlock.id }
        : { at: selectedIndex >= 0 ? selectedIndex + 1 : blocks.length };
    useItem(id, spot, true);
  };

  const dropItem = (spot: Spot) => (event: DragEvent) => {
    const id = event.dataTransfer.getData(LIBRARY_MIME);
    if (!id) return false;
    event.preventDefault();
    event.stopPropagation();
    setLibOver(null);
    useItem(id, spot);
    return true;
  };

  const overItem = (key: string) => (event: DragEvent) => {
    if (!fromLib(event)) return false;
    event.preventDefault();
    event.stopPropagation();
    event.dataTransfer.dropEffect = "copy";
    setLibOver(key);
    return true;
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
    if (target.closest(".graph-node, .graph-node-x, .graph-flow, .graph-add, .graph-add-menu, .graph-insert, .graph-wire, .graph-split")) return;
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
      <header
        className="graph-bar"
        onMouseDown={(event) => {
          const target = event.target as HTMLElement;
          if (target.closest("button, input, a, select, textarea, .rel-field, .gear-wrap, .slide-toggle")) return;
          post({ type: "window", action: "drag" });
        }}
      >
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
        <DelayGear
          mode={delayMode}
          fixedMs={fixedMs}
          canRecord={!!macro?.recording}
          disabled={!selectedBlock || !takesSteps(selectedBlock)}
          resetKey={`${macro?.id ?? ""}:${selectedBlock?.id ?? ""}`}
          onMode={applyDelay}
          onFixed={(ms) => {
            setFixedMs(ms);
            applyDelay("fixed", ms);
          }}
        />
        {macro && usesReleaseStop(macro) ? (
          <ReleaseField compact label="Macro on release" value={macro.releaseStop} onChange={(releaseStop) => onChange({ ...macro, releaseStop: releaseStop ?? "nextUp" })} />
        ) : null}
        <button type="button" className={macro?.busy ? "is-on" : "ghost"} disabled={!macro} aria-label="Skip if running" onClick={() => macro && onChange({ ...macro, busy: !macro.busy })}>
          Skip
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
            className={`graph-canvas${libOver === "canvas" ? " is-drop" : ""}`}
            onPointerDown={onCanvasDown}
            onPointerMove={onCanvasMove}
            onPointerUp={() => {
              panDrag.current = null;
            }}
            onDragStart={(event) => {
              if (!(event.target as HTMLElement).closest(".graph-node")) event.preventDefault();
            }}
            onDragOver={overItem("canvas")}
            onDragLeave={() => setLibOver(null)}
            onDrop={dropItem({ at: blocks.length })}
          >
            <div className="graph-world" ref={worldRef} style={{ transform: `translate(${pan.x}px, ${pan.y}px)`, width, height }}>
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
                  const drawn = wirePath(from, to, link.side);
                  return (
                    <path
                      key={`${link.from}-${link.to}-${link.fork ?? "main"}`}
                      className={`graph-wire${link.fork ? ` is-${link.fork}` : ""}`}
                      d={drawn.d}
                      markerEnd="url(#graph-arrow)"
                      onPointerDown={(event) => {
                        if (event.button !== 0) return;
                        event.stopPropagation();
                        event.preventDefault();
                        const point = worldPoint(event);
                        setWire({ from: link.from, x: point.x, y: point.y, over: null });
                      }}
                    />
                  );
                })}
                {wire ? (
                  <path
                    className="graph-wire is-drag"
                    d={dragWire(layout.nodes.find((node) => node.id === wire.from), wire.x, wire.y)}
                  />
                ) : null}
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
                    className={`graph-node block-${node.kind}${picked === node.id ? " is-on" : ""}${flowNode === node.id ? " is-preview" : ""}${dragId === node.id ? " is-lift" : ""}${libOver === `node:${node.id}` || wire?.over === node.id ? " is-drop" : ""}`}
                    style={{ left: node.x, top: node.y, width: GRAPH_NODE_W, height: GRAPH_NODE_H }}
                    data-node={movable ? node.id : undefined}
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
                      if (movable && overItem(`node:${node.id}`)(event)) return;
                      if (!movable || !dragId) return;
                      event.preventDefault();
                      setDropAt(index);
                    }}
                    onDragLeave={() => setLibOver(null)}
                    onDrop={(event) => {
                      if (!movable) return;
                      if (dropItem({ block: node.id })(event)) return;
                      dropOn(index)(event);
                    }}
                  >
                    <strong>
                      {node.kind === "heldPath" || !item ? (
                        <>Held past {node.underMs} ms</>
                      ) : (
                        <BlockLead block={item} trigger={trigger} />
                      )}
                    </strong>
                    {node.hint ? <em>{node.hint}</em> : null}
                  </button>
                  {movable ? (
                    <button
                      type="button"
                      className="graph-split"
                      aria-label="Split connector"
                      style={{ left: node.x + GRAPH_NODE_W - 8, top: node.y + GRAPH_NODE_H - 10 }}
                      onPointerDown={(event) => event.stopPropagation()}
                      onClick={(event) => {
                        event.stopPropagation();
                        const next = splitFrom(blocks, node.id);
                        commit(next.blocks, next.pick);
                      }}
                    >
                      Split
                    </button>
                  ) : null}
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
                  className={`graph-insert${dropAt === slot.index ? " is-hot" : ""}${libOver === `slot:${slot.index}` ? " is-drop" : ""}`}
                  style={{ left: slot.x, top: slot.y }}
                  aria-label={`Insert at step ${slot.index + 1}`}
                  onPointerDown={(event) => event.stopPropagation()}
                  onClick={(event) => {
                    event.stopPropagation();
                    setMenu((open) => (open === slot.index ? null : slot.index));
                  }}
                  onDragOver={(event) => {
                    if (overItem(`slot:${slot.index}`)(event)) return;
                    if (!dragId) return;
                    event.preventDefault();
                    setDropAt(slot.index);
                  }}
                  onDragLeave={() => setLibOver(null)}
                  onDrop={(event) => {
                    if (dropItem({ at: slot.index })(event)) return;
                    dropOn(slot.index)(event);
                  }}
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
                  {MENU_GROUPS.map((group) => {
                    const items = LIBRARY.filter(
                      (item) =>
                        item.group === group &&
                        (item.id !== "lastRecording" || !!macro?.recording) &&
                        (item.id !== "scanWait" || (macro?.playMode !== "onRelease")),
                    );
                    if (!items.length) return null;
                    return (
                      <Fragment key={group}>
                        <p className="ctx-label">{group}</p>
                        {items.map((item) => (
                          <button
                            key={item.id}
                            type="button"
                            title={item.detail}
                            onClick={() => {
                              setMenu(null);
                              useItem(item.id, { at: menu }, true);
                            }}
                          >
                            {item.label}
                          </button>
                        ))}
                      </Fragment>
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
          <Library onPick={pickItem} hasRecording={!!macro?.recording} />
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
                <BlockLead block={block} trigger={trigger} />
                {takesSteps(block) && block.steps.length ? <em>{block.steps.length}</em> : null}
              </button>
            ))}
          </nav>
          <div className="graph-edit">
            {selectedBlock ? (
              <>
                <header className="graph-edit-head">
                  <h3>
                    <BlockLead block={selectedBlock} trigger={trigger} />
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
                {selectedBlock && takesSteps(selectedBlock) ? (
                  <RecordSurface
                    resetKey={`${macro?.id ?? ""}:${selectedBlock.id}`}
                    steps={selectedBlock.steps}
                    macros={others}
                    allowScanWait={allowsScanWait(selectedBlock, macro?.playMode)}
                    onLive={setLive}
                    onSteps={(steps) => {
                      setDelayMode("custom");
                      updateBlock(selectedBlock.id, { ...selectedBlock, steps });
                    }}
                    onCapture={(recording) => {
                      const extra = compileRecording(recording);
                      commit(
                        blocks.map((block) => (block.id === selectedBlock.id ? appendSteps(block, extra) : block)),
                        selectedBlock.id,
                        { recording },
                      );
                    }}
                    onClear={() => updateBlock(selectedBlock.id, { ...selectedBlock, steps: [] })}
                  />
                ) : null}
              </>
            ) : null}
          </div>
          <Library onPick={pickItem} hasRecording={!!macro?.recording} />
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
            if (takesSteps(selectedBlock) && capture.slot === "step") {
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
      {libPick ? (
        <Capture
          title={libraryItem(libPick.id)?.label ?? ""}
          onCancel={() => setLibPick(null)}
          onPick={(pickedKey) => {
            const kind = pickedKey.kind === "key" ? "key" : "mouse";
            addSteps(librarySteps(matchInput(libPick.id, kind), pickedKey.button), libPick.spot);
            setLibPick(null);
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

function wirePath(from: { x: number; y: number }, to: { x: number; y: number }, side?: boolean) {
  if (side) {
    const x1 = from.x + GRAPH_NODE_W;
    const y1 = from.y + GRAPH_NODE_H / 2;
    const x2 = to.x;
    const y2 = to.y + GRAPH_NODE_H / 2;
    const mid = (x1 + x2) / 2;
    return { d: `M ${x1} ${y1} C ${mid} ${y1}, ${mid} ${y2}, ${x2} ${y2}` };
  }
  const x1 = from.x + GRAPH_NODE_W / 2;
  const y1 = from.y + GRAPH_NODE_H;
  const x2 = to.x + GRAPH_NODE_W / 2;
  const y2 = to.y;
  const mid = (y1 + y2) / 2;
  return { d: `M ${x1} ${y1} C ${x1} ${mid}, ${x2} ${mid}, ${x2} ${y2}` };
}

function dragWire(from: { x: number; y: number } | undefined, x: number, y: number) {
  if (!from) return "";
  const x1 = from.x + GRAPH_NODE_W / 2;
  const y1 = from.y + GRAPH_NODE_H;
  return `M ${x1} ${y1} L ${x} ${y}`;
}
