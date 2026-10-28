import { useEffect, useRef, useState } from "react";
import { hasShell, post, subscribe } from "../bridge";
import { parseTurn, type TurnState } from "../hostMessages";
import { keyLabel } from "../keyboard";
import { pressLabel } from "../recording";
import { vkOf } from "../vk";
import { Capture, type Captured } from "./Capture";
import { NumberField } from "./NumberField";

const INPUT_STORE = "macroui-turn-input";
/** M5. Any key or mouse button but left and right click. */
export const TURN_INPUT_DEFAULT = "XButton2";

function readInput(): string {
  try {
    const saved = localStorage.getItem(INPUT_STORE) ?? "";
    return vkOf(saved) ? saved : TURN_INPUT_DEFAULT;
  } catch {
    return TURN_INPUT_DEFAULT;
  }
}

export function inputLabel(name: string): string {
  return /Button/.test(name) ? pressLabel(name) : keyLabel(name);
}

/** counts times 1 + percent / 100, at least 1 count away. */
export function nudge(counts: number, percent: number): number {
  const step = Math.max(1, Math.round((counts * Math.abs(percent)) / 100));
  return Math.max(1, counts + (percent < 0 ? -step : step));
}

export function TurnPanel({
  configName,
  countsPer360,
  onCounts,
  armed,
}: {
  configName: string;
  countsPer360: number | undefined;
  onCounts: (counts: number) => void;
  /** The engine is running macros. Calibration pauses them, so a macro on the same button cannot fire or eat the press. */
  armed: boolean;
}) {
  const [input, setInput] = useState(readInput);
  const [picking, setPicking] = useState(false);
  const [pickNote, setPickNote] = useState("");
  const [state, setState] = useState<TurnState>({ state: "off", counts: 0, detail: "" });
  const paused = useRef(false);
  const shell = hasShell();
  const vk = vkOf(input);
  const busy = state.state === "armed" || state.state === "turning" || state.state === "short";
  const testing = state.state === "testing" || state.state === "sent";
  const active = busy || testing;

  useEffect(() => {
    try {
      localStorage.setItem(INPUT_STORE, input);
    } catch {
      /* the button is only remembered for this session */
    }
  }, [input]);

  // Macros stop while the shell holds the button, and start again after, if they were running.
  useEffect(() => {
    if (active && armed && !paused.current) {
      paused.current = true;
      post({ type: "command", action: "stop" });
    } else if (!active && paused.current) {
      paused.current = false;
      post({ type: "command", action: "start" });
    }
  }, [active, armed]);

  const pick = (captured: Captured) => {
    if (captured.button === "LButton" || captured.button === "RButton") {
      setPickNote("Left and right click can't be used. Press another key or button");
      return;
    }
    if (!vkOf(captured.button)) {
      setPickNote("That key can't be watched. Press another one");
      return;
    }
    setPickNote("");
    setInput(captured.button);
    setPicking(false);
  };

  useEffect(
    () =>
      subscribe((message) => {
        const next = parseTurn(message);
        if (!next) return;
        setState(next);
        if (next.state === "done") onCounts(next.counts);
      }),
    [onCounts],
  );

  // Leaving the pane gives the button back to the game, and the macros back to the engine.
  useEffect(
    () => () => {
      post({ type: "turn", action: "cancel" });
      if (paused.current) post({ type: "command", action: "start" });
    },
    [],
  );

  // A nudge while testing: the next press uses the new counts.
  useEffect(() => {
    if (testing && countsPer360) post({ type: "turn", action: "test", vk, counts: countsPer360 });
  }, [countsPer360]);

  const label = inputLabel(input);
  const line =
    state.state === "armed"
      ? `In game, aim at a landmark, then press ${label}.`
      : state.state === "turning"
        ? `Turn one full circle back to the landmark, then press ${label} again.`
        : state.state === "short"
          ? `That turn was only ${state.counts} counts. Press ${label} to start again.`
          : state.state === "done"
            ? `Saved ${state.counts} counts per 360.`
            : state.state === "testing"
              ? `In game, press ${label} to turn one full circle.`
              : state.state === "sent"
                ? `Sent ${state.counts} counts. Nudge until it lands on the landmark, then press ${label} again.`
                : state.state === "error"
                  ? state.detail
                  : "";

  return (
    <>
      <h2>Turn</h2>
      <p className="settings-lead">
        Turn by degrees steps for {configName || "this profile"}. Each profile keeps its own calibration, so each game can have its own sensitivity.
      </p>
      <div className="settings-stack">
        <div className="settings-block">
          <div className="settings-row is-static">
            <span>
              <span className="settings-row-name">Counts per 360</span>
              <span className="settings-row-note">
                {countsPer360 ? "How many mouse counts make one full turn in this game." : "Not calibrated. Turn steps send nothing until you calibrate."}
              </span>
            </span>
          </div>
          <div className="turn-counts">
            <button type="button" disabled={!countsPer360} onClick={() => countsPer360 && onCounts(nudge(countsPer360, -1))}>
              −1%
            </button>
            <button type="button" disabled={!countsPer360} onClick={() => countsPer360 && onCounts(nudge(countsPer360, -0.1))}>
              −0.1%
            </button>
            <NumberField ariaLabel="Counts per 360" min={0} value={countsPer360 ?? 0} onChange={(value) => onCounts(value)} />
            <button type="button" disabled={!countsPer360} onClick={() => countsPer360 && onCounts(nudge(countsPer360, 0.1))}>
              +0.1%
            </button>
            <button type="button" disabled={!countsPer360} onClick={() => countsPer360 && onCounts(nudge(countsPer360, 1))}>
              +1%
            </button>
          </div>
          <p className={`settings-status${countsPer360 ? " is-on" : ""}`}>
            <i aria-hidden="true" />
            {countsPer360 ? `Calibrated: ${countsPer360} counts per 360` : "Not calibrated"}
          </p>
        </div>
        <div className="settings-block">
          <div className="settings-row is-static">
            <span>
              <span className="settings-row-name">Calibrate</span>
              <span className="settings-row-note">
                Press the button in game, turn one full circle back to the same spot, and press it again. The game never sees the button while this is on, and macros pause until you finish. Click the button to use a different key or mouse button.
              </span>
            </span>
          </div>
          <div className="turn-actions">
            <button
              type="button"
              className="turn-input"
              aria-label={`Calibration button ${label}. Click to change`}
              title="Click to change"
              disabled={active}
              onClick={() => {
                setPickNote("");
                setPicking(true);
              }}
            >
              {label}
            </button>
            {busy ? (
              <button type="button" onClick={() => post({ type: "turn", action: "cancel" })}>
                Cancel
              </button>
            ) : (
              <button type="button" className="primary" disabled={!shell || testing} onClick={() => post({ type: "turn", action: "calibrate", vk })}>
                Calibrate
              </button>
            )}
            {testing ? (
              <button type="button" onClick={() => post({ type: "turn", action: "cancel" })}>
                Stop test
              </button>
            ) : (
              <button
                type="button"
                disabled={!shell || busy || !countsPer360}
                onClick={() => countsPer360 && post({ type: "turn", action: "test", vk, counts: countsPer360 })}
              >
                Test 360
              </button>
            )}
          </div>
          {line ? <p className={`turn-line${state.state === "error" || state.state === "short" ? " is-bad" : ""}`}>{line}</p> : null}
          {!shell ? <p className="turn-line">Calibration needs the desktop app.</p> : null}
          {picking ? (
            <Capture
              title={pickNote || "Calibration button"}
              onCancel={() => {
                setPickNote("");
                setPicking(false);
              }}
              onPick={pick}
            />
          ) : null}
        </div>
        <div className="settings-block">
          <div className="settings-row is-static">
            <span>
              <span className="settings-row-name">Recalibrate after changes</span>
              <span className="settings-row-note">
                Changing the in-game sensitivity or your mouse DPI changes the counts for a full turn. Calibrate again after either one.
              </span>
            </span>
          </div>
        </div>
        <div className="settings-block">
          <div className="settings-row is-static">
            <span>
              <span className="settings-row-name">Casual and single-player games only</span>
              <span className="settings-row-note">
                Competitive games and anti-cheat (Overwatch, for example) may treat sent mouse input as cheating. A turn does not aim. It only turns the camera.
              </span>
            </span>
          </div>
        </div>
      </div>
    </>
  );
}
