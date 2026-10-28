import { applyDelayMode, expandTaps, isSleep, laneCells, scrollDy, setDelay } from "./eventLane";
import { activeTurn, blankMacro, blankStep, normalizeProfile, setActiveTurn, turnCounts, type Block, type Macro, type Profile, type Step } from "./profile";
import { parseTurn } from "./hostMessages";
import { nudge } from "./components/TurnPanel";
import { deleteMacro, macroIssues } from "./profileEdits";
import { readList, segment } from "./tape";

const failures: string[] = [];

function check(ok: boolean, label: string) {
  if (!ok) failures.push(label);
}

function same(actual: unknown, expected: unknown, label: string) {
  const left = JSON.stringify(actual);
  const right = JSON.stringify(expected);
  if (left !== right) failures.push(`${label}\n  actual   ${left}\n  expected ${right}`);
}

function macro(patch: Partial<Macro> & Pick<Macro, "id">): Macro {
  return { ...blankMacro(), name: patch.id, trigger: { kind: "key", button: "" }, ...patch };
}

export function runBugChecks(): string[] {
  failures.length = 0;
  checkDelayMode();
  checkScroll();
  checkDeleteMacro();
  checkBrokenRepeat();
  checkTurn();
  return failures.slice();
}

function checkTurn() {
  same(blankStep("turn"), { type: "turn", degrees: 180, dir: "right", ms: 30 }, "a new turn is 180 right in 30 ms");
  const saved = {
    version: 1,
    name: "Games",
    variables: {},
    activeId: "ow",
    macros: [],
    configs: [
      {
        id: "ow",
        name: "Overwatch",
        turn: { countsPer360: 16364.4 },
        macros: [macro({ id: "spin", steps: [{ type: "turn", degrees: 9999, dir: "up", ms: -4 } as unknown as Step, { type: "turn" } as unknown as Step] })],
      },
      { id: "fn", name: "Fortnite", turn: { countsPer360: 0 }, macros: [] },
    ],
  };
  const profile = normalizeProfile(saved);
  same(profile.configs[0].turn, { countsPer360: 16364 }, "a config keeps its calibration, rounded");
  check(profile.configs[1].turn === undefined, "a 0 calibration is not calibrated");
  same(profile.macros[0].steps, [
    { type: "turn", degrees: 3600, dir: "right", ms: 30 },
    { type: "turn", degrees: 180, dir: "right", ms: 30 },
  ], "turn steps clamp degrees and default the rest");
  const flat = normalizeProfile({ version: 1, name: "Old", variables: {}, macros: [], turn: { countsPer360: 3600 } });
  same(activeTurn(flat), { countsPer360: 3600 }, "a profile with no configs moves its turn onto its config");
  const set = setActiveTurn(profile, 20000);
  same(activeTurn(set), { countsPer360: 20000 }, "setting the calibration changes only the live config");
  check(set.configs[1].turn === undefined, "the other config is untouched");
  check(activeTurn(setActiveTurn(set, 0)) === undefined, "0 clears the calibration");
  check(turnCounts(180, 16364) === 8182 && turnCounts(90.5, 16364) === 4114, "counts round like the engines");
  check(nudge(16364, 0.1) === 16380 && nudge(16364, -1) === 16200 && nudge(500, -0.1) === 499, "a nudge moves at least one count");
  same(parseTurn({ type: "turn", state: "done", counts: 16500 }), { state: "done", counts: 16500, detail: "" }, "the shell's result is read");
  check(parseTurn({ type: "turn", state: "bogus" }) === null, "an unknown calibration state is ignored");
  check(macroIssues(macro({ id: "t", steps: [{ type: "turn", degrees: 0, dir: "left", ms: 0 }] })).length === 1, "a 0 degree turn cannot be saved");
}

