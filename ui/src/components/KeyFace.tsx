export function KeyFace({
  label,
  empty,
}: {
  label: string;
  empty?: boolean;
}) {
  return <span className={`key-face${empty ? " is-empty" : ""}`}>{label}</span>;
}
