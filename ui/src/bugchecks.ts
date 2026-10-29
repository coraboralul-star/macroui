import { applyDelayMode, expandTaps, isSleep, laneCells, scrollDy, setDelay } from "./eventLane";
import { appendSteps, insertTyped, withBlocks } from "./blocks";
import { playModeChip, shownBlocks } from "./macroFlow";
import { activeTurn, blankMacro, blankStep, normalizeProfile, repairedModes, setActiveTurn, turnCounts, writeActive, type Block, type Macro, type Profile, type Step, type Trigger } from "./profile";
import { useMacroActions, type TriggerMenu } from "./useMacroActions";
import { parseTurn } from "./hostMessages";
import { TURN_INPUT_DEFAULT, inputLabel, nudge } from "./components/TurnPanel";
import { vkOf } from "./vk";
import { deleteMacro, macroIssues, upsertMacro } from "./profileEdits";
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

/** fixture is engine-native/tests/fixtures/create-paths.json, which both engines also play. */
export function runBugChecks(fixture?: unknown): string[] {
  failures.length = 0;
  checkDelayMode();
  checkScroll();
  checkDeleteMacro();
  checkBrokenRepeat();
  checkTurn();
  checkCreatePaths(fixture);
  checkCanonicalLoad();
  return failures.slice();
}

// One pass, about 190 ms: space, then e. The engine tests count both.
const PASS: Step[] = [
  { type: "key", action: "down", key: "space" },
  { type: "wait", ms: 20 },
  { type: "key", action: "up", key: "space" },
  { type: "wait", ms: 150 },
  { type: "key", action: "down", key: "e" },
  { type: "wait", ms: 20 },
  { type: "key", action: "up", key: "e" },
];

/** The app's state and actions, with the setters React would own. */
function app(menu: TriggerMenu | null = null) {
  const state = { profile: normalizeProfile({ version: 1, name: "T", variables: {}, macros: [] }), selected: "" };
  const update = (next: Profile) => {
    state.profile = writeActive(next, next.macros);
  };
  const actions = (at: TriggerMenu | null = menu) =>
    useMacroActions({
      profile: state.profile,
      selected: state.selected,
      graphId: null,
      menu: at,
      update,
      saveMacro: (next) => {
        update(upsertMacro(state.profile, next));
        state.selected = next.id;
      },
      setSelected: (id) => {
        state.selected = id;
      },
      setPicked() {},
      setMenu() {},
      setGraphId() {},
      openAdvanced() {},
    });
  const current = () => state.profile.macros.find((item) => item.id === state.selected) as Macro;
  return { state, actions, current };
}

/** GraphEditor: Library > Run Once, then the steps land in it. Blocks come from shownBlocks, as on screen. */
function graphRunOnce(macro: Macro): Macro {
  const added = insertTyped(shownBlocks(macro), 0, "steps");
  const withBlock = withBlocks(macro, added.blocks);
  return withBlocks(withBlock, shownBlocks(withBlock).map((block) => (block.id === added.pick ? appendSteps(block, PASS) : block)));
}

type Choice = { name: string; trigger: Trigger };

const keyMenu = (trigger: Trigger): TriggerMenu => ({ kind: trigger.kind, code: trigger.button, x: 0, y: 0 });

