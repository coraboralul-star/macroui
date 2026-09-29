import { useEffect, useRef, useState } from "react";
import { createPortal } from "react-dom";
import { FieldSelect } from "./FieldSelect";
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
  const [gap, setGap] = useState(String(macro?.gapMs ?? MIN_REPEAT_MS));
  const [times, setTimes] = useState(String(macro?.repeatCount ?? 1));

  useEffect(() => {
    setGap(String(macro?.gapMs ?? MIN_REPEAT_MS));
    setTimes(String(macro?.repeatCount ?? 1));
  }, [macro?.id, macro?.gapMs, macro?.repeatCount, macro?.playMode]);

  useEffect(() => {
    const onDoc = (event: PointerEvent) => {
      if (root.current?.contains(event.target as Node)) return;
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

  const left = Math.max(8, Math.min(x, window.innerWidth - 276));
  const top = Math.max(8, Math.min(y, window.innerHeight - 340));
  const flip = y > window.innerHeight * 0.62;

  const commitGap = () => {
    const ms = Math.max(0, Math.round(Number(gap)) || 0);
    setGap(String(ms));
    onGap(ms);
  };

  return createPortal(
    <div className={`key-menu${flip ? " is-flip" : ""}`} ref={root} style={{ left, top }} role="menu" aria-label={`${label} assignment`}>
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
                <input
                  type="number"
                  min={1}
                  step={1}
                  value={times}
                  aria-label="Repeat count"
                  onChange={(event) => {
                    setTimes(event.target.value);
                    const count = Math.max(1, Math.round(Number(event.target.value)) || 1);
                    if (count >= 1 && event.target.value !== "") onTimes(count);
                  }}
                  onBlur={() => {
                    const count = Math.max(1, Math.round(Number(times)) || 1);
                    setTimes(String(count));
                    onTimes(count);
                  }}
                />
              </label>
            ) : null}
            {timed && (!macro || macro.basic) ? (
              <label className="menu-delay">
                Repeat
                <span className="menu-ms">
                  <input
                    type="number"
                    min={0}
                    step={1}
                    value={gap}
                    aria-label="Repeat delay"
                    onChange={(event) => {
                      setGap(event.target.value);
                      const ms = Math.round(Number(event.target.value));
                      if (event.target.value !== "" && Number.isFinite(ms) && ms >= 0) onGap(ms);
                    }}
                    onBlur={commitGap}
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
