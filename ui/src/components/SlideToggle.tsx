import { useState } from "react";

type Opt = { id: string; label: string; disabled?: boolean };

export function SlideToggle({
  value,
  options,
  onChange,
  label,
}: {
  value: string;
  options: readonly [Opt, Opt];
  onChange: (id: string) => void;
  label: string;
}) {
  const on = Math.max(0, options.findIndex((item) => item.id === value));
  const [motion, setMotion] = useState({ from: on, to: on, play: 0 });
  if (motion.to !== on) setMotion({ from: motion.to, to: on, play: motion.play + 1 });
  const still = motion.from === motion.to;

  return (
    <div className="slide-toggle" role="group" aria-label={label}>
      <i
        key={motion.play}
        aria-hidden="true"
        className={still ? "is-still" : undefined}
        style={{ ["--from" as string]: String(motion.from), ["--on" as string]: String(motion.to) }}
      />
      {options.map((item) => (
        <button
          key={item.id}
          type="button"
          className={item.id === value ? "is-on" : undefined}
          aria-pressed={item.id === value}
          disabled={item.disabled}
          onClick={() => onChange(item.id)}
        >
          {item.label}
        </button>
      ))}
    </div>
  );
}
