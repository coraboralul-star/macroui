import { useEffect, useMemo, useRef, useState } from "react";
import { post, subscribe } from "./bridge";
import { Backdrop } from "./components/Backdrop";
import { GraphEditor } from "./components/GraphEditor";
import { Keyboard } from "./components/Keyboard";
import { KeyMenu } from "./components/KeyMenu";
import { MousePad } from "./components/MousePad";
import { ProfileList } from "./components/MacroList";
import { MacroStudio } from "./components/MacroStudio";
import { Mark } from "./components/Mark";
import { FieldSelect } from "./components/FieldSelect";
import { SettingsPage, type SettingPane } from "./components/SettingsPage";
import { keyLabel } from "./keyboard";
import { pressLabel } from "./recording";
import {
  applyGap,
  basicSteps,
  blankMacro,
  defaultProfile,
  MIN_REPEAT_MS,
  newId,
  normalizeInputMode,
  normalizeProfile,
  renameActive,
  setFocusExe,
  switchConfig,
  writeActive,
  mouseSteps,
  type EngineState,
  type Macro,
  type PlayMode,
  type InputMode,
  type Profile,
  type TriggerKind,
} from "./profile";

type BoardPort = { port: string; chip: string; name: string };

const STORAGE_KEY = "macroui-profile";

type Page = "keyboard" | "settings";
type Board = "remap" | "advanced" | "mouse" | "gamepad";

const BOARDS: { id: Board; label: string; later?: boolean }[] = [
  { id: "remap", label: "Remap" },
  { id: "advanced", label: "Macro Editor" },
  { id: "mouse", label: "Mouse" },
  { id: "gamepad", label: "Gamepad", later: true },
];

