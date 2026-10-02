import { useEffect, useLayoutEffect, useRef, useState } from "react";
import { createPortal } from "react-dom";
import { FieldSelect } from "./FieldSelect";
import { NumberField } from "./NumberField";
import { keyLabel } from "../keyboard";
import { pressLabel } from "../recording";
import { MIN_REPEAT_MS, type Macro, type PlayMode } from "../profile";

const PLAY = [
  { value: "once", label: "Play once" },
  { value: "repeat", label: "Repeat" },
  { value: "whileHeld", label: "While pressed" },
  { value: "toggle", label: "Toggle" },
  { value: "onRelease", label: "On release" },
];

export function KeyMenu({
  code,
  label,
  x,
  y,
  macro,
  macros,
  onClose,
  onNew,
  onEdit,
  onClear,
  onAssign,
  onPlay,
  onGap,
  onTimes,
}: {
  code: string;
  label: string;
  x: number;
  y: number;
  macro: Macro | null;
  macros: Macro[];
  onClose: () => void;
  onNew: () => void;
  onEdit: () => void;
  onClear: () => void;
  onAssign: (macroId: string) => void;
  onPlay: (mode: PlayMode) => void;
  onGap: (ms: number) => void;
  onTimes: (count: number) => void;
}) {
  const root = useRef<HTMLDivElement>(null);
  const mode = macro?.playMode ?? "once";
  const timed = mode === "repeat" || mode === "whileHeld" || mode === "toggle";
  const [pos, setPos] = useState({ left: 8, top: 8 });

  useLayoutEffect(() => {
    const el = root.current;
    const pad = 12;
    const viewH = window.visualViewport?.height ?? window.innerHeight;
    const viewW = window.visualViewport?.width ?? window.innerWidth;
    const w = el?.offsetWidth ?? 260;
    const h = el?.offsetHeight ?? 220;
    const left = Math.max(pad, Math.min(x, viewW - w - pad));
    const high = Math.max(pad, Math.round(viewH * 0.2));
    let top = Math.min(Math.max(pad, y), high);
    if (top + h > viewH - pad) top = Math.max(pad, viewH - h - pad);
    setPos({ left, top });
  }, [x, y, code, macro?.id, mode, timed]);

  useEffect(() => {
    const onDoc = (event: PointerEvent) => {
      const target = event.target as Node;
      if (root.current?.contains(target)) return;
      if (target instanceof Element && target.closest(".field-menu")) return;
      onClose();
    };
    const onKey = (event: KeyboardEvent) => {
      if (event.key !== "Escape") return;
      if (root.current?.querySelector(".field-select-btn.is-open")) return;
      onClose();
    };
    document.addEventListener("pointerdown", onDoc);
    document.addEventListener("keydown", onKey);
    return () => {
      document.removeEventListener("pointerdown", onDoc);
      document.removeEventListener("keydown", onKey);
    };
  }, [onClose]);

  return createPortal(
    <div className="key-menu" ref={root} style={{ left: pos.left, top: pos.top }} role="menu" aria-label={`${label} assignment`}>
      <header>
        <strong>{label}</strong>
        {macro ? <em>{macro.name}</em> : null}
      </header>
      {code === "Pause" ? null : (
        <>
          {macro ? (
            <button type="button" className="menu-row" onClick={onEdit}>
              Macro Editor
            </button>
          ) : (
            <button type="button" className="menu-row" onClick={onNew}>
              New macro
            </button>
          )}
          {macro ? (
            <button type="button" className="menu-row" onClick={onClear}>
              Clear
            </button>
          ) : null}
          <div className="menu-play">
            <FieldSelect
              label="Macro"
              ariaLabel="Macro"
              value={macro?.id ?? ""}
              placeholder="None"
              options={[
                { value: "", label: "None" },
                ...macros.map((item) => ({
                  value: item.id,
                  label: macroChoice(item, code),
                })),
              ]}
              onChange={onAssign}
            />
            <FieldSelect
              label="Playback"
              ariaLabel="Playback"
              value={mode}
              options={PLAY}
              onChange={(value) => onPlay(value as PlayMode)}
            />
            {mode === "repeat" ? (
              <label className="menu-delay">
                Times
                <NumberField
                  min={1}
                  value={macro?.repeatCount ?? 1}
                  ariaLabel="Repeat count"
                  onChange={onTimes}
                />
              </label>
            ) : null}
            {timed && (!macro || macro.basic) ? (
              <label className="menu-delay">
                Repeat
                <span className="menu-ms">
                  <NumberField
                    min={0}
                    value={macro?.gapMs ?? MIN_REPEAT_MS}
                    ariaLabel="Repeat delay"
                    onChange={onGap}
                  />
                  <i>ms</i>
                </span>
              </label>
            ) : null}
          </div>
        </>
      )}
    </div>,
    document.body,
  );
}

function macroChoice(item: Macro, code: string): string {
  const name = item.name.trim() || "Untitled";
  if (item.trigger.button && item.trigger.button !== code) {
    const where = item.trigger.kind === "key" ? keyLabel(item.trigger.button) : pressLabel(item.trigger.button);
    return `${name} · ${where}`;
  }
  return name;
}
