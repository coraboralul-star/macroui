import type { Macro } from "../profile";
import { MAIN, NAV, NUM } from "../keyboard";

function units(w: number) {
  return { ["--w" as string]: String(w) };
}

export function Keyboard({
  macros,
  picked,
  onPick,
  onAssign,
}: {
  macros: Macro[];
  picked: string | null;
  onPick: (code: string) => void;
  onAssign: (code: string, x: number, y: number) => void;
}) {
  const bound = new Set(
    macros.filter((macro) => macro.trigger.kind === "key").map((macro) => macro.trigger.button),
  );

  const renderKey = (
    cell: { label: string; code: string; caption?: string },
    reactKey: string,
    extra?: Record<string, string | number>,
  ) => {
    const on = bound.has(cell.code);
    const active = picked === cell.code;
    return (
      <button
        key={reactKey}
        type="button"
        className={`keycap${on ? " is-bound" : ""}${active ? " is-picked" : ""}`}
        style={extra}
        title={cell.label}
        onClick={(event) => {
          const rect = event.currentTarget.getBoundingClientRect();
          onPick(cell.code);
          onAssign(cell.code, rect.left, rect.bottom + 6);
        }}
        onContextMenu={(event) => event.preventDefault()}
      >
        {cell.caption ?? cell.label}
      </button>
    );
  };

  return (
    <div className="board" aria-label="Keyboard">
      <div className="kb-main">
        {MAIN.map((row, index) => (
          <div key={index} className="board-row">
            {row.map((cell, cellIndex) => {
              if ("gap" in cell) return <span key={cellIndex} className="key-gap" style={units(cell.gap)} />;
              return renderKey(cell, `${cell.code}-${cellIndex}`, units(cell.w ?? 1));
            })}
          </div>
        ))}
      </div>
      <div className="kb-cluster kb-nav">
        {NAV.map((row, index) => (
          <div key={index} className="board-row kb-nav-row">
            {row.map((cell, cellIndex) =>
              cell ? (
                renderKey(cell, `${cell.code}-${index}`, units(1))
              ) : (
                <span key={`empty-${index}-${cellIndex}`} className="key-gap" style={units(1)} />
              ),
            )}
          </div>
        ))}
      </div>
      <div className="kb-cluster kb-num">
        <div className="kb-num-spacer" />
        <div className="kb-num-grid">
          {NUM.map((cell) =>
            renderKey(cell, cell.code, {
              gridColumn: `${cell.col} / span ${cell.w ?? 1}`,
              gridRow: `${cell.row} / span ${cell.h ?? 1}`,
            }),
          )}
        </div>
      </div>
    </div>
  );
}