export function App() {
  const [profile, setProfile] = useState<Profile>(defaultProfile);
  const [selected, setSelected] = useState(defaultProfile.macros[0].id);
  const [shell, setShell] = useState(false);
  const [pipe, setPipe] = useState(false);
  const [engine, setEngine] = useState<EngineState>({ armed: false, running: [], held: [] });
  const [page, setPage] = useState<Page>("keyboard");
  const [board, setBoard] = useState<Board>("remap");
  const [setting, setSetting] = useState<SettingPane>("general");
  const [picked, setPicked] = useState<string | null>(null);
  const [menu, setMenu] = useState<{ kind: TriggerKind; code: string; x: number; y: number } | null>(null);
  const [graphId, setGraphId] = useState<string | null>(null);
  const [fault, setFault] = useState<string | null>(null);
  const [ports, setPorts] = useState<BoardPort[]>([]);
  const [windows, setWindows] = useState<{ exe: string; title: string }[]>([]);
  const [maximized, setMaximized] = useState(false);
  const booted = useRef(false);
  const echo = useRef(true);

  useEffect(() => {
    const off = subscribe((message) => {
      if (message.type === "link") {
        setShell(Boolean(message.shell));
        setPipe(Boolean(message.pipe));
      }
      if (message.type === "ready" && message.profile) {
        const next = normalizeProfile(message.profile);
        echo.current = false;
        booted.current = true;
        setProfile(next);
        setSelected((current) => (next.macros.some((macro) => macro.id === current) ? current : next.macros[0]?.id ?? ""));
        post({ type: "input", mode: normalizeInputMode(next.inputMode) });
      }
      if (message.type === "state") {
        setEngine({
          armed: Boolean(message.armed),
          running: Array.isArray(message.running) ? message.running as EngineState["running"] : [],
          held: Array.isArray(message.held) ? message.held.filter((key) => typeof key === "string") : [],
          front: typeof message.front === "string" ? message.front : "",
        });
        setPipe(true);
        if (Array.isArray(message.windows)) {
          setWindows(
            message.windows.flatMap((item) => {
              if (!item || typeof item !== "object") return [];
              const row = item as { exe?: unknown; title?: unknown };
              if (typeof row.exe !== "string" || row.exe === "") return [];
              return [{ exe: row.exe, title: typeof row.title === "string" ? row.title : "" }];
            }),
          );
        }
      }
      if (message.type === "error") {
        console.error("[vendetta] shell error", message);
        const detail = typeof message.detail === "string" ? message.detail : "";
        const code = typeof message.code === "string" ? message.code : "error";
        setFault(detail || code);
      }
      if (message.type === "chrome") {
        setMaximized(Boolean(message.maximized));
      }
      if (message.type === "ports" && Array.isArray(message.devices)) {
        setPorts(
          message.devices.flatMap((item) => {
            if (!item || typeof item !== "object") return [];
            const row = item as BoardPort;
            if (typeof row.port !== "string" || typeof row.chip !== "string") return [];
            return [{ port: row.port, chip: row.chip, name: typeof row.name === "string" ? row.name : row.chip }];
          }),
        );
      }
    });
    post({ type: "hello" });
    const timer = window.setTimeout(() => {
      if (booted.current) return;
      const saved = localStorage.getItem(STORAGE_KEY);
      if (saved) {
        try {
          const next = normalizeProfile(JSON.parse(saved));
          echo.current = false;
          setProfile(next);
          setSelected(next.macros[0]?.id ?? "");
          post({ type: "input", mode: normalizeInputMode(next.inputMode) });
        } catch {
          /* keep the built-in profile */
        }
      }
      booted.current = true;
    }, 400);
    return () => {
      off();
      window.clearTimeout(timer);
    };
  }, []);

  useEffect(() => {
    if (!booted.current) return;
    if (!echo.current) {
      echo.current = true;
      return;
    }
    if (!shell) localStorage.setItem(STORAGE_KEY, JSON.stringify(profile));
    const timer = window.setTimeout(() => post({ type: "profile", profile }), 180);
    return () => window.clearTimeout(timer);
  }, [profile, shell]);

  const runningIds = useMemo(() => new Set(engine.running.map((item) => item.id)), [engine.running]);
  const menuMacro = menu
    ? profile.macros.find((item) => item.trigger.kind === menu.kind && item.trigger.button === menu.code) ?? null
    : null;

  const update = (next: Profile) => {
    echo.current = true;
    setProfile(writeActive(next, next.macros));
  };

  const saveMacro = (next: Macro) => {
    const exists = profile.macros.some((item) => item.id === next.id);
    const macros = exists
      ? profile.macros.map((item) => (item.id === next.id ? next : item))
      : [...profile.macros, next];
    update({ ...profile, macros });
    setSelected(next.id);
  };

  const pickTrigger = (kind: TriggerKind, code: string) => {
    setPicked(code);
    const hit = profile.macros.find((item) => item.trigger.kind === kind && item.trigger.button === code);
    if (hit) setSelected(hit.id);
  };

  const closeMenu = () => setMenu(null);

  const bindTrigger = (kind: TriggerKind, code: string, make: (current: Macro | null) => Macro) => {
    const current = profile.macros.find((item) => item.trigger.kind === kind && item.trigger.button === code) ?? null;
    saveMacro(make(current));
  };

  const assignMacro = (kind: TriggerKind, code: string, macroId: string) => {
    const macros = profile.macros.map((item) => {
      if (macroId && item.id === macroId) {
        const trigger = { kind, button: code };
        return {
          ...item,
          trigger,
          steps: item.basic ? (kind === "key" ? basicSteps(code, item.gapMs ?? MIN_REPEAT_MS) : mouseSteps(code, item.gapMs ?? MIN_REPEAT_MS)) : item.steps,
        };
      }
      if (item.trigger.kind === kind && item.trigger.button === code)
        return { ...item, trigger: { ...item.trigger, button: "" } };
      return item;
    });
    update({ ...profile, macros });
    if (macroId) setSelected(macroId);
  };

  const freshTrigger = (kind: TriggerKind, code: string, extra: Partial<Macro> = {}): Macro => ({
    ...blankMacro(),
    name: kind === "key" ? keyLabel(code) : pressLabel(code),
    playMode: "once",
    basic: false,
    gapMs: MIN_REPEAT_MS,
    trigger: { kind, button: code },
    steps: [],
    recording: null,
    ...extra,
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
      const hit = windows.find((row) => row.exe.toLowerCase() === current.toLowerCase());
      add(current, hit?.title ?? "");
    }
    for (const row of windows) add(row.exe, row.title);
    return rows;
  }, [windows, profile.focusExe]);

  const status = pipe ? (engine.armed ? "Running" : "Idle") : shell ? "Offline" : "Local";
  const inputMode = normalizeInputMode(profile.inputMode);
  const inputConnected = inputMode === "software" || ports.some((item) => item.chip === inputMode);

  const setInputMode = (mode: InputMode) => {
    update({ ...profile, inputMode: mode });
    post({ type: "input", mode });
  };
  const openAdvanced = () => {
    setPage("keyboard");
    setBoard("advanced");
    setMenu(null);
  };

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
              <p className="mark">Vendetta</p>
              <input
                className="profile-name"
                aria-label="Profile name"
                value={profile.name}
                spellCheck={false}
                onMouseDown={(event) => event.stopPropagation()}
                onChange={(e) => update(renameActive(profile, e.target.value))}
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
            <p className={`status${pipe && engine.armed ? " is-live" : ""}`}>{status}</p>
          </div>
          {shell ? (
            <div className="chrome">
              <button type="button" aria-label="Minimize" onClick={() => post({ type: "window", action: "minimize" })}>
                –
              </button>
              <button
                type="button"
                aria-label={maximized ? "Windowed" : "Full screen"}
                onClick={() => post({ type: "window", action: "maximize" })}
              >
                {maximized ? "❐" : "□"}
              </button>
              <button type="button" aria-label="Close" className="close" onClick={() => post({ type: "window", action: "close" })}>
                ×
              </button>
            </div>
          ) : null}
        </header>
        {page === "keyboard" ? (
        <div className="body">
          <aside className="rail tex">
            <section className="rail-block">
              <h2>Profiles</h2>
              <ProfileList
                configs={profile.configs}
                selected={profile.activeId}
                live={runningIds.size ? profile.activeId : ""}
                onSelect={(id) => {
                  const next = switchConfig(profile, id);
                  update(next);
                  setSelected(next.macros[0]?.id ?? "");
                  setPicked(null);
                  setMenu(null);
                }}
                onAdd={() => {
                  const config = { id: newId(), name: `Profile ${profile.configs.length + 1}`, macros: [] as Macro[], focusExe: "" };
                  update({
                    ...profile,
                    configs: [...profile.configs, config],
                    activeId: config.id,
                    name: config.name,
                    macros: [],
                    focusExe: "",
                  });
                  setSelected("");
                  setPicked(null);
                  setMenu(null);
                }}
                onDelete={(id) => {
                  if (profile.configs.length < 2) return;
                  const configs = profile.configs.filter((config) => config.id !== id);
                  const active = configs.find((config) => config.id === profile.activeId) ?? configs[0];
                  update({ ...profile, configs, activeId: active.id, name: active.name, macros: active.macros, focusExe: active.focusExe ?? "" });
                  setSelected(active.macros[0]?.id ?? "");
                  setMenu(null);
                }}
              />
              <div className="focus-pick">
                <span>
                  <span className="settings-row-name">Only when focused</span>
                  <span className="settings-row-note">
                    This profile runs only if that window is in front.
                    {engine.front ? ` Front now - ${engine.front.replace(/\.exe$/i, "")}` : ""}
                  </span>
                </span>
                <FieldSelect
                  ariaLabel="Only when focused"
                  prefer="up"
                  value={profile.focusExe ?? ""}
                  options={focusOptions}
                  placeholder="Any window"
                  onChange={(exe) => update(setFocusExe(profile, exe))}
                />
              </div>
            </section>
            <section className="rail-block is-quick">
              <h2>Quick settings</h2>
              <div className="quick">
                <div className="quick-actions">
                  <button type="button" className="primary" onClick={() => post({ type: "command", action: "start" })} disabled={!pipe || engine.armed}>
                    <svg className="quick-icon" viewBox="0 0 16 16" aria-hidden="true">
                      <path fill="currentColor" d="M4.2 2.35v11.3L13.9 8z" />
                    </svg>
                    Start
                  </button>
                  <button type="button" className="stop" onClick={() => post({ type: "command", action: "stop" })} disabled={!pipe || !engine.armed}>
                    <svg className="quick-icon" viewBox="0 0 16 16" aria-hidden="true">
                      <rect x="3.6" y="3.6" width="8.8" height="8.8" rx="1.1" fill="currentColor" />
                    </svg>
                    Stop
                  </button>
                  <button type="button" className="ghost" onClick={() => post({ type: "command", action: "reload" })} disabled={!pipe}>
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
                    pickTrigger(kind, button);
                    setMenu({ kind, code: button, x, y });
                  }}
                  onSwap={(next) => update({ ...profile, swapClicks: next })}
                />
              ) : board === "advanced" ? (
                <MacroStudio
                  macros={profile.macros}
                  selected={selected}
                  running={runningIds}
                  onSelect={(id) => {
                    setSelected(id);
                    const item = profile.macros.find((macro) => macro.id === id);
                    if (item?.trigger.button) setPicked(item.trigger.button);
                  }}
                  onAdd={() => {
                    const created = blankMacro();
                    update({ ...profile, macros: [...profile.macros, created] });
                    setSelected(created.id);
                  }}
                  onChange={(next) => {
                    const prev = profile.macros.find((item) => item.id === next.id);
                    const edited =
                      prev &&
                      (JSON.stringify(prev.steps) !== JSON.stringify(next.steps) ||
                        JSON.stringify(prev.recording) !== JSON.stringify(next.recording));
                    saveMacro(edited ? { ...next, basic: false } : next);
                  }}
                  onDelete={() => {
                    const macros = profile.macros.filter((item) => item.id !== selected);
                    update({ ...profile, macros });
                    setSelected(macros[0]?.id ?? "");
                  }}
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
                  onPick={(code) => pickTrigger("key", code)}
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
            status={status}
            inputMode={inputMode}
            onInputMode={setInputMode}
            connected={inputConnected}
          />
        )}
        {menu ? (
          <KeyMenu
            code={menu.code}
            label={menu.kind === "key" ? keyLabel(menu.code) : pressLabel(menu.code)}
            x={menu.x}
            y={menu.y}
            macro={menuMacro}
            macros={profile.macros}
            onClose={closeMenu}
            onNew={() => {
              const created = menuMacro ?? freshTrigger(menu.kind, menu.code);
              saveMacro(created);
              openAdvanced();
            }}
            onEdit={() => {
              if (!menuMacro) return;
              setSelected(menuMacro.id);
              openAdvanced();
            }}
            onClear={() => {
              if (!menuMacro) return;
              assignMacro(menu.kind, menu.code, "");
            }}
            onAssign={(macroId) => assignMacro(menu.kind, menu.code, macroId)}
            onPlay={(mode: PlayMode) => {
              bindTrigger(menu.kind, menu.code, (current) => {
                if (!current) {
                  const basic = mode !== "once";
                  const steps = basic
                    ? menu.kind === "key"
                      ? basicSteps(menu.code, MIN_REPEAT_MS)
                      : mouseSteps(menu.code, MIN_REPEAT_MS)
                    : [];
                  return freshTrigger(menu.kind, menu.code, {
                    playMode: mode,
                    basic,
                    repeatCount: mode === "repeat" ? 2 : 1,
                    steps,
                  });
                }
                if (!current.steps.length && mode !== "once" && mode !== "onRelease") {
                  return applyGap({ ...current, playMode: mode, basic: true }, current.gapMs ?? MIN_REPEAT_MS);
                }
                return { ...current, playMode: mode };
              });
            }}
            onGap={(ms) => {
              bindTrigger(menu.kind, menu.code, (current) => {
                const base = current ?? freshTrigger(menu.kind, menu.code, { basic: true, playMode: "whileHeld" });
                return applyGap({ ...base, basic: current ? current.basic : true }, ms);
              });
            }}
            onTimes={(count) => {
              bindTrigger(menu.kind, menu.code, (current) => ({
                ...(current ?? freshTrigger(menu.kind, menu.code, { playMode: "repeat" })),
                repeatCount: count,
              }));
            }}
          />
        ) : null}
      </div>
      {graphId ? (
        <GraphEditor
          macros={profile.macros}
          selected={graphId}
          onChange={(next) => {
            const prev = profile.macros.find((item) => item.id === next.id);
            const edited =
              prev &&
              (JSON.stringify(prev.steps) !== JSON.stringify(next.steps) ||
                JSON.stringify(prev.recording) !== JSON.stringify(next.recording) ||
                JSON.stringify(prev.blocks) !== JSON.stringify(next.blocks));
            const bound = edited ? { ...next, basic: false } : next;
            if (bound.trigger.button) {
              const macros = profile.macros.map((item) => {
                if (item.id === bound.id) return bound;
                if (item.trigger.kind === bound.trigger.kind && item.trigger.button === bound.trigger.button)
                  return { ...item, trigger: { ...item.trigger, button: "" } };
                return item;
              });
              const exists = macros.some((item) => item.id === bound.id);
              update({ ...profile, macros: exists ? macros : [...macros, bound] });
              setSelected(bound.id);
              return;
            }
            saveMacro(bound);
          }}
          onClose={() => setGraphId(null)}
          onDelete={() => {
            const macros = profile.macros.filter((item) => item.id !== graphId);
            update({ ...profile, macros });
            setSelected(macros[0]?.id ?? "");
            setGraphId(null);
          }}
        />
      ) : null}
      {fault ? (
        <div className="fault" role="alert">
          <p>{fault}</p>
          <button type="button" aria-label="Dismiss" onClick={() => setFault(null)}>
            ×
          </button>
        </div>
      ) : null}
    </>
  );
}
