import { useEffect } from "react";
import { createPortal } from "react-dom";

export function Confirm({
  title,
  note,
  action = "Delete",
  onConfirm,
  onCancel,
}: {
  title: string;
  note?: string;
  action?: string;
  onConfirm: () => void;
  onCancel: () => void;
}) {
  useEffect(() => {
    const onKey = (event: KeyboardEvent) => {
      if (event.repeat) return;
      if (event.key === "Escape") {
        event.preventDefault();
        onCancel();
        return;
      }
      if (event.key === "Enter") {
        event.preventDefault();
        onConfirm();
      }
    };
    window.addEventListener("keydown", onKey, true);
    return () => window.removeEventListener("keydown", onKey, true);
  }, [onCancel, onConfirm]);

  return createPortal(
    <div className="confirm">
      <div className="confirm-card">
        <p>{title}</p>
        {note ? <small className="confirm-note">{note}</small> : null}
        <div className="confirm-row">
          <button type="button" onClick={onCancel}>
            Cancel
          </button>
          <button type="button" className="is-danger" onClick={onConfirm}>
            {action}
          </button>
        </div>
      </div>
    </div>,
    document.body,
  );
}
