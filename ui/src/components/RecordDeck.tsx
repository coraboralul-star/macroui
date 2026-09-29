import { useEffect, useState } from "react";
import type { Macro } from "../profile";
import { applyDelayMode } from "../eventLane";
import { RecordSurface } from "./RecordSurface";
import { compileRecording, type Recording } from "../recording";

type DelayMode = "recorded" | "fixed" | "none" | "custom";

export function RecordDeck({
  macro,
  macros,
  onChange,
}: {
  macro: Macro;
  macros: Macro[];
  onChange: (macro: Macro) => void;
}) {
  const [delayMode, setDelayMode] = useState<DelayMode>(modeOf(macro.recording));
  const [fixedMs, setFixedMs] = useState(macro.recording?.holdMs && macro.recording.holdMs > 1 ? macro.recording.holdMs : 20);
  const [gear, setGear] = useState(false);

  useEffect(() => {
    setDelayMode(modeOf(macro.recording));
    setFixedMs(macro.recording?.holdMs && macro.recording.holdMs > 1 ? macro.recording.holdMs : 20);
    setGear(false);
  }, [macro.id]);

  useEffect(() => {
    if (!gear) return;
    const close = () => setGear(false);
    window.addEventListener("pointerdown", close);
    return () => window.removeEventListener("pointerdown", close);
  }, [gear]);

  const setMode = (mode: DelayMode) => {
    setDelayMode(mode);
    if (!macro.steps.length && !macro.recording) return;
    if (mode === "recorded") {
      if (!macro.recording) return;
      const recording = { ...macro.recording, timing: "played" as const };
      onChange({ ...macro, recording, steps: compileRecording(recording) });
      return;
    }
    if (mode === "custom") return;
    const ms = mode === "none" ? 0 : Math.max(0, fixedMs);
    const steps = applyDelayMode(macro.steps, mode, ms);
    const recording = macro.recording
      ? { ...macro.recording, timing: "custom" as const, holdMs: mode === "none" ? 1 : ms, intervalMs: mode === "fixed" ? ms : 0 }
      : null;
    onChange({ ...macro, steps, recording });
  };

  return (
    <div className="deck sse">
      <RecordSurface
      resetKey={macro.id}
      steps={macro.steps}
      macros={macros}
      onSteps={(steps) => {
        setDelayMode("custom");
        onChange({ ...macro, steps });
      }}
      onCapture={(recording) => {
        setDelayMode("recorded");
        onChange({ ...macro, steps: compileRecording(recording), recording });
      }}
      onClear={() => {
        setDelayMode("recorded");
        onChange({ ...macro, steps: [], recording: null });
      }}
      extras={
        <div className="gear-wrap" onPointerDown={(event) => event.stopPropagation()}>
          <button type="button" className="rec-gear" aria-label="Delay options" aria-expanded={gear} onClick={() => setGear((open) => !open)}>
            <GearIcon />
          </button>
          {gear ? (
            <div className="gear-menu" role="menu">
              <button type="button" role="menuitemradio" aria-checked={delayMode === "recorded"} disabled={!macro.recording} onClick={() => setMode("recorded")}>
                As recorded
              </button>
              <button type="button" role="menuitemradio" aria-checked={delayMode === "fixed"} onClick={() => setMode("fixed")}>
                Fixed delay
              </button>
              {delayMode === "fixed" ? (
                <label className="gear-ms">
                  ms
                  <input
                    aria-label="Fixed delay"
                    type="number"
                    min={0}
                    value={fixedMs}
                    onChange={(event) => {
                      const ms = Math.max(0, Math.round(Number(event.target.value) || 0));
                      setFixedMs(ms);
                      setDelayMode("fixed");
                      onChange({
                        ...macro,
                        steps: applyDelayMode(macro.steps, "fixed", Math.max(1, ms)),
                        recording: macro.recording ? { ...macro.recording, timing: "custom", holdMs: Math.max(1, ms), intervalMs: Math.max(1, ms) } : macro.recording,
                      });
                    }}
                  />
                </label>
              ) : null}
              <button type="button" role="menuitemradio" aria-checked={delayMode === "none"} onClick={() => setMode("none")}>
                No delay
              </button>
              {delayMode === "custom" ? (
                <button type="button" role="menuitemradio" aria-checked disabled>
                  Custom
                </button>
              ) : null}
            </div>
          ) : null}
        </div>
      }
    />
    </div>
  );
}

function GearIcon() {
  return (
    <svg width="14" height="14" viewBox="0 0 14 14" aria-hidden="true">
      <path
        fill="currentColor"
        d="M5.6 0h2.8l.3 1.7a5 5 0 0 1 1.5.9L11.8 2l1.4 2.4-1.4 1a5 5 0 0 1 0 1.8l1.4 1-1.4 2.4-1.6-.6a5 5 0 0 1-1.5.9L8.4 14H5.6l-.3-1.7a5 5 0 0 1-1.5-.9L2.2 12 0.8 9.6l1.4-1a5 5 0 0 1 0-1.8l-1.4-1L2.2 2l1.6.6a5 5 0 0 1 1.5-.9zm1.4 4.2A2.8 2.8 0 1 0 9.8 7a2.8 2.8 0 0 0-2.8-2.8z"
      />
    </svg>
  );
}

function modeOf(recording: Recording | null): DelayMode {
  if (!recording || recording.timing === "played") return "recorded";
  if (recording.intervalMs === 0 && recording.holdMs <= 1) return "none";
  if (recording.holdMs === recording.intervalMs) return "fixed";
  return "custom";
}
