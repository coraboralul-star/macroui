import { heldName } from "../macroFlow";
import type { Block } from "../profile";
import { KeyFace } from "./KeyFace";

/** Every block reads as one statement: a keyword, then the condition. */
export function BlockLead({ block, trigger }: { block: Block | "trigger"; trigger: string }) {
  const face = <KeyFace label={trigger} empty={!trigger} />;
  if (block === "trigger") return trigger ? face : <>Bind a key</>;
  if (block.type === "whileHeld") {
    return (
      <span className="block-lead">
        <i>While</i> {face} held
      </span>
    );
  }
  if (block.type === "ifShort") {
    return (
      <span className="block-lead">
        <i>If</i> {face} released before {block.underMs} ms
      </span>
    );
  }
  if (block.type === "swapAfter") {
    return (
      <span className="block-lead">
        <i>If</i> {face} held past {block.afterMs} ms
      </span>
    );
  }
  if (block.type === "then") {
    return (
      <span className="block-lead">
        <i>After</i> {face} release
      </span>
    );
  }
  if (block.type === "repeat") {
    return (
      <span className="block-lead">
        <i>Repeat</i> {block.count === 0 ? "until stopped" : `${block.count} times`}
      </span>
    );
  }
  if (block.type === "wait") {
    return (
      <span className="block-lead">
        <i>Wait</i> {block.ms} ms
      </span>
    );
  }
  if (block.type === "tapHold") {
    return (
      <span className="block-lead">
        <i>Repress</i> <KeyFace label={heldName(block.key) || "Key"} />
      </span>
    );
  }
  return (
    <span className="block-lead">
      <i>Run</i> once
    </span>
  );
}