/** Every way to make a Run Once macro. Each returns the saved macro. */
const CREATE_PATHS: Record<string, (choice: Choice) => Macro> = {
  // Remap or Mouse: click the trigger > New macro > Advanced > Run Once.
  "trigger menu > New macro": ({ name, trigger }) => {
    const ui = app(keyMenu(trigger));
    ui.actions().menu.create();
    ui.actions().graph.change(graphRunOnce({ ...ui.current(), name }));
    return ui.current();
  },
  // Macro Editor > New > Advanced > Run Once > Assign trigger.
  "Macro Editor > New > Advanced": ({ name, trigger }) => {
    const ui = app();
    ui.actions().studio.add();
    ui.actions().graph.change({ ...graphRunOnce({ ...ui.current(), name }), trigger });
    return ui.current();
  },
  // Macro Editor > New, record in Simple, Save, then Advanced. The steps are already a Run Once.
  "Macro Editor > New > Simple > Advanced": ({ name, trigger }) => {
    const ui = app();
    ui.actions().studio.add();
    ui.actions().studio.change({ ...ui.current(), name, steps: PASS });
    const shown = ui.current();
    ui.actions().graph.change({ ...withBlocks(shown, shownBlocks(shown)), trigger });
    return ui.current();
  },
  // A Simple macro assigned from the trigger menu, then the menu's Macro Editor.
  "Simple macro > trigger menu > Macro Editor": ({ name, trigger }) => {
    const ui = app();
    ui.actions().studio.add();
    ui.actions().studio.change({ ...ui.current(), name, steps: PASS });
    ui.actions(keyMenu(trigger)).menu.assign(ui.state.selected);
    ui.actions(keyMenu(trigger)).menu.edit();
    return ui.current();
  },
  // Add a Tap Hold block, then remove it. Its whileHeld must not stay behind.
  "Tap Hold added then removed": ({ name, trigger }) => {
    const ui = app();
    ui.actions().studio.add();
    const made = graphRunOnce({ ...ui.current(), name, trigger });
    const tapped = withBlocks(made, insertTyped(made.blocks ?? [], 1, "tapHold").blocks);
    check(tapped.playMode === "whileHeld", "a Tap Hold block makes the macro whileHeld");
    ui.actions().graph.change(withBlocks(tapped, (tapped.blocks ?? []).filter((block) => block.type !== "tapHold")));
    return ui.current();
  },
};

/** Ids are random per run. Everything else must match. */
function masked(macro: Macro, id: string): Macro {
  return JSON.parse(JSON.stringify({ ...macro, id, blocks: (macro.blocks ?? []).map((block, index) => ({ ...block, id: `${id}-b${index + 1}` })) }));
}

/** Trigger menu > Play > Repeat, Times 3, on a recorded Simple macro. */
function repeatThree(trigger: Trigger): Macro {
  const ui = app(keyMenu(trigger));
  ui.actions().studio.add();
  ui.actions().studio.change({ ...ui.current(), name: "Repeat 3", steps: PASS.slice(0, 4) });
  ui.actions().menu.assign(ui.state.selected);
  ui.actions().menu.times(3);
  ui.actions().menu.play("repeat");
  return ui.current();
}

/** What engine-native/tests/fixtures/create-paths.json holds: one macro per creation path. */
export function createPathsFixture(): Profile {
  const macros = [
    masked(CREATE_PATHS["trigger menu > New macro"]({ name: "Key path", trigger: { kind: "key", button: "g" } }), "key-path"),
    masked(CREATE_PATHS["Macro Editor > New > Advanced"]({ name: "New path", trigger: { kind: "key", button: "h" } }), "new-path"),
    masked(repeatThree({ kind: "key", button: "j" }), "repeat-3"),
  ];
  return normalizeProfile({ version: 1, name: "Create paths", variables: {}, activeId: "paths", configs: [{ id: "paths", name: "Create paths", macros }], macros });
}

function checkCreatePaths(fixture: unknown) {
  for (const trigger of [{ kind: "key", button: "g" }, { kind: "side", button: "XButton2" }] as Trigger[]) {
    const made = Object.entries(CREATE_PATHS).map(([path, create]) => [path, masked(create({ name: "Test", trigger }), "m")] as const);
    const [firstPath, first] = made[0];
    check(first.playMode === "once" && first.advanced === true && first.blocks?.[0]?.type === "steps", `${firstPath} is a Run Once`);
    for (const [path, macro] of made.slice(1)) same(macro, first, `${path} on ${trigger.button} saves the same macro as ${firstPath}`);
  }

  const repeat = repeatThree({ kind: "key", button: "j" });
  const block = repeat.blocks?.[0];
  check(repeat.playMode === "once" && block?.type === "repeat" && block.count === 3, "Play > Repeat x3 counts once, in its repeat block (not 3 x 3)");

  const pressed = app();
  pressed.actions().studio.add();
  pressed.actions().studio.change({ ...pressed.current(), playMode: "whileHeld", steps: PASS });
  pressed.actions(keyMenu({ kind: "key", button: "k" })).menu.assign(pressed.state.selected);
  pressed.actions(keyMenu({ kind: "key", button: "k" })).menu.edit();
  const held = pressed.current();
  check(held.playMode === "whileHeld" && held.blocks?.length === 1 && held.blocks[0].type === "whileHeld",
    "a Simple While pressed macro opens in Advanced as a While Held block, so it plays the same");
  same(playModeChip(held).label, "While held", "the chip shows the saved mode");

  if (fixture === undefined) return;
  same(normalizeProfile(fixture).macros, createPathsFixture().macros,
    "engine-native/tests/fixtures/create-paths.json is what the UI makes (npm run check -- --write-fixture)");
}

