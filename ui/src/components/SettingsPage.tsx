import { INPUT_MODES, type InputMode } from "../profile";

export type CloseMode = "tray" | "quit";

export type SettingPane = "general" | "updates" | "interface" | "input";

const PANES: { id: SettingPane; label: string }[] = [
  { id: "general", label: "General" },
  { id: "updates", label: "Updates" },
  { id: "interface", label: "Interface" },
  { id: "input", label: "Input" },
];

const METHOD_NOTE: Record<InputMode, string> = {
  software: "Windows events",
  rp2040: "USB board",
  rp2350: "USB board",
};

export function SettingsPage({
  pane,
  onPane,
  status,
  inputMode,
  onInputMode,
  connected,
  closeMode,
  onCloseMode,
}: {
  pane: SettingPane;
  onPane: (pane: SettingPane) => void;
  status: string;
  inputMode: InputMode;
  onInputMode: (mode: InputMode) => void;
  connected: boolean;
  closeMode: CloseMode;
  onCloseMode: (mode: CloseMode) => void;
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
            <p className="settings-lead">Window and playback options. Engine status is {status.toLowerCase()}.</p>
            <div className="settings-stack">
              <div className="settings-block">
                <div className="settings-row is-static">
                  <span>
                    <span className="settings-row-name">When you close</span>
                    <span className="settings-row-note">Hide keeps macros running. Quit stops them.</span>
                  </span>
                </div>
                <div className="settings-choices is-pair" role="radiogroup" aria-label="When you close">
                  <button
                    type="button"
                    role="radio"
                    aria-checked={closeMode === "tray"}
                    className={`settings-choice${closeMode === "tray" ? " is-on" : ""}`}
                    onClick={() => onCloseMode("tray")}
                  >
                    <span className="settings-row-name">Hide to the tray</span>
                    <span className="settings-row-note">The window hides. Macros keep running. Open it again from the tray icon.</span>
                  </button>
                  <button
                    type="button"
                    role="radio"
                    aria-checked={closeMode === "quit"}
                    className={`settings-choice${closeMode === "quit" ? " is-on" : ""}`}
                    onClick={() => onCloseMode("quit")}
                  >
                    <span className="settings-row-name">Quit</span>
                    <span className="settings-row-note">The window closes and macros stop.</span>
                  </button>
                </div>
              </div>
              <div className="settings-block">
                <div className="settings-row is-static">
                  <span>
                    <span className="settings-row-name">Swap clicks</span>
                    <span className="settings-row-note">This lives on the Mouse tab. Left and right click trade places in playback.</span>
                  </span>
                </div>
              </div>
            </div>
          </>
        ) : null}
        {pane === "updates" ? (
          <>
            <h2>Updates</h2>
            <p className="settings-lead">How this install stays current.</p>
            <div className="settings-stack">
              <div className="settings-block">
                <div className="settings-row is-static">
                  <span>
                    <span className="settings-row-name">Updater</span>
                    <span className="settings-row-note">This is the local build. There is no updater yet.</span>
                  </span>
                </div>
              </div>
            </div>
          </>
        ) : null}
        {pane === "interface" ? (
          <>
            <h2>Interface</h2>
            <p className="settings-lead">How the app looks.</p>
            <div className="settings-stack">
              <div className="settings-block">
                <div className="settings-row is-static">
                  <span>
                    <span className="settings-row-name">Theme</span>
                    <span className="settings-row-note">The app uses the current dark layout. More options can land here later.</span>
                  </span>
                </div>
              </div>
            </div>
          </>
        ) : null}
        {pane === "input" ? (
          <>
            <h2>Input</h2>
            <p className="settings-lead">Choose where macros send keys and mouse. Only one path is used.</p>
            <div className="settings-stack">
              <div className="settings-block">
                <div className="settings-choices" role="radiogroup" aria-label="Input method">
                  {INPUT_MODES.map((item) => (
                    <button
                      key={item.value}
                      type="button"
                      role="radio"
                      aria-checked={item.value === inputMode}
                      className={`settings-choice${item.value === inputMode ? " is-on" : ""}`}
                      onClick={() => onInputMode(item.value)}
                    >
                      <span className="settings-row-name">{item.label}</span>
                      <span className="settings-row-note">{METHOD_NOTE[item.value]}</span>
                    </button>
                  ))}
                </div>
                <p className={`settings-status${connected ? " is-on" : ""}`}>
                  <i aria-hidden="true" />
                  {connected ? "Connected" : "Not connected"}
                </p>
              </div>
              <details className="settings-block settings-fold">
                <summary>How to set this up</summary>
                <ol>
                  <li>Use a USB data cable, not charge-only.</li>
                  <li>Hold the BOOTSEL button on the board.</li>
                  <li>Plug the board in while holding BOOTSEL.</li>
                  <li>Copy vendetta-rp2040.uf2 or vendetta-rp2350.uf2 from firmware/out onto the drive that appears.</li>
                  <li>Unplug the board, then plug it back in.</li>
                  <li>Choose that chip above.</li>
                  <li>Click into Notepad and run a macro.</li>
                </ol>
              </details>
            </div>
          </>
        ) : null}
      </section>
    </div>
  );
}
