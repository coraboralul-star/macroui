import { useEffect, useMemo, useRef, useState } from "react";
import { canonCode, clusterAround, keyLabel, keySpots, type KeySpot } from "../keyboard";
import { previewScenes } from "../macroFlow";
import type { Block, PlayMode, Step } from "../profile";
import { pressLabel } from "../recording";
import { KeyFace } from "./KeyFace";
import { SlideToggle } from "./SlideToggle";

type Trig = "held" | "released" | "pressed";
type Kind = "key" | "mouse";

type InputRef = { code: string; kind: Kind; label: string };

type Cue =
  | { type: "trigger"; down: boolean; trig: Trig; ms: number; real: number }
  | { type: "key"; input: InputRef; down: boolean; ms: number; real: number }
  | { type: "pair"; input: InputRef; ms: number; real: number }
  | { type: "wait"; ms: number; real: number };

type Beat = {
  at: number;
  ms: number;
  trigger: Trig;
  lit: InputRef[];
  fade: InputRef[];
  fadeUntil: number;
};

const MOUSE = new Set(["LButton", "RButton", "MButton", "XButton1", "XButton2"]);
const UNIT = 52;

function tapMs(ms: number) {
  const n = ms <= 0 ? 18 : ms;
  return Math.min(920, Math.max(680, Math.round(160 + n * 1.6)));
}

function waitMs(ms: number) {
  return Math.min(900, Math.max(360, Math.round(ms * 1.2)));
}

function realMs(ms: number, floor = 42) {
  return Math.min(1600, Math.max(floor, Math.round(ms)));
}

function span(simple: number, real: number) {
  return { ms: simple, real };
}

function asInput(code: string): InputRef {
  if (MOUSE.has(code)) return { code, kind: "mouse", label: pressLabel(code) };
  const name = canonCode(code);
  return { code: name, kind: "key", label: keyLabel(name) };
}

function same(a: InputRef, b: InputRef) {
  return a.code === b.code && a.kind === b.kind;
}

function stepCues(steps: Step[]): Cue[] {
  const cues: Cue[] = [];
  for (const step of steps) {
    if (step.type === "wait" || step.type === "scanWait") {
      cues.push({ type: "wait", ...span(waitMs(step.ms), realMs(step.ms)) });
      continue;
    }
    if (step.type === "key" || step.type === "mouse") {
      const input = asInput(step.type === "key" ? step.key : step.button);
      if (step.action === "down") cues.push({ type: "key", input, down: true, ...span(tapMs(step.holdMs ?? 16), realMs(step.holdMs ?? 16)) });
      else if (step.action === "up") cues.push({ type: "key", input, down: false, ...span(280, 36) });
      else cues.push({ type: "pair", input, ...span(tapMs(step.holdMs ?? 18), realMs((step.holdMs ?? 18) + 16, 56)) });
      continue;
    }
    if (step.type === "repeat") {
      const times = step.count === 0 ? 3 : Math.min(step.count, 4);
      const once = stepCues(step.steps);
      for (let i = 0; i < times; i++) cues.push(...once);
    }
  }
  return cues;
}

