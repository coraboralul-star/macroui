import { useEffect, useMemo, useState } from "react";
import { post } from "./bridge";
import { Backdrop } from "./components/Backdrop";
import { GraphEditor } from "./components/GraphEditor";
import { Keyboard } from "./components/Keyboard";
import { KeyMenu } from "./components/KeyMenu";
import { MousePad } from "./components/MousePad";
import { ProfileList } from "./components/MacroList";
import { MacroStudio } from "./components/MacroStudio";
import { Mark } from "./components/Mark";
import { FieldSelect } from "./components/FieldSelect";
import { SettingsPage, type CloseMode, type SettingPane } from "./components/SettingsPage";
import { keyLabel } from "./keyboard";
import { pressLabel } from "./recording";
import { normalizeInputMode } from "./profile";
import { useHostStatus } from "./useHostStatus";
import { useMacroActions, type TriggerMenu } from "./useMacroActions";
import { useProfile } from "./useProfile";

type Page = "keyboard" | "settings";
type Board = "remap" | "advanced" | "mouse" | "gamepad";

const CLOSE_KEY = "macroui-close";

function readClose(): CloseMode {
  return localStorage.getItem(CLOSE_KEY) === "quit" ? "quit" : "tray";
}

const BOARDS: { id: Board; label: string; later?: boolean }[] = [
  { id: "remap", label: "Remap" },
  { id: "advanced", label: "Macro Editor" },
  { id: "mouse", label: "Mouse" },
  { id: "gamepad", label: "Gamepad", later: true },
];