function checkCanonicalLoad() {
  const steps = [{ type: "steps", steps: PASS }];
  const held = [{ type: "whileHeld", mute: [], steps: PASS }];
  const saved = (patch: Partial<Macro> & { blocks?: unknown[] }) => ({ ...blankMacro(), advanced: true, name: patch.id, ...patch });
  const raw = [
    // Saved by Macro Editor > New before the fix.
    saved({ id: "legacy-new", playMode: "whileHeld", blocks: steps as unknown as Block[] }),
    saved({ id: "legacy-flat", playMode: "whileHeld", steps: PASS }),
    saved({ id: "hold", playMode: "whileHeld", blocks: held as unknown as Block[] }),
    saved({ id: "hold-once", playMode: "once", blocks: held as unknown as Block[] }),
    saved({ id: "tap", playMode: "once", blocks: [{ type: "tapHold", key: "z", watch: ["t"] }] as Block[] }),
    saved({ id: "double", playMode: "repeat", repeatCount: 3, blocks: [{ type: "repeat", count: 3, steps: PASS }] as Block[] }),
    saved({ id: "plain-repeat", playMode: "repeat", repeatCount: 3, blocks: steps as unknown as Block[] }),
    saved({ id: "toggle", playMode: "toggle", blocks: steps as unknown as Block[] }),
    { ...blankMacro(), id: "simple-held", playMode: "whileHeld", steps: PASS },
  ];
  const once = normalizeProfile({ version: 1, name: "Old", variables: {}, macros: raw });
  const modes = Object.fromEntries(once.macros.map((item) => [item.id, item.playMode]));
  same(modes, {
    "legacy-new": "once",
    "legacy-flat": "once",
    hold: "whileHeld",
    "hold-once": "once",
    tap: "whileHeld",
    double: "once",
    "plain-repeat": "repeat",
    toggle: "toggle",
    "simple-held": "whileHeld",
  }, "load repairs only modes the blocks contradict");
  same(normalizeProfile(JSON.parse(JSON.stringify(once))), once, "the repair is idempotent");
  const old = { version: 1, name: "Old", variables: {}, macros: raw };
  same(repairedModes(old, once), [
    "Old / legacy-new: whileHeld -> once",
    "Old / legacy-flat: whileHeld -> once",
    "Old / tap: once -> whileHeld",
    "Old / double: repeat -> once",
  ], "the load lists what it repaired, so the page saves it");
  same(repairedModes(JSON.parse(JSON.stringify(once)), once), [], "a repaired profile loads without another save");
  check(once.macros[0] === once.configs[0].macros[0], "the live macros are the repaired ones");
}

function checkTurn() {
  same(blankStep("turn"), { type: "turn", degrees: 180, dir: "right", ms: 0 }, "a new turn is 180 right, one instant move");
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
    { type: "turn", degrees: 3600, dir: "right", ms: 0 },
    { type: "turn", degrees: 180, dir: "right", ms: 0 },
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
  check(TURN_INPUT_DEFAULT === "XButton2" && inputLabel(TURN_INPUT_DEFAULT) === "M5", "calibration defaults to M5");
  check(vkOf("XButton2") === 0x06 && vkOf("XButton1") === 0x05 && vkOf("MButton") === 0x04, "mouse buttons are their vks");
  check(vkOf("LButton") === 0 && vkOf("RButton") === 0, "left and right click are not a calibration input");
  check(vkOf("f10") === 0x79 && vkOf("q") === 0x51 && vkOf("7") === 0x37 && vkOf("LShift") === 0xa0 && vkOf("Numpad3") === 0x63 && vkOf("PgDn") === 0x22 && vkOf("`") === 0xc0, "captured keys map to vks");
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
