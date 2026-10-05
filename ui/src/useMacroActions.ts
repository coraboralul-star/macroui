import { applyPlayback } from "./blocks";
import { applyGap, blankMacro, renameActive, setFocusExe, switchConfig, type Macro, type PlayMode, type Profile, type TriggerKind } from "./profile";
import {
  addConfig,
  assignTrigger,
  deleteConfig,
  deleteMacro,
  freshTrigger,
  saveGraphMacro,
  saveStudioMacro,
} from "./profileEdits";

export type TriggerMenu = { kind: TriggerKind; code: string; x: number; y: number };

type Args = {
  profile: Profile;
  selected: string;
  graphId: string | null;
  menu: TriggerMenu | null;
  update: (next: Profile) => void;
  saveMacro: (next: Macro) => void;
  setSelected: (id: string) => void;
  setPicked: (code: string | null) => void;
  setMenu: (menu: TriggerMenu | null) => void;
  setGraphId: (id: string | null) => void;
  openAdvanced: () => void;
};

export function useMacroActions({
  profile,
  selected,
  graphId,
  menu,
  update,
  saveMacro,
  setSelected,
  setPicked,
  setMenu,
  setGraphId,
  openAdvanced,
}: Args) {
  const menuMacro = menu
    ? profile.macros.find((item) => item.trigger.kind === menu.kind && item.trigger.button === menu.code) ?? null
    : null;

  const pickTrigger = (kind: TriggerKind, code: string) => {
    setPicked(code);
    const hit = profile.macros.find((item) => item.trigger.kind === kind && item.trigger.button === code);
    if (hit) setSelected(hit.id);
  };

  const assignMacro = (kind: TriggerKind, code: string, macroId: string) => {
    update(assignTrigger(profile, kind, code, macroId));
    if (macroId) setSelected(macroId);
  };

  const bindTrigger = (kind: TriggerKind, code: string, make: (current: Macro | null) => Macro) => {
    const current = profile.macros.find((item) => item.trigger.kind === kind && item.trigger.button === code) ?? null;
    saveMacro(make(current));
  };

  return {
    menuMacro,
    pickTrigger,
    profiles: {
      select(id: string) {
        const next = switchConfig(profile, id);
        update(next);
        setSelected(next.macros[0]?.id ?? "");
        setPicked(null);
        setMenu(null);
      },
      add() {
        update(addConfig(profile));
        setSelected("");
        setPicked(null);
        setMenu(null);
      },
      remove(id: string) {
        if (profile.configs.length < 2) return;
        const next = deleteConfig(profile, id);
        update(next);
        setSelected(next.macros[0]?.id ?? "");
        setMenu(null);
      },
      rename(name: string) {
        update(renameActive(profile, name));
      },
      setFocus(exe: string) {
        update(setFocusExe(profile, exe));
      },
    },
    setSwapClicks(next: boolean) {
      update({ ...profile, swapClicks: next });
    },
    studio: {
      select(id: string) {
        setSelected(id);
        const item = profile.macros.find((macro) => macro.id === id);
        if (item?.trigger.button) setPicked(item.trigger.button);
      },
      add() {
        saveMacro(blankMacro());
      },
      change(next: Macro) {
        update(saveStudioMacro(profile, next));
        setSelected(next.id);
      },
      remove() {
        const next = deleteMacro(profile, selected);
        update(next);
        setSelected(next.macros[0]?.id ?? "");
      },
    },
    graph: {
      change(next: Macro) {
        update(saveGraphMacro(profile, next));
        setSelected(next.id);
      },
      remove() {
        if (!graphId) return;
        const next = deleteMacro(profile, graphId);
        update(next);
        setSelected(next.macros[0]?.id ?? "");
        setGraphId(null);
      },
    },
    menu: {
      create() {
        if (!menu) return;
        const created = menuMacro ?? freshTrigger(menu.kind, menu.code, { advanced: true });
        saveMacro({ ...created, advanced: true });
        openAdvanced();
      },
      edit() {
        if (!menuMacro) return;
        saveMacro({ ...menuMacro, advanced: true });
        setSelected(menuMacro.id);
        openAdvanced();
      },
      clear() {
        if (!menu || !menuMacro) return;
        assignMacro(menu.kind, menu.code, "");
      },
      assign(macroId: string) {
        if (!menu) return;
        assignMacro(menu.kind, menu.code, macroId);
      },
      play(mode: PlayMode) {
        if (!menuMacro || menuMacro.advanced) return;
        saveMacro(applyPlayback(menuMacro, mode));
      },
      gap(ms: number) {
        if (!menu) return;
        bindTrigger(menu.kind, menu.code, (current) => {
          const base = current ?? freshTrigger(menu.kind, menu.code, { basic: true, playMode: "whileHeld" });
          return applyGap({ ...base, basic: current ? current.basic : true }, ms);
        });
      },
      times(count: number) {
        if (!menu) return;
        bindTrigger(menu.kind, menu.code, (current) => ({
          ...(current ?? freshTrigger(menu.kind, menu.code, { playMode: "repeat" })),
          repeatCount: count,
        }));
      },
    },
  };
}