export function App() {
  const host = useHostStatus();
  const { profile, selected, setSelected, update, saveMacro, setInputMode } = useProfile(host.shell);
  const [page, setPage] = useState<Page>("keyboard");
  const [board, setBoard] = useState<Board>("remap");
  const [setting, setSetting] = useState<SettingPane>("general");
  const [picked, setPicked] = useState<string | null>(null);
  const [menu, setMenu] = useState<TriggerMenu | null>(null);
  const [graphId, setGraphId] = useState<string | null>(null);
  const [closeMode, setCloseMode] = useState<CloseMode>(readClose);

  useEffect(() => {
    localStorage.setItem(CLOSE_KEY, closeMode);
    post({ type: "shell", close: closeMode });
  }, [closeMode]);

  const openAdvanced = () => {
    setPage("keyboard");
    setBoard("advanced");
    setMenu(null);
  };

  const actions = useMacroActions({
    profile,
    selected,
    graphId,
    menu,
    update,
    saveMacro,
    setSelected,
    setPicked,
    setMenu,
    setGraphId,
    openAdvanced,
  });

  const focusOptions = useMemo(() => {
    const rows = [{ value: "", label: "Any window" }];
    const seen = new Set<string>();
    const add = (exe: string, title: string) => {
      const key = exe.toLowerCase();
      if (!exe || seen.has(key)) return;
      seen.add(key);
      const short = exe.replace(/\.exe$/i, "");
      rows.push({ value: exe, label: title && title.length < 48 ? `${title} - ${short}` : short });
    };
    const current = profile.focusExe ?? "";
    if (current) {
      const hit = host.windows.find((row) => row.exe.toLowerCase() === current.toLowerCase());
      add(current, hit?.title ?? "");
    }
    for (const row of host.windows) add(row.exe, row.title);
    return rows;
  }, [host.windows, profile.focusExe]);

  const inputMode = normalizeInputMode(profile.inputMode);
  const inputConnected = inputMode === "software" || host.ports.some((item) => item.chip === inputMode);

  return (
    <>
      <Backdrop />
      <div className="app">
        <header
          className="top tex"
          onMouseDown={(event) => {
            const target = event.target as HTMLElement;
            if (target.closest("button, input, a, select, textarea, .page-tabs, .chrome")) return;
            post({ type: "window", action: "drag" });
          }}
        >
          <div className="brand">
            <Mark />
            <div>
              <p className="mark">H&amp;le</p>
              <input
                className="profile-name"
                aria-label="Profile name"
                value={profile.name}
                spellCheck={false}
                onMouseDown={(event) => event.stopPropagation()}
                onChange={(event) => actions.profiles.rename(event.target.value)}
              />
            </div>
          </div>
          <nav className="page-tabs" aria-label="App">
            <button type="button" className={page === "keyboard" ? "is-on" : ""} onClick={() => setPage("keyboard")}>
              Keyboard
            </button>
            <button type="button" className={page === "settings" ? "is-on" : ""} onClick={() => { setPage("settings"); setMenu(null); }}>
              Settings
            </button>
          </nav>
          <div className="top-end">
            <p className={`status${host.pipe && host.engine.armed ? " is-live" : ""}`}>{host.status}</p>
          </div>
          <div className="chrome">
            <button type="button" aria-label="Minimize" onClick={() => post({ type: "window", action: "minimize" })}>
              <svg viewBox="0 0 12 12" aria-hidden="true"><path d="M1 6.2h10" /></svg>
            </button>
            <button
              type="button"
              aria-label={host.maximized ? "Restore" : "Maximize"}
              onClick={() => post({ type: "window", action: "maximize" })}
            >
              {host.maximized ? (
                <svg viewBox="0 0 12 12" aria-hidden="true"><path d="M3.2 1.6h7.2v7.2M1.6 3.4h7.2v7.2H1.6z" /></svg>
              ) : (
                <svg viewBox="0 0 12 12" aria-hidden="true"><rect x="1.6" y="1.6" width="8.8" height="8.8" /></svg>
              )}
            </button>
            <button type="button" aria-label="Close" className="close" onClick={() => post({ type: "window", action: "close" })}>
              <svg viewBox="0 0 12 12" aria-hidden="true"><path d="M2 2l8 8M10 2L2 10" /></svg>
            </button>
          </div>
        </header>
        {page === "keyboard" ? (
        <div className="body">
          <aside className="rail tex">
            <section className="rail-block">
              <h2>Profiles</h2>
              <ProfileList
                configs={profile.configs}
                selected={profile.activeId}
                live={host.pipe ? host.engine.activeId ?? "" : ""}
                onSelect={actions.profiles.select}
                onAdd={actions.profiles.add}
                onDelete={actions.profiles.remove}
              />
              <div className="focus-pick">
                <span className="settings-row-name">Only when focused</span>
                <FieldSelect
                  ariaLabel="Only when focused"
                  prefer="up"
                  value={profile.focusExe ?? ""}
                  options={focusOptions}
                  placeholder="Any window"
                  onChange={actions.profiles.setFocus}
                />
              </div>
            </section>
            <section className="rail-block is-quick">
              <h2>Quick settings</h2>
              <div className="quick">
                <div className="quick-actions">
                  <button type="button" className="primary" onClick={() => post({ type: "command", action: "start" })} disabled={!host.pipe || host.engine.armed}>
                    <svg className="quick-icon" viewBox="0 0 16 16" aria-hidden="true">
                      <path fill="currentColor" d="M4.2 2.35v11.3L13.9 8z" />
                    </svg>
                    Start
                  </button>
                  <button type="button" className="stop" onClick={() => post({ type: "command", action: "stop" })} disabled={!host.pipe || !host.engine.armed}>
                    <svg className="quick-icon" viewBox="0 0 16 16" aria-hidden="true">
                      <rect x="3.6" y="3.6" width="8.8" height="8.8" rx="1.1" fill="currentColor" />
                    </svg>
                    Stop
                  </button>
                  <button type="button" className="ghost" onClick={() => post({ type: "command", action: "reload" })} disabled={!host.pipe}>
                    <svg className="quick-icon" viewBox="0 0 16 16" aria-hidden="true">
                      <path
                        fill="none"
                        stroke="currentColor"
                        strokeWidth="1.7"
                        strokeLinecap="round"
                        strokeLinejoin="round"
                        d="M13.2 6.1A5.35 5.35 0 1 0 13.75 9.25M13 2.15v4.2H8.8"
                      />
                    </svg>
                    Reload
                  </button>
                </div>
              </div>
            </section>
          </aside>
          <div className="board-wrap">
            <nav className="config-tabs" aria-label="Keyboard configuration">
              {BOARDS.map((item) => (
                <button
                  key={item.id}
                  type="button"
                  className={board === item.id ? "is-on" : ""}
                  disabled={item.later}
                  title={item.later ? "When a controller is connected" : undefined}
                  onClick={() => {
                    if (item.later) return;
                    setBoard(item.id);
                    setMenu(null);
                  }}
                >
                  {item.label}
                </button>
              ))}
            </nav>
            <section className="stage">
              {board === "mouse" ? (
                <MousePad
                  macros={profile.macros}
                  picked={picked}
                  swapped={Boolean(profile.swapClicks)}
                  onAssign={(kind, button, x, y) => {
                    actions.pickTrigger(kind, button);
                    setMenu({ kind, code: button, x, y });
                  }}
                  onSwap={actions.setSwapClicks}
                />
              ) : board === "advanced" ? (
                <MacroStudio
                  macros={profile.macros}
                  selected={selected}
                  running={host.runningIds}
                  onSelect={actions.studio.select}
                  onAdd={actions.studio.add}
                  onChange={actions.studio.change}
                  onDelete={actions.studio.remove}
                  onOpenGraph={(id) => {
                    setSelected(id);
                    setGraphId(id);
                    setMenu(null);
                  }}
                />
              ) : board === "gamepad" ? (
                <div className="empty-board">
                  <h2>Gamepad</h2>
                  <p>When a controller is connected, you can bind macros to it here.</p>
                </div>
              ) : (
                <Keyboard
                  macros={profile.macros}
                  picked={picked}
                  onPick={(code) => actions.pickTrigger("key", code)}
                  onAssign={(code, x, y) => setMenu({ kind: "key", code, x, y })}
                />
              )}
            </section>
          </div>
        </div>
        ) : (
          <SettingsPage
            pane={setting}
            onPane={setSetting}
            status={host.status}
            inputMode={inputMode}
            onInputMode={setInputMode}
            connected={inputConnected}
            closeMode={closeMode}
            onCloseMode={setCloseMode}
          />
        )}
        {menu ? (
          <KeyMenu
            code={menu.code}
            label={menu.kind === "key" ? keyLabel(menu.code) : pressLabel(menu.code)}
            x={menu.x}
            y={menu.y}
            macro={actions.menuMacro}
            macros={profile.macros}
            onClose={() => setMenu(null)}
            onNew={actions.menu.create}
            onEdit={actions.menu.edit}
            onClear={actions.menu.clear}
            onAssign={actions.menu.assign}
            onPlay={actions.menu.play}
            onGap={actions.menu.gap}
            onTimes={actions.menu.times}
          />
        ) : null}
      </div>
      {graphId ? (
        <GraphEditor
          macros={profile.macros}
          selected={graphId}
          onChange={actions.graph.change}
          onClose={() => setGraphId(null)}
          onDelete={actions.graph.remove}
        />
      ) : null}
      {host.fault ? (
        <div className="fault" role="alert">
          <p>{host.fault}</p>
          <button type="button" aria-label="Dismiss" onClick={() => host.setFault(null)}>
            ×
          </button>
        </div>
      ) : null}
    </>
  );
}
