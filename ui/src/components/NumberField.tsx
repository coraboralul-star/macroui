import { useEffect, useRef, useState } from "react";

export function NumberField({
  value,
  min,
  signed = false,
  ariaLabel,
  className,
  autoFocus,
  onChange,
  onBlur,
}: {
  value: number;
  min?: number;
  signed?: boolean;
  ariaLabel: string;
  className?: string;
  autoFocus?: boolean;
  onChange: (value: number) => void;
  onBlur?: () => void;
}) {
  const focused = useRef(false);
  const [text, setText] = useState(String(value));
  useEffect(() => {
    if (!focused.current) setText(String(value));
  }, [value]);

  const commit = (raw: string, persist: boolean) => {
    if (raw === "" || (signed && raw === "-")) {
      if (persist) {
        const fallback = min ?? 0;
        onChange(fallback);
        setText(String(fallback));
      }
      return;
    }
    if (!(signed ? /^-?\d+$/ : /^\d+$/).test(raw)) return;
    let next = Math.round(Number(raw));
    if (!Number.isFinite(next)) return;
    if (min != null) next = Math.max(min, next);
    onChange(next);
    if (persist) setText(String(next));
  };

  return (
    <input
      className={className}
      aria-label={ariaLabel}
      autoFocus={autoFocus}
      type="text"
      inputMode={signed ? "text" : "numeric"}
      size={5}
      value={text}
      onFocus={() => {
        focused.current = true;
        setText(String(value));
      }}
      onChange={(event) => {
        const raw = event.target.value;
        if (raw === "" || (signed && raw === "-")) {
          setText(raw);
          return;
        }
        if (!(signed ? /^-?\d+$/ : /^\d+$/).test(raw)) return;
        setText(raw);
        commit(raw, false);
      }}
      onBlur={() => {
        focused.current = false;
        commit(text, true);
        onBlur?.();
      }}
      onKeyDown={(event) => {
        if (event.key === "Enter") event.currentTarget.blur();
      }}
    />
  );
}