function blockCues(block: Block, mode: "simple" | "real", splitMs?: number): Cue[] {
  if (block.type === "wait") return [{ type: "wait", ...span(waitMs(block.ms), realMs(block.ms)) }];
  if (block.type === "tapHold") {
    const cues: Cue[] = [];
    const watch = block.watch.filter(Boolean);
    for (const code of watch) cues.push({ type: "key", input: asInput(code), down: true, ...span(tapMs(40), realMs(40)) });
    cues.push({ type: "wait", ...span(waitMs(block.gapMs), realMs(block.gapMs)) });
    for (const code of watch) cues.push({ type: "key", input: asInput(code), down: false, ...span(280, 36) });
    const hold = block.key || "";
    if (hold) cues.push({ type: "pair", input: asInput(hold), ...span(tapMs(block.armMs || 18), realMs(block.armMs || 18, 56)) });
    return cues;
  }
  if (block.type === "whileHeld") {
    const body = block.steps.length ? loop(block.steps, 3) : [];
    const cues: Cue[] = [{ type: "trigger", down: true, trig: "held", ...span(520, 160) }, ...body];
    if (splitMs == null) cues.push({ type: "trigger", down: false, trig: "released", ...span(420, 120) });
    return cues;
  }
  if (block.type === "ifShort") {
    const times = Math.min(4, Math.max(1, block.minCycles));
    return [
      { type: "trigger", down: true, trig: "pressed", ...span(280, 90) },
      { type: "trigger", down: false, trig: "released", ...span(360, 110) },
      ...(block.steps.length ? loop(block.steps, times) : []),
    ];
  }
  if (block.type === "swapAfter") {
    const body = block.steps.length ? loop(block.steps, 3) : [];
    return [{ type: "trigger", down: true, trig: "held", ...span(520, 160) }, ...body, { type: "trigger", down: false, trig: "released", ...span(420, 120) }];
  }
  if (block.type === "then") {
    const once = stepCues(block.steps);
    if (!once.length) return [{ type: "wait", ...span(waitMs(block.forMs || 400), realMs(block.forMs || 400)) }];
    if (!block.forMs) return once;
    const length = (cue: Cue) => (mode === "real" ? cue.real : cue.ms);
    const cycle = once.reduce((sum, cue) => sum + length(cue), 0);
    const budget = mode === "real" ? realMs(block.forMs) : waitMs(block.forMs);
    const times = Math.max(1, Math.min(6, Math.ceil(budget / Math.max(1, cycle))));
    const cues: Cue[] = [];
    for (let i = 0; i < times; i++) cues.push(...once);
    return cues;
  }
  if (block.type === "repeat") {
    if (!block.steps.length) return [];
    const times = block.count === 0 ? 3 : Math.min(block.count, 4);
    return loop(block.steps, times);
  }
  return block.steps.length ? stepCues(block.steps) : [];
}

function loop(steps: Step[], times: number) {
  const once = stepCues(steps);
  const cues: Cue[] = [];
  for (let i = 0; i < Math.max(1, times); i++) cues.push(...once);
  return cues;
}

function heldPathCues(): Cue[] {
  return [
    { type: "trigger", down: true, trig: "held", ...span(980, 220) },
    { type: "trigger", down: false, trig: "released", ...span(640, 140) },
  ];
}

function applyTrigger(cues: Cue[], mode: "simple" | "real"): Beat[] {
  const held: InputRef[] = [];
  let trigger: Trig = "released";
  let at = 0;
  const beats: Beat[] = [];
  for (const cue of cues) {
    let fade: InputRef[] = [];
    let fadeUntil = 0;
    if (cue.type === "trigger") trigger = cue.trig;
    else if (cue.type === "key" && cue.down) {
      if (!held.some((item) => same(item, cue.input))) held.push(cue.input);
    } else if (cue.type === "key" && !cue.down) {
      const index = held.findIndex((item) => same(item, cue.input));
      if (index >= 0) held.splice(index, 1);
    } else if (cue.type === "pair") {
      fade = [cue.input];
      fadeUntil = 0.62;
    }
    const ms = mode === "real" ? cue.real : cue.ms;
    beats.push({
      at,
      ms,
      trigger,
      lit: held.slice(),
      fade,
      fadeUntil,
    });
    at += ms;
  }
  return beats;
}

function beatAt(beats: Beat[], time: number) {
  return beats.find((beat) => time < beat.at + beat.ms) ?? beats[beats.length - 1] ?? null;
}

function spotId(spot: KeySpot) {
  return `${spot.code}:${spot.x}:${spot.y}`;
}

function litNow(beat: Beat | null, progress: number) {
  if (!beat) return [];
  const list = beat.lit.slice();
  if (progress < beat.fadeUntil) {
    for (const item of beat.fade) {
      if (!list.some((other) => same(other, item))) list.push(item);
    }
  }
  return list;
}

