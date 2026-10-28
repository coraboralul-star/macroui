import { useEffect, useState } from "react";
import { hasShell, post, subscribe } from "../bridge";
import { parseTurn, type TurnState } from "../hostMessages";
import { FieldSelect } from "./FieldSelect";
import { NumberField } from "./NumberField";

const KEY_STORE = "macroui-turn-key";

/** Keys a game rarely uses. The shell swallows the one picked while calibrating. */
export const TURN_KEYS: { value: string; label: string; vk: number }[] = [
  ...Array.from({ length: 12 }, (_, i) => ({ value: `F${i + 1}`, label: `F${i + 1}`, vk: 0x70 + i })),
  { value: "Insert", label: "Insert", vk: 0x2d },
  { value: "Home", label: "Home", vk: 0x24 },
  { value: "End", label: "End", vk: 0x23 },
  { value: "PageUp", label: "Page Up", vk: 0x21 },
  { value: "PageDown", label: "Page Down", vk: 0x22 },
  { value: "Pause", label: "Pause", vk: 0x13 },
  { value: "ScrollLock", label: "Scroll Lock", vk: 0x91 },
];

function readKey(): string {
  try {
    const saved = localStorage.getItem(KEY_STORE) ?? "";
    return TURN_KEYS.some((key) => key.value === saved) ? saved : "F10";
  } catch {
    return "F10";
  }
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
}: {
  configName: string;
  countsPer360: number | undefined;
  onCounts: (counts: number) => void;
}) {
  const [key, setKey] = useState(readKey);
  const [state, setState] = useState<TurnState>({ state: "off", counts: 0, detail: "" });
  const shell = hasShell();
  const vk = TURN_KEYS.find((item) => item.value === key)?.vk ?? 0x79;
  const busy = state.state === "armed" || state.state === "turning" || state.state === "short";
  const testing = state.state === "testing" || state.state === "sent";

  useEffect(() => {
    try {
      localStorage.setItem(KEY_STORE, key);
    } catch {
      /* the key is only remembered for this session */
    }
  }, [key]);

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

  // Leaving the pane gives the key back to the game.
  useEffect(() => () => post({ type: "turn", action: "cancel" }), []);

  // A nudge while testing: the next press uses the new counts.
  useEffect(() => {
    if (testing && countsPer360) post({ type: "turn", action: "test", vk, counts: countsPer360 });
  }, [countsPer360]);

  const label = TURN_KEYS.find((item) => item.value === key)?.label ?? key;
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
                Press the key in game, turn one full circle back to the same spot, and press it again. The game never sees the key while this is on.
              </span>
            </span>
          </div>
          <div className="turn-actions">
            <FieldSelect
              ariaLabel="Calibration key"
              value={key}
              options={TURN_KEYS.map(({ value, label: name }) => ({ value, label: name }))}
              onChange={(next) => setKey(next)}
            />
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
