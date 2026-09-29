export function Chips({
  label,
  ariaLabel,
  value,
  options,
  onChange,
}: {
  label?: string;
  ariaLabel: string;
  value: string;
  options: { value: string; label: string }[];
  onChange: (value: string) => void;
}) {
  return (
    <div className="chip-field">
      {label ? <span>{label}</span> : null}
      <div className="chips" role="radiogroup" aria-label={ariaLabel}>
        {options.map((option) => (
          <button
            key={option.value}
            type="button"
            role="radio"
            aria-checked={option.value === value}
            className={option.value === value ? "is-on" : ""}
            onClick={() => onChange(option.value)}
          >
            {option.label}
          </button>
        ))}
      </div>
    </div>
  );
}
