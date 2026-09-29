import { blockTitle } from "../blocks";
import type { Block } from "../profile";
import { KeyFace } from "./KeyFace";

export function BlockLead({
  type,
  trigger,
  beforeMs,
}: {
  type: Block["type"] | "trigger";
  trigger: string;
  beforeMs?: number;
}) {
  const face = <KeyFace label={trigger} empty={!trigger} />;
  if (type === "trigger") return trigger ? face : <>Bind a key</>;
  if (type === "whileHeld") return <>While {face} held</>;
  if (type === "ifShort") {
    return beforeMs == null ? <>If {face} released</> : <>If {face} released before {beforeMs} ms</>;
  }
  if (type === "then") return <>After {face} release</>;
  return <>{blockTitle(type)}</>;
}
