import { useEffect, useRef, useState, type ReactNode } from "react";
import { liveSteps } from "../eventLane";
import type { Macro, Step } from "../profile";
import { compileRecording, type RecEvent, type Recording } from "../recording";
import { EventLane } from "./EventLane";
import { RecordBar } from "./RecordBar";

export function RecordSurface({
  resetKey,
  steps,
  macros,
  extras,
  onSteps,
  onCapture,
  onClear,
  onLive,
  allowScanWait = true,
}: {
  resetKey: string;
  steps: Step[];
  macros: Macro[];
  extras?: ReactNode;
  onSteps: (steps: Step[]) => void;
  onCapture?: (recording: Recording) => void;
  onClear: () => void;
  onLive?: (events: RecEvent[] | null) => void;
  allowScanWait?: boolean;
}) {
  const [live, setLive] = useState<RecEvent[] | null>(null);
  const [saved, setSaved] = useState(steps);
  const savedKey = useRef(resetKey);
  const onLiveRef = useRef(onLive);
  onLiveRef.current = onLive;

  useEffect(() => {
    setLive(null);
    setSaved(steps);
    savedKey.current = resetKey;
  }, [resetKey]);

  const setBoth = (events: RecEvent[] | null) => {
    setLive(events);
    onLiveRef.current?.(events);
  };

  const shown = live ? liveSteps(live, "recorded", 0) : steps;
  const dirty = JSON.stringify(steps) !== JSON.stringify(saved);
  const busy = live != null;

  return (
    <div className="rec-dock">
      <div className="rec-stage">
        <div className="lane-wrap well" data-chrome={live ? "" : undefined}>
          {live ? (
            <EventLane steps={shown} macros={macros} empty="" readOnly allowScanWait={allowScanWait} onChange={() => {}} />
          ) : (
            <EventLane steps={steps} macros={macros} empty="" allowScanWait={allowScanWait} onChange={onSteps} />
          )}
        </div>
        <RecordBar
          dock
          extras={extras}
          onLive={setBoth}
          onCapture={(recording) => {
            if (onCapture) {
              onCapture(recording);
              return;
            }
            onSteps([...steps, ...compileRecording(recording)]);
          }}
          onClear={onClear}
          clearDisabled={busy || !steps.length}
        />
      </div>
      <div className="rec-commit">
        <button type="button" className="rec-revert" disabled={busy || !dirty} onClick={() => onSteps(saved)}>
          Revert
        </button>
        <button
          type="button"
          className="rec-save"
          disabled={busy || !dirty}
          onClick={() => setSaved(steps)}
        >
          Save
        </button>
      </div>
    </div>
  );
}
