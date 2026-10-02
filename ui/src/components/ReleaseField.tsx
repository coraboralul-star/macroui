import { useState } from "react";
import { RELEASE_STOPS, type ReleaseStop } from "../profile";
import { Confirm } from "./Confirm";
import { FieldSelect } from "./FieldSelect";

const INHERIT = { value: "macro", label: "Same as macro" };

export function ReleaseField({
  value,
  onChange,
  compact,
  label = "On release",
  allowInherit,
}: {
  value: ReleaseStop | undefined;
  onChange: (next: ReleaseStop | undefined) => void;
  compact?: boolean;
  label?: string;
  allowInherit?: boolean;
}) {
  const [warn, setWarn] = useState(false);
  const current = allowInherit ? (value === "finish" || value === "nextUp" ? value : "macro") : value === "finish" ? "finish" : "nextUp";
  const options = allowInherit ? [INHERIT, ...RELEASE_STOPS] : RELEASE_STOPS;

  const pick = (next: string) => {
    if (next === "finish" && current !== "finish") {
      setWarn(true);
      return;
    }
    if (allowInherit && next === "macro") {
      onChange(undefined);
      return;
    }
    onChange(next === "finish" ? "finish" : "nextUp");
  };

  return (
    <>
      <div className={`rel-field${compact ? " is-bar" : ""}`}>
        {compact ? <span>{label}</span> : <p className="ctx-label">{label}</p>}
        <FieldSelect ariaLabel={label} value={current} options={options} onChange={pick} />
      </div>
      {warn ? (
        <Confirm
          title="Enable Play full?"
          note="Letting go still plays the rest of this cycle. Keys and clicks after release can open apps or press buttons. Enable only if you want that."
          action="Enable"
          onCancel={() => setWarn(false)}
          onConfirm={() => {
            onChange("finish");
            setWarn(false);
          }}
        />
      ) : null}
    </>
  );
}
