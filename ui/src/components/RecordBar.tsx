import { useEffect, useRef, useState, type ReactNode } from "react";
import { post } from "../bridge";
import { closeHeld, keyFromCode, mouseFromButton, type RecEvent, type Recording } from "../recording";

export function RecordBar({
  onCapture,
  onLive,
  onClear,
  clearDisabled,
  extras,
  dock,
  onRevert,
  revertDisabled,
}: {
  onCapture: (recording: Recording) => void;
  onLive?: (events: RecEvent[] | null) => void;
  onClear?: () => void;
  clearDisabled?: boolean;
  extras?: ReactNode;
  dock?: boolean;
  onRevert?: () => void;
  revertDisabled?: boolean;
}) {
  const [active, setActive] = useState(false);
  const events = useRef<RecEvent[]>([]);
  const held = useRef(new Set<string>());
  const started = useRef(0);
  const finishRef = useRef<(end: number) => void>(() => {});
  const onCaptureRef = useRef(onCapture);
  const onLiveRef = useRef(onLive);
  onCaptureRef.current = onCapture;
  onLiveRef.current = onLive;

  useEffect(() => {
    if (!active) return;
    const origin = performance.now();
    started.current = origin;
    events.current = [];
    held.current = new Set();
    onLiveRef.current?.([]);
    let done = false;

    const stamp = () => Math.max(0, Math.round(performance.now() - origin));
    const push = (kind: RecEvent["kind"], button: string, down: boolean) => {
      const id = kind + ":" + button;
      if (down) {
        if (held.current.has(id)) return;
        held.current.add(id);
      } else if (!held.current.delete(id)) return;
      events.current = [...events.current, { t: stamp(), kind, button, down }];
      onLiveRef.current?.(events.current);
    };

    const save = (end: number) => {
      if (done) return;
      done = true;
      const closed = closeHeld(events.current, end);
      if (closed.some((event) => event.down)) {
        onCaptureRef.current({ timing: "played", holdMs: 18, intervalMs: 0, events: closed });
      }
    };
    const finish = (end: number) => {
      save(end);
      onLiveRef.current?.(null);
      setActive(false);
    };
    finishRef.current = finish;

    const onKey = (event: KeyboardEvent) => {
      if (event.repeat) return;
      const key = keyFromCode(event.code);
      if (!key) return;
      event.preventDefault();
      event.stopPropagation();
      if (key === "escape") {
        if (event.type === "keydown") finish(stamp());
        return;
      }
      push("key", key, event.type === "keydown");
    };
    const onPointer = (event: PointerEvent) => {
      const target = event.target as HTMLElement | null;
      if (target?.closest?.("[data-chrome], button, input, textarea, select")) return;
      const button = mouseFromButton(event.button);
      if (!button) return;
      push("mouse", button, event.type === "pointerdown");
    };

    window.addEventListener("keydown", onKey, true);
    window.addEventListener("keyup", onKey, true);
    window.addEventListener("pointerdown", onPointer, true);
    window.addEventListener("pointerup", onPointer, true);
    return () => {
      save(stamp());
      onLiveRef.current?.(null);
      finishRef.current = () => {};
      window.removeEventListener("keydown", onKey, true);
      window.removeEventListener("keyup", onKey, true);
      window.removeEventListener("pointerdown", onPointer, true);
      window.removeEventListener("pointerup", onPointer, true);
    };
  }, [active]);

  const toggle = () => {
    if (active) {
      finishRef.current(Math.max(0, Math.round(performance.now() - started.current)));
      return;
    }
    post({ type: "command", action: "stop" });
    setActive(true);
  };

  return (
    <div className={`rec-bar${dock ? " is-dock" : ""}`} data-chrome="">
      <div className="rec-bar-tools">
        {extras}
        {onClear ? (
          <button type="button" className="rec-clear" disabled={active || clearDisabled} onClick={onClear}>
            Clear
          </button>
        ) : null}
      </div>
      <div className="rec-bar-end">
        {onRevert ? (
          <button type="button" className="rec-revert" disabled={revertDisabled} onClick={onRevert}>
            Revert
          </button>
        ) : null}
        <button
          type="button"
          className={`rec-arm${active ? " is-live" : ""}`}
          aria-label={active ? "Stop" : "Start"}
          onClick={toggle}
        >
          <strong>{active ? "Stop" : "Start"}</strong>
          {dock ? null : <span className={`rec-dot${active ? " is-live" : ""}`} aria-hidden="true" />}
        </button>
      </div>
      {dock ? (
        <button
          type="button"
          className={`rec-well${active ? " is-live" : ""}`}
          tabIndex={-1}
          aria-hidden="true"
          onClick={toggle}
        >
          <span className={`rec-dot${active ? " is-live" : ""}`} />
        </button>
      ) : null}
    </div>
  );
}
