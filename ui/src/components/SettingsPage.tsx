export type SettingPane = "general" | "updates" | "interface";

const PANES: { id: SettingPane; label: string }[] = [
  { id: "general", label: "General" },
  { id: "updates", label: "Updates" },
  { id: "interface", label: "Interface" },
];

export function SettingsPage({
  pane,
  onPane,
  status,
  swapped,
  onSwap,
}: {
  pane: SettingPane;
  onPane: (pane: SettingPane) => void;
  status: string;
  swapped: boolean;
  onSwap: () => void;
}) {
  return (
    <div className="settings">
      <aside className="settings-nav">
        {PANES.map((item) => (
          <button
            key={item.id}
            type="button"
            className={`settings-link${pane === item.id ? " is-on" : ""}`}
            onClick={() => onPane(item.id)}
          >
            {item.label}
          </button>
        ))}
      </aside>
      <section className="settings-panel">
        {pane === "general" ? (
          <>
            <h2>General</h2>
            <p className="settings-lead">Engine status is {status.toLowerCase()}.</p>
            <label className="check">
              <input type="checkbox" checked={swapped} onChange={onSwap} />
              Swap left and right click in playback
            </label>
          </>
        ) : null}
        {pane === "updates" ? (
          <>
            <h2>Updates</h2>
            <p className="settings-lead">This is the local build. There is no updater yet.</p>
          </>
        ) : null}
        {pane === "interface" ? (
          <>
            <h2>Interface</h2>
            <p className="settings-lead">The app uses the current dark layout. More interface options can land here later.</p>
          </>
        ) : null}
      </section>
    </div>
  );
}
