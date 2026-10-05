import { heldName, place, showBriefRelease, shownBlocks } from "../macroFlow";
import type { Block, Macro, Step } from "../profile";
import { BlockLead } from "./BlockLead";
import { EventLane } from "./EventLane";
import { KeyFace } from "./KeyFace";
import { ReleaseField } from "./ReleaseField";

export function MacroBrief({
  macro,
  macros,
  onChange,
  onDelete,
  onOpenGraph,
  onSave,
  saveDisabled,
  saveTitle,
  issue,
}: {
  macro: Macro;
  macros: Macro[];
  onChange: (macro: Macro) => void;
  onDelete: () => void;
  onOpenGraph: () => void;
  onSave: () => void;
  saveDisabled: boolean;
  saveTitle: string;
  issue: string;
}) {
  const blocks = shownBlocks(macro);
  const trigger = place(macro);

  return (
    <section className="brief">
      <div className="brief-head">
        <input
          className="name"
          aria-label="Macro name"
          value={macro.name}
          spellCheck={false}
          onChange={(event) => onChange({ ...macro, name: event.target.value })}
        />
        <div className="brief-actions">
          {showBriefRelease(macro) ? (
            <ReleaseField compact label="Macro on release" value={macro.releaseStop} onChange={(releaseStop) => onChange({ ...macro, releaseStop: releaseStop ?? "nextUp" })} />
          ) : null}
          <button type="button" className="ghost is-danger" onClick={onDelete}>
            Delete
          </button>
          <button type="button" className="studio-new is-primary" disabled={saveDisabled} title={saveTitle} onClick={onSave}>
            Save
          </button>
          <button type="button" className="studio-new is-primary" onClick={onOpenGraph}>
            Advanced
          </button>
        </div>
      </div>
      {issue ? <p className="draft-note is-bad">{issue}</p> : null}
      <p className="brief-trigger">
        Trigger
        <KeyFace label={trigger || "Trigger"} empty={!trigger} />
      </p>
      {blocks.length ? (
        <ol className="brief-flow">
          {blocks.map((block) => (
            <li key={block.id} className="brief-card">
              <div className="brief-body">
                <p className="brief-lead">
                  <BlockLead block={block} trigger={trigger} />
                </p>
                <BlockBody
                  block={block}
                  macros={macros}
                  onSteps={(steps) => onChange(withBlockSteps(macro, block.id, steps))}
                />
              </div>
            </li>
          ))}
        </ol>
      ) : (
        <p className="brief-empty">This macro is empty. Use Advanced to hook blocks together.</p>
      )}
    </section>
  );
}

function withBlockSteps(macro: Macro, blockId: string, steps: Step[]): Macro {
  const blocks = macro.blocks ?? [];
  if (blocks.some((block) => block.id === blockId)) {
    return {
      ...macro,
      blocks: blocks.map((block) => (block.id === blockId && "steps" in block ? { ...block, steps } : block)),
    };
  }
  return { ...macro, steps };
}

function BlockBody({ block, macros, onSteps }: { block: Block; macros: Macro[]; onSteps: (steps: Step[]) => void }) {
  if (block.type === "wait") return <p className="brief-meta">{block.ms} ms</p>;
  if (block.type === "tapHold") {
    const key = heldName(block.key) || "Key";
    const tracked = block.watch;
    return (
      <div className="brief-path">
        <p className="brief-meta">Track state of</p>
        {tracked.length ? (
          <div className="lane">
            {tracked.map((item) => (
              <span key={item} className="ev ev-act">
                <b>{heldName(item)}</b>
              </span>
            ))}
          </div>
        ) : (
          <p className="brief-empty">None</p>
        )}
        <p className="brief-meta">
          Release <KeyFace label={key} />
        </p>
        <p className="brief-meta">{block.gapMs} ms</p>
        <p className="brief-meta">
          Resend <KeyFace label={key} />
        </p>
      </div>
    );
  }
  if (block.type === "ifShort") {
    return <SendBody steps={block.steps} macros={macros} onChange={onSteps} after={`${block.minCycles} times`} />;
  }
  if (block.type === "swapAfter") {
    return <SendBody steps={block.steps} macros={macros} onChange={onSteps} after={`until you let go`} />;
  }
  if (block.type === "then") {
    return <SendBody steps={block.steps} macros={macros} onChange={onSteps} after={block.forMs ? `for ${block.forMs} ms` : undefined} />;
  }
  if (block.type === "repeat") {
    return <SendBody steps={block.steps} macros={macros} onChange={onSteps} after={block.count === 0 ? "until it stops" : `${block.count} times`} />;
  }
  if (block.type === "whileHeld" || block.type === "steps") return <SendBody steps={block.steps} macros={macros} onChange={onSteps} />;
  return null;
}

function SendBody({ steps, macros, onChange, after }: { steps: Step[]; macros: Macro[]; onChange: (steps: Step[]) => void; after?: string }) {
  return (
    <>
      <p className="brief-meta">Send</p>
      <StepStrip steps={steps} macros={macros} onChange={onChange} />
      {after ? <p className="brief-meta">{after}</p> : null}
    </>
  );
}

function StepStrip({ steps, macros, onChange }: { steps: Step[]; macros: Macro[]; onChange: (steps: Step[]) => void }) {
  if (!steps.length) return <p className="brief-empty">Empty</p>;
  return <EventLane steps={steps} macros={macros} readOnly tweak empty="" onChange={onChange} />;
}