function checkDelayMode() {
  const steps: Step[] = [
    { type: "key", action: "down", key: "a" },
    { type: "randomWait", minMs: 40, maxMs: 70 },
    { type: "wait", ms: 15 },
    { type: "scanWait", ms: 80 },
    { type: "key", action: "tap", key: "b", holdMs: 20 },
    { type: "repeat", count: 2, steps: [{ type: "randomWait", minMs: 5, maxMs: 9 }, { type: "goto", x: 1, y: 2, ms: 15 }] },
  ];
  same(applyDelayMode(steps, "none", 0), [
    { type: "key", action: "down", key: "a" },
    { type: "key", action: "down", key: "b" },
    { type: "key", action: "up", key: "b" },
    { type: "repeat", count: 2, steps: [{ type: "goto", x: 1, y: 2, ms: 15 }] },
  ], "no delay drops random wait, wait, and scan wait");
  same(applyDelayMode(steps, "fixed", 25), [
    { type: "key", action: "down", key: "a" },
    { type: "wait", ms: 25 },
    { type: "wait", ms: 25 },
    { type: "scanWait", ms: 25 },
    { type: "key", action: "tap", key: "b", holdMs: 25 },
    { type: "repeat", count: 2, steps: [{ type: "wait", ms: 25 }, { type: "goto", x: 1, y: 2, ms: 15 }] },
  ], "fixed delay turns random wait into a wait and leaves go-to timing");
  const random: Step = { type: "randomWait", minMs: 40, maxMs: 70 };
  check(!isSleep(random), "random wait is not a sleep step");
  same(setDelay([random], 0, 10, "wait"), [random], "editing a delay chip does not rewrite random wait");
  const cells = laneCells([{ type: "key", action: "down", key: "a" }, random]);
  check(cells.some((cell) => cell.type === "randomWait"), "random wait keeps its own chip");
  check(!cells.some((cell) => cell.type === "delay"), "random wait is not drawn as a delay chip");
}

function checkScroll() {
  check(scrollDy(-3, 0) === -1, "clearing notches on an up scroll stays up");
  check(scrollDy(-3, 4) === -4, "an up scroll keeps its direction");
  check(scrollDy(3, 0) === 1, "clearing notches on a down scroll stays down");
  check(scrollDy(3, 2) === 2, "a down scroll keeps its direction");
  check(scrollDy(0, 3) === 3, "a stored zero has already lost its up direction");
}

function checkDeleteMacro() {
  const held: Block = {
    id: "held",
    type: "whileHeld",
    steps: [{ type: "run", macroId: "a" }],
    mute: ["a", "c"],
  };
  const swapped: Block = {
    id: "swap",
    type: "swapAfter",
    afterMs: 150,
    steps: [{ type: "repeat", count: 2, steps: [{ type: "run", macroId: "a" }] }],
    mute: ["a"],
  };
  const loop: Block = {
    id: "loop",
    type: "repeat",
    count: 1,
    steps: [{ type: "run", macroId: "c" }],
  };
  const keeper = macro({
    id: "b",
    name: "B",
    steps: [
      { type: "run", macroId: "a" },
      { type: "repeat", count: 2, steps: [{ type: "run", macroId: "a" }, { type: "key", action: "down", key: "x" }] },
      { type: "run", macroId: "c" },
    ],
    blocks: [held, swapped, loop],
  });
  const other = macro({ id: "c", name: "C", steps: [{ type: "run", macroId: "a" }] });
  const base = normalizeProfile({
    version: 1,
    name: "T",
    macros: [macro({ id: "a", name: "A" }), keeper, other],
  });
  const parked = structuredClone(base.macros);
  const profile: Profile = {
    ...base,
    configs: [...base.configs, { id: "other", name: "Other", macros: parked, focusExe: "" }],
  };
  const next = deleteMacro(profile, "a");
  check(!next.macros.some((item) => item.id === "a"), "deleted macro is gone");
  const kept = next.macros.find((item) => item.id === "b");
  same(kept?.steps, [
    { type: "run", macroId: "" },
    { type: "repeat", count: 2, steps: [{ type: "run", macroId: "" }, { type: "key", action: "down", key: "x" }] },
    { type: "run", macroId: "c" },
  ], "run steps that pointed at the deleted macro are cleared");
  const nextHeld = kept?.blocks?.find((block) => block.id === "held");
  const nextSwap = kept?.blocks?.find((block) => block.id === "swap");
  const nextLoop = kept?.blocks?.find((block) => block.id === "loop");
  check(nextHeld?.type === "whileHeld" && nextHeld.mute.length === 1 && nextHeld.mute[0] === "c", "while-held mute drops the deleted id");
  check(nextHeld?.type === "whileHeld" && nextHeld.steps[0]?.type === "run" && nextHeld.steps[0].macroId === "", "while-held run step is cleared");
  check(nextSwap?.type === "swapAfter" && nextSwap.mute.length === 0, "swap-after mute drops the deleted id");
  check(
    nextSwap?.type === "swapAfter" && nextSwap.steps[0]?.type === "repeat" && nextSwap.steps[0].steps[0]?.type === "run" && nextSwap.steps[0].steps[0].macroId === "",
    "a nested run inside a block is cleared",
  );
  check(nextLoop?.type === "repeat" && nextLoop.steps[0]?.type === "run" && nextLoop.steps[0].macroId === "c", "a run of a macro that still exists stays");
  check(macroIssues(kept ?? keeper).includes("A run step needs a macro."), "an emptied run step blocks save");
  const untouched = next.configs.find((config) => config.id === "other");
  const parkedRun = untouched?.macros.find((item) => item.id === "b")?.steps[0];
  check(Boolean(untouched?.macros.some((item) => item.id === "a")), "another profile still has the macro");
  check(parkedRun?.type === "run" && parkedRun.macroId === "a", "another profile keeps its run step");
}

