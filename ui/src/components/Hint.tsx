import { useState } from "react";

export function Hint({ text }: { text: string }) {
  const [open, setOpen] = useState(false);
  return (
    <span className="hint-wrap">
      <button
        type="button"
        className="hint"
        title={text}
        aria-label={text}
        onClick={(event) => {
          event.preventDefault();
          event.stopPropagation();
          setOpen((on) => !on);
        }}
        onBlur={() => setOpen(false)}
      >
        ?
      </button>
      {open ? <span className="hint-tip">{text}</span> : null}
    </span>
  );
}
