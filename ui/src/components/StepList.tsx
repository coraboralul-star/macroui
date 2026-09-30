import { useState } from "react";
import type { KeyAction, Macro, Step } from "../profile";
import { GOTO_WHERE, KEY_OPTIONS, MOUSE_OPTIONS, SIDE_OPTIONS, blankStep, normalizeGotoWhere } from "../profile";
import { Chips } from "./Chips";
import { FieldSelect } from "./FieldSelect";

const ACTIONS = [
  { value: "down", label: "Down" },
  { value: "up", label: "Up" },
  { value: "tap", label: "Tap" },
];

const STEP_TYPES: { value: Step["type"]; label: string }[] = [
  { value: "key", label: "Key" },
  { value: "mouse", label: "Click" },
  { value: "move", label: "Move" },
  { value: "goto", label: "Go to" },
  { value: "wait", label: "Wait" },
  { value: "repeat", label: "Repeat" },
  { value: "run", label: "Run" },
];

const QUICK_KEYS = ["w", "a", "s", "d", "space", "shift", "ctrl", "f", "q", "e", "1", "2"];

export function StepList({
  steps,
  macros,
  onChange,
}: {
  steps: Step[];
  macros: Macro[];
  onChange: (steps: Step[]) => void;
}) {
  return (
    <div className="steps">
      {steps.map((step, index) => (
        <StepRow
          key={index}
          step={step}
          macros={macros}
          onChange={(next) => onChange(steps.map((item, i) => (i === index ? next : item)))}
          onRemove={() => onChange(steps.filter((_, i) => i !== index))}
          onUp={index > 0 ? () => onChange(swap(steps, index, index - 1)) : undefined}
          onDown={index < steps.length - 1 ? () => onChange(swap(steps, index, index + 1)) : undefined}
        />
      ))}
      <div className="add-step" role="group" aria-label="Add step">
        {STEP_TYPES.map((type) => (
          <button key={type.value} type="button" onClick={() => onChange([...steps, blankStep(type.value)])}>
            {type.label}
          </button>
        ))}
      </div>
    </div>
  );
}

function StepRow({
  step,
  macros,
  onChange,
  onRemove,
  onUp,
  onDown,
}: {
  step: Step;
  macros: Macro[];
  onChange: (step: Step) => void;
  onRemove: () => void;
  onUp?: () => void;
  onDown?: () => void;
}) {
  return (
    <div className={`step tex${step.type === "repeat" ? " is-repeat" : ""}`}>
      <div className="step-main">
        <span className="step-kind">{label(step)}</span>
        <StepFields step={step} macros={macros} onChange={onChange} />
        <span className="step-actions">
          <button type="button" onClick={onUp} disabled={!onUp} aria-label="Move step up">
            ↑
          </button>
          <button type="button" onClick={onDown} disabled={!onDown} aria-label="Move step down">
            ↓
          </button>
          <button type="button" onClick={onRemove} aria-label="Remove step">
            ×
          </button>
        </span>
      </div>
      {step.type === "repeat" ? (
        <StepList steps={step.steps} macros={macros} onChange={(child) => onChange({ ...step, steps: child })} />
      ) : null}
    </div>
  );
}

