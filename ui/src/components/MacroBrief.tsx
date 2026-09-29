import { heldName, place, shownBlocks } from "../macroFlow";
import type { Block, Macro, Step } from "../profile";
import { BlockLead } from "./BlockLead";
import { EventLane } from "./EventLane";
import { KeyFace } from "./KeyFace";

export function MacroBrief({
  macro,
  onChange,
  onDelete,
  onOpenGraph,
}: {
  macro: Macro;
  onChange: (macro: Macro) => void;
  onDelete: () => void;
  onOpenGraph: () => void;
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
        <button type="button" className={macro.busy ? "is-on" : "ghost"} onClick={() => onChange({ ...macro, busy: !macro.busy })}>
          Skip if running
        </button>
        <button type="button" className="ghost is-danger" onClick={onDelete}>
          Delete
        </button>
        <button type="button" className="studio-new is-primary" onClick={onOpenGraph}>
          Open advanced
        </button>
      </div>
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
                  {block.type === "tapHold" ? (
                    <>
                      Repress <KeyFace label={heldName(block.key) || "Key"} />
                    </>
                  ) : (
                    <BlockLead
                      type={block.type}
                      trigger={trigger}
                      beforeMs={block.type === "ifShort" ? block.underMs : undefined}
                    />
                  )}
                </p>
                <BlockBody block={block} />
              </div>
            </li>
          ))}
        </ol>
      ) : (
        <p className="brief-empty">This macro is empty. Open advanced to hook blocks together.</p>
      )}
    </section>
  );
}

function BlockBody({ block }: { block: Block }) {
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
                <small>Pressed</small>
              </span>
            ))}
          </div>
        ) : (
          <p className="brief-empty">Empty</p>
        )}
        <p className="brief-meta">{block.armMs} ms</p>
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
    return <SendBody steps={block.steps} after={`${block.minCycles} times`} />;
  }
  if (block.type === "then") {
    return <SendBody steps={block.steps} after={block.forMs ? `for ${block.forMs} ms` : undefined} />;
  }
  if (block.type === "repeat") {
    return <SendBody steps={block.steps} after={block.count === 0 ? "until it stops" : `${block.count} times`} />;
  }
  if (block.type === "whileHeld" || block.type === "steps") return <SendBody steps={block.steps} />;
  return null;
}

function SendBody({ steps, after }: { steps: Step[]; after?: string }) {
  return (
    <>
      <p className="brief-meta">Send</p>
      <StepStrip steps={steps} />
      {after ? <p className="brief-meta">{after}</p> : null}
    </>
  );
}

function StepStrip({ steps }: { steps: Step[] }) {
  if (!steps.length) return <p className="brief-empty">Empty</p>;
  return <EventLane steps={steps} macros={[]} readOnly empty="" onChange={() => {}} />;
}
