import { useState } from "react";
import {
  RADIAL_BLOCK_KEYS_MAX,
  RADIAL_OPACITY_MAX,
  RADIAL_OPACITY_MIN,
  RADIAL_SIZE_MAX,
  RADIAL_SIZE_MIN,
  RADIAL_SLOTS_MAX,
  RADIAL_SLOTS_MIN,
  blankRadial,
  type Macro,
  type Radial,
  type RadialBlock,
} from "../profile";
import { blockClash, blockedKeys, radialProblems, slotAngle, withSlot, withSlotCount } from "../radial";
import { Capture, type Captured } from "./Capture";
import { FieldSelect } from "./FieldSelect";
import { NumberField } from "./NumberField";
import { inputLabel } from "./TurnPanel";

type Picking = "trigger" | "prev" | "next" | "block" | null;

const PICK_TITLE: Record<Exclude<Picking, null>, string> = {
  trigger: "Radial menu trigger",
  prev: "Previous slot key",
  next: "Next slot key",
  block: "Key to block",
};

const BLOCK_NEXT: Record<RadialBlock, RadialBlock> = { off: "wasd", wasd: "custom", custom: "off" };
const BLOCK_LABEL: Record<RadialBlock, string> = { off: "Disabled", wasd: "WASD", custom: "Custom" };

/** Where slot i sits on the ring, as x, y around 0, 0. */
function point(angle: number, radius: number): [number, number] {
  const rad = ((angle - 90) * Math.PI) / 180;
  return [Math.cos(rad) * radius, Math.sin(rad) * radius];
}

function wedge(index: number, count: number, inner: number, outer: number): string {
  const half = 180 / count;
  const from = slotAngle(index, count) - half;
  const to = slotAngle(index, count) + half;
  const large = to - from > 180 ? 1 : 0;
  const [ax, ay] = point(from, outer);
  const [bx, by] = point(to, outer);
  const [cx, cy] = point(to, inner);
  const [dx, dy] = point(from, inner);
  return `M${ax} ${ay}A${outer} ${outer} 0 ${large} 1 ${bx} ${by}L${cx} ${cy}A${inner} ${inner} 0 ${large} 0 ${dx} ${dy}Z`;
}

function short(name: string): string {
  return name.length > 12 ? `${name.slice(0, 11)}…` : name;
}

export function RadialPreview({ radial, macros }: { radial: Radial; macros: Macro[] }) {
  const count = radial.slots.length;
  const names = new Map(macros.map((macro) => [macro.id, macro.name || "Macro"]));
  return (
    <svg className="radial-ring" viewBox="-120 -120 240 240" role="img" aria-label={`Radial menu with ${count} slots`}>
      {radial.slots.map((slot, index) => {
        const [x, y] = point(slotAngle(index, count), 78);
        const name = slot ? names.get(slot) : undefined;
        return (
          <g key={index} className={name ? "is-set" : ""}>
            <path d={wedge(index, count, 38, 114)} />
            <text x={x} y={y - 6} className="radial-num">
              {index + 1}
            </text>
            <text x={x} y={y + 10} className="radial-name">
              {name ? short(name) : "Empty"}
            </text>
          </g>
        );
      })}
      <circle r="30" className="radial-dead" />
    </svg>
  );
}

