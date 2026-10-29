import { playModeChip } from "../macroFlow";
import type { Macro } from "../profile";

/** The saved playMode, as the engines read it. */
export function PlayChip({ macro }: { macro: Macro }) {
  const chip = playModeChip(macro);
  return (
    <span className="play-chip" data-mode={macro.playMode} title={chip.detail} aria-label={`Plays: ${chip.label}. ${chip.detail}`}>
      <i>Plays</i>
      {chip.label}
    </span>
  );
}