function checkBrokenRepeat() {
  const loaded = normalizeProfile({
    version: 1,
    name: "Hand",
    macros: [{
      id: "m",
      name: "Hand",
      trigger: { kind: "key", button: "f" },
      blocks: [{ id: "held", type: "whileHeld", mute: [], steps: [{ type: "repeat", count: "2" }] }],
      steps: [
        { type: "repeat", count: 2 },
        { type: "repeat", count: 0, steps: null },
        { type: "repeat", count: -3, steps: [{ type: "wait", ms: "50" }, { type: "repeat" }] },
        { type: "wait", ms: -1 },
        { type: "wait", ms: "12.6" },
        { type: "scanWait", ms: "8" },
        { type: "move", x: "4.2", y: "no" },
        { type: "goto", x: 1.2, y: -2.6, ms: -5, where: "nope" },
        { type: "goto", x: 3, y: 4 },
        { type: "scroll", dy: -3 },
        { type: "future", hello: true },
      ],
    }],
  });
  const hand = loaded.macros[0];
  same(hand?.steps, [
    { type: "repeat", count: 2, steps: [] },
    { type: "repeat", count: 0, steps: [] },
    { type: "repeat", count: 0, steps: [{ type: "wait", ms: 50 }, { type: "repeat", count: 1, steps: [] }] },
    { type: "wait", ms: 0 },
    { type: "wait", ms: 13 },
    { type: "scanWait", ms: 8 },
    { type: "move", x: 4, y: 0 },
    { type: "goto", x: 1, y: -3, ms: 0, where: "screen" },
    { type: "goto", x: 3, y: 4 },
    { type: "scroll", dy: -3 },
    { type: "future", hello: true },
  ], "a hand-edited repeat and its neighbours are coerced");
  const held = hand?.blocks?.[0];
  check(held?.type === "whileHeld" && held.steps[0]?.type === "repeat" && Array.isArray(held.steps[0].steps), "a block repeat without steps becomes an empty list");
  let issues: string[] = [];
  try {
    issues = macroIssues(hand ?? blankMacro());
  } catch (error) {
    failures.push(`opening a broken repeat throws: ${error instanceof Error ? error.message : String(error)}`);
  }
  check(issues.length === 0, "a coerced repeat has nothing for save to reject");
  const bare = macro({ id: "bare", steps: [{ type: "repeat", count: 2 } as Step] });
  try {
    macroIssues(bare);
    expandTaps(bare.steps);
    applyDelayMode(bare.steps, "none", 0);
    applyDelayMode(bare.steps, "fixed", 10);
    const cells = laneCells(bare.steps);
    const repeat = cells.find((cell) => cell.type === "repeat");
    check(repeat?.type === "repeat" && Array.isArray(repeat.steps), "the lane gets an empty repeat instead of a missing list");
  } catch (error) {
    failures.push(`rendering a repeat with no steps throws: ${error instanceof Error ? error.message : String(error)}`);
  }
  try {
    macroIssues({ ...blankMacro(), steps: undefined as unknown as Step[] });
  } catch (error) {
    failures.push(`missing macro steps throw: ${error instanceof Error ? error.message : String(error)}`);
  }
  const unknown = segment([{ type: "future", count: 9 } as unknown as Step]);
  check(unknown.length === 0, "an unknown step is not drawn as a repeat");
  const known = segment([{ type: "repeat", count: 2, steps: [] }]);
  check(known.length === 1 && known[0]?.type === "repeat" && known[0].steps.length === 0, "a real repeat is still a repeat");
  check(readList([{ type: "repeat", count: 1 } as Step], [0]).length === 0, "reading a repeat with no steps returns an empty list");
}