export function RadialPanel({
  configName,
  radial,
  macros,
  onChange,
}: {
  configName: string;
  radial: Radial | undefined;
  macros: Macro[];
  onChange: (radial: Radial | undefined) => void;
}) {
  const [picking, setPicking] = useState<Picking>(null);
  const [pickNote, setPickNote] = useState("");

  if (!radial) {
    return (
      <div className="empty-board">
        <h2>Radial menu</h2>
        <p>
          Hold a key, pick a macro from a ring, and let go to play it. Good for Helldivers 2 stratagems. {configName || "This profile"} has no radial menu yet.
        </p>
        <button type="button" className="primary" onClick={() => onChange(blankRadial())}>
          Add radial menu
        </button>
      </div>
    );
  }

  const problems = radialProblems(radial, macros);
  const options = [{ value: "", label: "Empty" }, ...macros.map((macro) => ({ value: macro.id, label: macro.name || "Macro" }))];
  const set = (patch: Partial<Radial>) => onChange({ ...radial, ...patch });

  const pick = (captured: Captured) => {
    if (captured.button === "LButton" || captured.button === "RButton") {
      setPickNote("Left and right click can't be used. Press another key or button");
      return;
    }
    if (picking !== "trigger" && captured.kind !== "key") {
      setPickNote("Use a keyboard key. The game still sees mouse buttons");
      return;
    }
    if (picking === "block" && captured.button.toLowerCase() === "pause") {
      setPickNote("Pause stops every macro, so it can't be blocked. Press another key");
      return;
    }
    if (picking === "trigger") set({ trigger: { kind: captured.kind, button: captured.button } });
    else if (picking === "block") {
      const have = radial.blockKeys.some((key) => key.toLowerCase() === captured.button.toLowerCase());
      if (!have) set({ blockKeys: [...radial.blockKeys, captured.button].slice(0, RADIAL_BLOCK_KEYS_MAX) });
    } else if (picking === "prev") set({ prevKey: captured.button });
    else if (picking === "next") set({ nextKey: captured.button });
    setPickNote("");
    setPicking(null);
  };

  const keyButton = (which: Exclude<Picking, null>, label: string, name: string) => (
    <button
      type="button"
      className="turn-input"
      aria-label={`${label} ${name ? inputLabel(name) : "not set"}. Click to change`}
      title="Click to change"
      onClick={() => {
        setPickNote("");
        setPicking(which);
      }}
    >
      {name ? inputLabel(name) : "Set"}
    </button>
  );

  return (
    <div className="radial">
      <div className="settings-stack radial-form">
        <div className="settings-block">
          <label className="settings-row">
            <span>
              <span className="settings-row-name">Radial menu</span>
              <span className="settings-row-note">Hold the trigger to open it, pick a slot, let go to play that macro once.</span>
            </span>
            <input type="checkbox" checked={radial.enabled} onChange={(event) => set({ enabled: event.target.checked })} />
          </label>
          <div className="turn-actions">
            <span className="radial-label">Trigger</span>
            {keyButton("trigger", "Trigger", radial.trigger.button)}
            <button type="button" onClick={() => onChange(undefined)}>
              Remove
            </button>
          </div>
        </div>

        <div className="settings-block">
          <div className="settings-row is-static">
            <span>
              <span className="settings-row-name">Slots</span>
              <span className="settings-row-note">Each slot plays its macro once, start to end, whatever that macro's own play mode is.</span>
            </span>
          </div>
          <div className="turn-actions">
            <button type="button" disabled={radial.slots.length <= RADIAL_SLOTS_MIN} onClick={() => onChange(withSlotCount(radial, radial.slots.length - 1))}>
              −
            </button>
            <span className="radial-label">{radial.slots.length} slots</span>
            <button type="button" disabled={radial.slots.length >= RADIAL_SLOTS_MAX} onClick={() => onChange(withSlotCount(radial, radial.slots.length + 1))}>
              +
            </button>
          </div>
          <ol className="radial-slots">
            {radial.slots.map((slot, index) => (
              <li key={index}>
                <span className="radial-num-chip">{index + 1}</span>
                <FieldSelect ariaLabel={`Slot ${index + 1}`} value={macros.some((macro) => macro.id === slot) ? slot : ""} options={options} onChange={(value) => onChange(withSlot(radial, index, value))} />
              </li>
            ))}
          </ol>
        </div>

        <div className="settings-block">
          <div className="settings-row is-static">
            <span>
              <span className="settings-row-name">Pick a slot with</span>
            </span>
          </div>
          <div className="settings-choices is-pair" role="radiogroup" aria-label="Pick a slot with">
            <button
              type="button"
              role="radio"
              aria-checked={radial.select === "keys"}
              className={`settings-choice${radial.select === "keys" ? " is-on" : ""}`}
              onClick={() => set({ select: "keys" })}
            >
              <span className="settings-row-name">Keys</span>
              <span className="settings-row-note">1 to 8 pick a slot. Two keys step through them. The game never sees these keys while the menu is open.</span>
            </button>
            <button
              type="button"
              role="radio"
              aria-checked={radial.select === "flick"}
              className={`settings-choice${radial.select === "flick" ? " is-on" : ""}`}
              onClick={() => set({ select: "flick" })}
            >
              <span className="settings-row-name">Flick</span>
              <span className="settings-row-note">Move the mouse toward a slot. The camera turns too in most games.</span>
            </button>
          </div>
          {radial.select === "keys" ? (
            <div className="turn-actions">
              <span className="radial-label">Previous</span>
              {keyButton("prev", "Previous", radial.prevKey)}
              <span className="radial-label">Next</span>
              {keyButton("next", "Next", radial.nextKey)}
            </div>
          ) : (
            <>
              <p className="turn-line is-bad">
                Windows can't stop a game from reading the mouse directly, so most games still turn the camera while you flick. Use Keys if that matters.
              </p>
              <div className="turn-actions">
                <span className="radial-label">Dead zone</span>
                <NumberField ariaLabel="Dead zone in mouse counts" min={1} value={radial.deadZone} onChange={(value) => set({ deadZone: value })} />
                <span className="radial-label">counts</span>
              </div>
              <label className="settings-row radial-check">
                <span>
                  <span className="settings-row-name">Click to pick</span>
                  <span className="settings-row-note">Left click plays the lit slot right away. The game may see that click too.</span>
                </span>
                <input type="checkbox" checked={radial.clickSelect} onChange={(event) => set({ clickSelect: event.target.checked })} />
              </label>
            </>
          )}
        </div>

        <div className="settings-block">
          <div className="settings-row is-static">
            <span>
              <span className="settings-row-name">Block movement</span>
              <span className="settings-row-note">
                Stops your character when the menu opens. The game doesn't see these keys while the menu is open or while the picked macro plays. Keys you still hold
                start again when it ends.
              </span>
            </span>
            <button
              type="button"
              className="turn-input"
              aria-label={`Block movement ${BLOCK_LABEL[radial.blockMove]}. Click to change`}
              title="Click to change"
              onClick={() => set({ blockMove: BLOCK_NEXT[radial.blockMove] })}
            >
              {BLOCK_LABEL[radial.blockMove]}
            </button>
          </div>
          {radial.blockMove !== "off" ? (
            <div className="watch-row radial-block-keys">
              {blockedKeys(radial).map((key) => {
                const clash = blockClash(radial, key);
                const custom = radial.blockMove === "custom";
                const label = `${inputLabel(key)}${clash ? `: ${clash}, not blocked` : ""}`;
                return custom ? (
                  <button
                    key={key}
                    type="button"
                    className={clash ? "is-clash" : ""}
                    title={clash ? `This is ${clash}. The menu won't arm until you remove it` : "Click to remove"}
                    aria-label={`${label}. Click to remove`}
                    onClick={() => set({ blockKeys: radial.blockKeys.filter((item) => item !== key) })}
                  >
                    {inputLabel(key)} <span className="act-x">×</span>
                  </button>
                ) : (
                  <span key={key} className={`radial-block-key${clash ? " is-clash" : ""}`} title={clash ? `This is ${clash}` : undefined} aria-label={label}>
                    {inputLabel(key)}
                  </span>
                );
              })}
              {radial.blockMove === "custom" && radial.blockKeys.length < RADIAL_BLOCK_KEYS_MAX ? (
                <button
                  type="button"
                  aria-label="Add a key to block"
                  onClick={() => {
                    setPickNote("");
                    setPicking("block");
                  }}
                >
                  +
                </button>
              ) : null}
            </div>
          ) : null}
          {radial.blockMove === "custom" && !radial.blockKeys.length ? <p className="turn-line">No keys yet, so nothing is blocked. Press + to add one.</p> : null}
        </div>

        <div className="settings-block">
          <div className="settings-row is-static">
            <span>
              <span className="settings-row-name">Look</span>
              <span className="settings-row-note">Size is at 100 % display scaling. A monitor set to 150 % draws it 1.5 times as big, so it looks the same.</span>
            </span>
          </div>
          <label className="radial-range">
            <span className="radial-label">Size</span>
            <input
              type="range"
              min={RADIAL_SIZE_MIN}
              max={RADIAL_SIZE_MAX}
              step={10}
              value={radial.size}
              onChange={(event) => set({ size: Number(event.target.value) })}
            />
            <span className="radial-value">{radial.size} px</span>
          </label>
          <label className="radial-range">
            <span className="radial-label">Opacity</span>
            <input
              type="range"
              min={RADIAL_OPACITY_MIN}
              max={RADIAL_OPACITY_MAX}
              step={1}
              value={radial.opacity}
              onChange={(event) => set({ opacity: Number(event.target.value) })}
            />
            <span className="radial-value">{radial.opacity} %</span>
          </label>
        </div>

        <div className="settings-block">
          <div className="settings-row is-static">
            <span>
              <span className="settings-row-name">Where it opens</span>
            </span>
          </div>
          <div className="settings-choices is-pair" role="radiogroup" aria-label="Where it opens">
            {(["center", "cursor"] as const).map((at) => (
              <button
                key={at}
                type="button"
                role="radio"
                aria-checked={radial.at === at}
                className={`settings-choice${radial.at === at ? " is-on" : ""}`}
                onClick={() => set({ at })}
              >
                <span className="settings-row-name">{at === "center" ? "Middle of the screen" : "At the cursor"}</span>
                <span className="settings-row-note">{at === "center" ? "Best for games that hide the cursor." : "Where the mouse pointer is when you press the trigger."}</span>
              </button>
            ))}
          </div>
        </div>

        <div className="settings-block">
          <div className="settings-row is-static">
            <span>
              <span className="settings-row-name">Before you play</span>
              <span className="settings-row-note">
                The menu draws over borderless and windowed games, not exclusive fullscreen. Competitive games and anti-cheat may treat sent input as cheating.
              </span>
            </span>
          </div>
        </div>
        {picking ? (
          <Capture
            title={pickNote || PICK_TITLE[picking]}
            onCancel={() => {
              setPickNote("");
              setPicking(null);
            }}
            onPick={pick}
          />
        ) : null}
      </div>
      <aside className="radial-side">
        <RadialPreview radial={radial} macros={macros} />
        {problems.length ? (
          <ul className="radial-problems">
            {problems.map((problem) => (
              <li key={problem}>{problem}</li>
            ))}
          </ul>
        ) : (
          <p className="settings-status is-on">
            <i aria-hidden="true" />
            Set up
          </p>
        )}
      </aside>
    </div>
  );
}
