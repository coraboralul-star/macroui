import { useEffect, useId, useLayoutEffect, useRef, useState } from "react";
import { createPortal } from "react-dom";

export type Option = { value: string; label: string };

type Box = { top: number; left: number; width: number; maxH: number };
type Prefer = "up" | "down" | "auto";

function viewBox() {
  const view = window.visualViewport;
  if (view) return { top: view.offsetTop, left: view.offsetLeft, width: view.width, height: view.height };
  return { top: 0, left: 0, width: window.innerWidth, height: window.innerHeight };
}

export function FieldSelect({
  label,
  ariaLabel,
  value,
  options,
  placeholder = "Choose",
  prefer = "auto",
  onChange,
}: {
  label?: string;
  ariaLabel: string;
  value: string;
  options: Option[];
  placeholder?: string;
  prefer?: Prefer;
  onChange: (value: string) => void;
}) {
  const [open, setOpen] = useState(false);
  const [box, setBox] = useState<Box | null>(null);
  const root = useRef<HTMLDivElement>(null);
  const btn = useRef<HTMLButtonElement>(null);
  const menu = useRef<HTMLUListElement>(null);
  const id = useId();
  const current = options.find((option) => option.value === value);

  const place = () => {
    const el = btn.current;
    if (!el) return;
    const r = el.getBoundingClientRect();
    const view = viewBox();
    const pad = 10;
    const gap = 4;
    const width = Math.min(Math.max(r.width, 168), view.width - pad * 2);
    const below = view.top + view.height - pad - (r.bottom + gap);
    const above = r.top - gap - (view.top + pad);
    const want = Math.min(320, options.length * 34 + 20);
    let flip = prefer === "up";
    if (prefer === "auto") flip = below < Math.min(want, 220) && above > 88;
    if (prefer === "down") flip = below < 88 && above > below;
    if (flip && above < 88 && below > above) flip = false;
    const room = Math.max(88, flip ? above : below);
    let maxH = Math.min(320, room);
    let top = flip ? r.top - gap - maxH : r.bottom + gap;
    let left = r.left;
    const rightLimit = view.left + view.width - pad;
    const bottomLimit = view.top + view.height - pad;
    const topLimit = view.top + pad;
    if (left + width > rightLimit) left = rightLimit - width;
    if (left < view.left + pad) left = view.left + pad;
    if (top + maxH > bottomLimit) {
      maxH = Math.max(88, bottomLimit - top);
      if (top + maxH > bottomLimit) top = bottomLimit - maxH;
    }
    if (top < topLimit) {
      top = topLimit;
      maxH = Math.max(88, Math.min(maxH, bottomLimit - top));
    }
    setBox({ top, left, width, maxH });
  };

  useLayoutEffect(() => {
    if (!open) {
      setBox(null);
      return;
    }
    place();
  }, [open, options.length, prefer]);

  useEffect(() => {
    if (!open) return;
    const onDoc = (event: PointerEvent) => {
      const target = event.target as Node;
      if (root.current?.contains(target) || menu.current?.contains(target)) return;
      setOpen(false);
    };
    const onKey = (event: KeyboardEvent) => {
      if (event.key === "Escape") setOpen(false);
    };
    document.addEventListener("pointerdown", onDoc);
    document.addEventListener("keydown", onKey);
    window.addEventListener("resize", place);
    window.addEventListener("scroll", place, true);
    window.visualViewport?.addEventListener("resize", place);
    window.visualViewport?.addEventListener("scroll", place);
    return () => {
      document.removeEventListener("pointerdown", onDoc);
      document.removeEventListener("keydown", onKey);
      window.removeEventListener("resize", place);
      window.removeEventListener("scroll", place, true);
      window.visualViewport?.removeEventListener("resize", place);
      window.visualViewport?.removeEventListener("scroll", place);
    };
  }, [open]);

  return (
    <div className="field-select" ref={root}>
      {label ? <span id={id}>{label}</span> : null}
      <button
        ref={btn}
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
      {open && box
        ? createPortal(
            <ul
              ref={menu}
              className="field-menu is-portal"
              role="listbox"
              aria-label={ariaLabel}
              style={{ top: box.top, left: box.left, width: box.width, maxHeight: box.maxH }}
            >
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
            </ul>,
            document.body,
          )
        : null}
    </div>
  );
}