function StepFields({
  step,
  macros,
  onChange,
}: {
  step: Step;
  macros: Macro[];
  onChange: (step: Step) => void;
}) {
  if (step.type === "key") {
    return (
      <>
        <Chips
          ariaLabel="Key action"
          value={step.action}
          options={ACTIONS}
          onChange={(action) => onChange({ ...step, action: action as KeyAction })}
        />
        <KeyChips value={step.key} onChange={(key) => onChange({ ...step, key })} />
        {step.action === "tap" ? (
          <NumberBox label="ms" value={step.holdMs ?? 20} min={1} onChange={(holdMs) => onChange({ ...step, holdMs })} />
        ) : null}
      </>
    );
  }
  if (step.type === "mouse") {
    return (
      <>
        <Chips
          ariaLabel="Mouse action"
          value={step.action}
          options={ACTIONS}
          onChange={(action) => onChange({ ...step, action: action as "down" | "up" | "tap" })}
        />
        <Chips
          ariaLabel="Mouse button"
          value={step.button}
          options={[...MOUSE_OPTIONS, ...SIDE_OPTIONS]}
          onChange={(button) => onChange({ ...step, button })}
        />
        {step.action === "tap" ? (
          <NumberBox label="ms" value={step.holdMs ?? 15} min={1} onChange={(holdMs) => onChange({ ...step, holdMs })} />
        ) : null}
      </>
    );
  }
  if (step.type === "move") {
    return (
      <>
        <NumberBox label="X" value={step.x} onChange={(x) => onChange({ ...step, x })} />
        <NumberBox label="Y" value={step.y} onChange={(y) => onChange({ ...step, y })} />
      </>
    );
  }
  if (step.type === "goto") {
    return (
      <>
        <FieldSelect
          ariaLabel="Go to space"
          value={normalizeGotoWhere(step.where)}
          options={GOTO_WHERE}
          onChange={(where) => onChange({ ...step, where: normalizeGotoWhere(where) })}
        />
        <NumberBox label="X" value={step.x} onChange={(x) => onChange({ ...step, x })} />
        <NumberBox label="Y" value={step.y} onChange={(y) => onChange({ ...step, y })} />
        <NumberBox label="ms" value={step.ms ?? 15} min={0} onChange={(ms) => onChange({ ...step, ms })} />
      </>
    );
  }
  if (step.type === "wait") {
    return <NumberBox label="ms" value={step.ms} min={1} onChange={(ms) => onChange({ ...step, ms })} />;
  }
  if (step.type === "repeat") {
    return <NumberBox label="Times, 0 = until stop" value={step.count} min={0} onChange={(count) => onChange({ ...step, count })} />;
  }
  return (
    <FieldSelect
      ariaLabel="Macro to run"
      value={step.macroId}
      placeholder="Choose macro"
      options={macros.map((macro) => ({ value: macro.id, label: macro.name }))}
      onChange={(macroId) => onChange({ ...step, macroId })}
    />
  );
}

function KeyChips({ value, onChange }: { value: string; onChange: (value: string) => void }) {
  const [more, setMore] = useState(false);
  const base = value && !QUICK_KEYS.includes(value) ? [value, ...QUICK_KEYS] : QUICK_KEYS;
  const keys = more ? (KEY_OPTIONS.includes(value) ? KEY_OPTIONS : [value, ...KEY_OPTIONS]) : base;
  return (
    <div className="chips" role="radiogroup" aria-label="Key">
      {keys.map((key) => (
        <button
          key={key}
          type="button"
          role="radio"
          aria-checked={key === value}
          className={key === value ? "is-on" : ""}
          onClick={() => onChange(key)}
        >
          {key}
        </button>
      ))}
      <button type="button" className="more" onClick={() => setMore((open) => !open)}>
        {more ? "Less" : "More"}
      </button>
    </div>
  );
}

function NumberBox({
  label,
  value,
  min,
  onChange,
}: {
  label: string;
  value: number;
  min?: number;
  onChange: (value: number) => void;
}) {
  return (
    <label className="num">
      <span>{label}</span>
      <input
        type="number"
        value={Number.isFinite(value) ? value : 0}
        min={min}
        onChange={(e) => onChange(e.target.value === "" ? 0 : Number(e.target.value))}
      />
    </label>
  );
}

function label(step: Step) {
  if (step.type === "key") return "Key";
  if (step.type === "mouse") return "Click";
  if (step.type === "move") return "Move";
  if (step.type === "goto") return "Go to";
  if (step.type === "wait") return "Wait";
  if (step.type === "repeat") return "Repeat";
  return "Run";
}

function swap<T>(items: T[], a: number, b: number) {
  const next = items.slice();
  const hold = next[a];
  next[a] = next[b];
  next[b] = hold;
  return next;
}