export function GraphPreview({
  trigger,
  blocks,
  playMode,
  focusId,
  focusNonce,
  onScene,
  onPick,
}: {
  trigger: string;
  blocks: Block[];
  playMode: PlayMode;
  focusId?: string;
  focusNonce?: number;
  onScene: (nodeId: string) => void;
  onPick?: (nodeId: string) => void;
}) {
  const scenes = useMemo(() => previewScenes(trigger, blocks), [trigger, blocks]);
  const [index, setIndex] = useState(0);
  const [run, setRun] = useState(0);
  const [speed, setSpeed] = useState<"simple" | "real">("simple");
  const [elapsed, setElapsed] = useState(0);
  const [done, setDone] = useState(false);
  const [cut, setCut] = useState({ w: 640, h: 168 });
  const boot = useRef(true);
  const camRef = useRef<string[]>([]);
  const camScene = useRef("");
  const cutRef = useRef<HTMLDivElement>(null);
  const scene = scenes[index] ?? scenes[0];
  const block = scene && scene.kind !== "heldPath" ? blocks.find((item) => item.id === scene.id) ?? null : null;
  const bind = trigger || "Trigger";
  const spots = useMemo(() => keySpots(), []);
  const [clockScene, setClockScene] = useState(scene?.id ?? "");
  if ((scene?.id ?? "") !== clockScene) {
    setClockScene(scene?.id ?? "");
    setElapsed(0);
    setDone(false);
  }

  const beats = useMemo(() => {
    if (!scene) return [];
    const after = block ? blocks[blocks.findIndex((item) => item.id === block.id) + 1] : undefined;
    const splitMs = block?.type === "whileHeld" && (after?.type === "ifShort" || after?.type === "swapAfter")
      ? after.type === "ifShort" ? after.underMs : after.afterMs
      : undefined;
    const cues = scene.kind === "heldPath" ? heldPathCues() : block ? blockCues(block, speed, splitMs) : [];
    return applyTrigger(cues, speed);
  }, [block, blocks, scene, speed]);

  const total = beats.length ? beats[beats.length - 1].at + beats[beats.length - 1].ms : 1;
  const current = beatAt(beats, elapsed);
  const progress = current ? Math.min(1, Math.max(0, (elapsed - current.at) / Math.max(1, current.ms))) : 0;
  const active = litNow(current, done ? 1 : progress);
  const trig = current?.trigger ?? "released";
  const beatKeys = active.filter((item) => item.kind === "key").map((item) => item.code);
  if (camScene.current !== scene?.id) {
    camScene.current = scene?.id ?? "";
    camRef.current = [];
  }
  if (beatKeys.length) camRef.current = beatKeys;
  const cameraCodes = beatKeys.length ? beatKeys : camRef.current;
  const cluster = useMemo(() => clusterAround(cameraCodes), [cameraCodes.join("|")]);
  const mouseCodes = useMemo(() => {
    const found = new Set<string>();
    for (const beat of beats) {
      for (const item of [...beat.lit, ...beat.fade]) {
        if (item.kind === "mouse") found.add(item.code);
      }
    }
    return [...found];
  }, [beats]);

  const frame = useMemo(() => {
    const shown = [...cluster.hot, ...cluster.near];
    if (!shown.length || cut.w < 8 || cut.h < 8) return null;
    const minX = Math.min(...shown.map((spot) => spot.x)) - 0.18;
    const minY = Math.min(...shown.map((spot) => spot.y)) - 0.16;
    const maxX = Math.max(...shown.map((spot) => spot.x + spot.w)) + 0.18;
    const maxY = Math.max(...shown.map((spot) => spot.y + spot.h)) + 0.16;
    const bw = Math.max(0.8, maxX - minX);
    const bh = Math.max(0.8, maxY - minY);
    const scale = Math.min(cut.w / (bw * UNIT), cut.h / (bh * UNIT));
    const tx = (cut.w - bw * UNIT * scale) / 2 - minX * UNIT * scale;
    const ty = (cut.h - bh * UNIT * scale) / 2 - minY * UNIT * scale;
    return { scale, tx, ty };
  }, [cluster, cut]);

  useEffect(() => {
    const el = cutRef.current;
    if (!el) return;
    const measure = () => setCut({ w: el.clientWidth, h: el.clientHeight });
    measure();
    const obs = new ResizeObserver(measure);
    obs.observe(el);
    return () => obs.disconnect();
  }, [scenes.length]);

  useEffect(() => {
    onScene(scene?.nodeId ?? "");
  }, [scene?.nodeId, onScene]);

  useEffect(() => {
    const next = scenes.findIndex((item) => item.nodeId === focusId);
    if (next < 0) return;
    if (boot.current) {
      boot.current = false;
      setIndex(next);
      return;
    }
    setIndex(next);
    setRun((value) => value + 1);
  }, [focusId, focusNonce]);

  useEffect(() => {
    setDone(false);
    setElapsed(0);
    let cancelled = false;
    let frameId = 0;
    let last = performance.now();
    let time = 0;
    const tick = (now: number) => {
      if (cancelled) return;
      time = Math.min(total, time + (now - last));
      last = now;
      setElapsed(time);
      if (time < total) frameId = requestAnimationFrame(tick);
      else {
        setElapsed(total);
        setDone(true);
      }
    };
    frameId = requestAnimationFrame(tick);
    return () => {
      cancelled = true;
      cancelAnimationFrame(frameId);
    };
  }, [index, playMode, run, total, scene?.id, scene?.kind]);

  const nextId = index < scenes.length - 1 ? scenes[index + 1]?.nodeId : "";
  const hotIds = new Set(cluster.hot.map(spotId));
  const nearIds = new Set(cluster.near.map(spotId));
  const litCodes = new Set(active.filter((item) => item.kind === "key").map((item) => item.code));
  const litMouse = new Set(active.filter((item) => item.kind === "mouse").map((item) => item.code));
  const boardW = Math.max(...spots.map((spot) => spot.x + spot.w), 1) * UNIT;
  const boardH = Math.max(...spots.map((spot) => spot.y + spot.h), 1) * UNIT;
  const trigWord = trig === "released" ? "Released" : "Held";

  return (
    <div className="kb-dock" onPointerDown={(event) => event.stopPropagation()}>
      <section className={`kb-trig is-${trig}`} aria-label="Trigger state">
        <span className={`kb-trig-face${trig !== "released" ? " is-on" : ""}`}>
          <KeyFace label={bind} empty={!trigger} />
        </span>
        <div className="kb-trig-copy">
          <strong>{trigWord}</strong>
          <span className={`kb-trig-bar is-${trig}`} aria-hidden="true">
            <i />
          </span>
        </div>
      </section>
      <div className="kb-cut" ref={cutRef} aria-hidden="true">
        <div
          className="kb-board"
          style={{
            width: boardW,
            height: boardH,
            transform: frame ? `translate(${frame.tx}px, ${frame.ty}px) scale(${frame.scale})` : "scale(0.2)",
          }}
        >
          {spots.map((spot) => {
            const id = spotId(spot);
            const hot = hotIds.has(id);
            const down = litCodes.has(spot.code) && hot;
            const near = nearIds.has(id);
            const tone = down ? " is-down" : hot ? " is-hot" : near ? " is-near" : " is-gone";
            return (
              <span
                key={id}
                className={`kb-key${tone}`}
                style={{ left: spot.x * UNIT, top: spot.y * UNIT, width: Math.max(8, spot.w * UNIT - 5), height: Math.max(8, spot.h * UNIT - 5) }}
              >
                {spot.label}
              </span>
            );
          })}
        </div>
      </div>
      {mouseCodes.length ? (
        <div className="kb-mouse" aria-hidden="true">
          {mouseCodes.map((code) => (
            <span key={code} className={`kb-key is-mouse${litMouse.has(code) ? " is-down" : " is-near"}`}>
              {pressLabel(code)}
            </span>
          ))}
        </div>
      ) : null}
      <div className="kb-controls">
        <SlideToggle
          label="Preview speed"
          value={speed}
          options={[
            { id: "simple", label: "Simple" },
            { id: "real", label: "Real-time" },
          ]}
          onChange={(id) => setSpeed(id === "real" ? "real" : "simple")}
        />
        <div className="kb-actions">
          <button type="button" className="ghost" onClick={() => setRun((value) => value + 1)}>
            Replay
          </button>
          <button type="button" className="is-primary" disabled={!nextId} onClick={() => nextId && onPick?.(nextId)}>
            Next
          </button>
        </div>
      </div>
    </div>
  );
}
