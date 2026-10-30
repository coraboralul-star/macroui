import { RADIAL_SLOTS_MAX, RADIAL_SLOTS_MIN, RADIAL_WASD, normalizeRadial, type Macro, type Profile, type Radial, type Trigger } from "./profile";

/** 1 to 8 pick a slot while the menu is open. */
export const SLOT_KEYS = ["1", "2", "3", "4", "5", "6", "7", "8"];

/** The live config's first radial, the one the editor shows. */
export function activeRadial(profile: Profile): Radial | undefined {
  return profile.configs.find((config) => config.id === profile.activeId)?.radials?.[0];
}

/** Writes the editor's radial over the first one, or clears the list when undefined. */
export function setActiveRadial(profile: Profile, radial: Radial | undefined): Profile {
  const clean = radial ? normalizeRadial(radial) : undefined;
  const configs = profile.configs.map((config) => {
    if (config.id !== profile.activeId) return config;
    const { radials: old, ...rest } = config;
    const others = (old ?? []).slice(1);
    const radials = clean ? [clean, ...others] : others;
    return radials.length ? { ...rest, radials } : rest;
  });
  return { ...profile, configs };
}

/** 2 to 8. New slots are empty, dropped slots lose their macro. */
export function withSlotCount(radial: Radial, count: number): Radial {
  const n = Math.min(RADIAL_SLOTS_MAX, Math.max(RADIAL_SLOTS_MIN, Math.round(count)));
  const slots = radial.slots.slice(0, n);
  while (slots.length < n) slots.push("");
  return { ...radial, slots };
}

export function withSlot(radial: Radial, index: number, macroId: string): Radial {
  return { ...radial, slots: radial.slots.map((slot, i) => (i === index ? macroId : slot)) };
}

/** The keys Block movement holds back, in the order the editor shows them. */
export function blockedKeys(radial: Radial): string[] {
  if (radial.blockMove === "wasd") return RADIAL_WASD;
  if (radial.blockMove === "custom") return radial.blockKeys;
  return [];
}

function sameKey(a: string, b: string): boolean {
  return a.toLowerCase() === b.toLowerCase();
}

function keyName(key: string): string {
  return key.length === 1 ? key.toUpperCase() : key;
}

/** Why Block movement can't hold this key back, or "" when it can. Each one stops the menu arming. */
export function blockClash(radial: Radial, key: string): string {
  if (radial.trigger.kind === "key" && radial.trigger.button && sameKey(key, radial.trigger.button)) return "the trigger";
  if (radial.select !== "keys") return "";
  if (SLOT_KEYS.includes(key)) return "a slot key";
  if (sameKey(key, radial.prevKey)) return "the Previous key";
  if (sameKey(key, radial.nextKey)) return "the Next key";
  return "";
}

function sameTrigger(a: Trigger, b: Trigger): boolean {
  return Boolean(a.button) && a.button === b.button && (a.kind === "key") === (b.kind === "key");
}

/** Why the engines would not arm this radial, or why a slot will not play. Empty when it is ready. */
export function radialProblems(radial: Radial, macros: Macro[]): string[] {
  const out: string[] = [];
  const trigger = radial.trigger;
  if (!trigger.button) out.push("Pick a trigger. The menu does nothing until it has one.");
  const clash = macros.find((macro) => macro.enabled && sameTrigger(macro.trigger, trigger));
  if (clash) out.push(`${clash.name || "A macro"} also uses this trigger. Pick another trigger or move that macro.`);
  if (radial.select === "keys") {
    if (trigger.kind === "key" && SLOT_KEYS.includes(trigger.button))
      out.push("1 to 8 pick slots while the menu is open, so the trigger can't be one of them.");
    for (const [label, key] of [["Previous", radial.prevKey], ["Next", radial.nextKey]] as const) {
      if (SLOT_KEYS.includes(key)) out.push(`${label} can't be 1 to 8. Those pick slots.`);
      else if (trigger.kind === "key" && key === trigger.button) out.push(`${label} can't be the trigger.`);
    }
    if (radial.prevKey === radial.nextKey) out.push("Previous and Next need different keys.");
  }
  for (const key of blockedKeys(radial)) {
    const clash = blockClash(radial, key);
    if (clash) out.push(`${keyName(key)} is ${clash}, so Block movement can't block it.`);
  }
  const ids = new Set(macros.map((macro) => macro.id));
  if (!radial.slots.some((slot) => slot && ids.has(slot))) out.push("Put a macro in at least one slot.");
  radial.slots.forEach((slot, index) => {
    if (slot && !ids.has(slot)) out.push(`Slot ${index + 1}'s macro was deleted. That slot is empty.`);
  });
  return out;
}

/** Slot i's centre angle in degrees, clockwise from the top. Slot 0 is straight up. */
export function slotAngle(index: number, count: number): number {
  return (360 * index) / count;
}
