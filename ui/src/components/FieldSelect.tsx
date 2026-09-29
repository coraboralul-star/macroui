import { useEffect, useId, useRef, useState } from "react";

export type Option = { value: string; label: string };

export function FieldSelect({
  label,
  ariaLabel,
  value,
  options,
  placeholder = "Choose",
  onChange,
}: {
  label?: string;
  ariaLabel: string;
  value: string;
  options: Option[];
  placeholder?: string;
  onChange: (value: string) => void;
}) {
  const [open, setOpen] = useState(false);
  const root = useRef<HTMLDivElement>(null);
  const id = useId();
  const current = options.find((option) => option.value === value);

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
    <div className="field-select" ref={root}>
      {label ? <span id={id}>{label}</span> : null}
      <button
        type="button"
        className={`field-select-btn${open ? " is-open" : ""}`}
        aria-labelledby={label ? id : undefined}
        aria-label={label ? undefined : ariaLabel}
        aria-expanded={open}
        onClick={() => setOpen((next) => !next)}
      >
        <em>{current?.label ?? placeholder}</em>
        <i aria-hidden="true" />
      </button>
      {open ? (
        <ul className="field-menu" role="listbox" aria-label={ariaLabel}>
          {options.map((option) => (
            <li key={option.value || option.label}>
              <button
                type="button"
                role="option"
                aria-selected={option.value === value}
                className={option.value === value ? "is-selected" : ""}
                onClick={() => {
                  onChange(option.value);
                  setOpen(false);
                }}
              >
                {option.label}
              </button>
            </li>
          ))}
        </ul>
      ) : null}
    </div>
  );
}
