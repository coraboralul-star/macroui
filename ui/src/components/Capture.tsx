import { useEffect } from "react";
import { createPortal } from "react-dom";
import { keyFromCode, mouseFromButton } from "../recording";

export type Captured = { kind: "key" | "mouse" | "side"; button: string };

export function Capture({
  title,
  onPick,
  onCancel,
}: {
  title: string;
  onPick: (captured: Captured) => void;
  onCancel: () => void;
}) {
  useEffect(() => {
    const onKey = (event: KeyboardEvent) => {
      if (event.repeat) return;
      event.preventDefault();
      event.stopPropagation();
      if (event.key === "Escape" || event.code === "Escape") {
        onCancel();
        return;
      }
      const button = keyFromCode(event.code);
      if (!button || button === "escape" || button === "Pause") return;
      onPick({ kind: "key", button });
    };
    const onPointer = (event: PointerEvent) => {
      const target = event.target as HTMLElement | null;
      if (target?.closest?.("[data-capture-cancel]")) return;
      if (event.button === 0 && target?.closest?.(".capture-card")) return;
      const button = mouseFromButton(event.button);
      if (!button || event.type !== "pointerdown") return;
      event.preventDefault();
      event.stopPropagation();
      const kind = button.startsWith("XButton") ? "side" : "mouse";
      onPick({ kind, button });
    };
    window.addEventListener("keydown", onKey, true);
    window.addEventListener("pointerdown", onPointer, true);
    return () => {
      window.removeEventListener("keydown", onKey, true);
      window.removeEventListener("pointerdown", onPointer, true);
    };
  }, [onCancel, onPick]);

  return createPortal(
    <div className="capture">
      <div className="capture-card">
        {title ? <strong>{title}</strong> : null}
        <p>Press a key or click</p>
        <button type="button" data-capture-cancel="" onClick={onCancel}>
          Cancel
        </button>
      </div>
    </div>,
    document.body,
  );
}
