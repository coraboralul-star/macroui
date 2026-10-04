import { useEffect, useLayoutEffect, useRef, useState } from "react";
import { post, subscribe } from "./bridge";
import { upsertMacro } from "./profileEdits";
import {
  defaultProfile,
  normalizeInputMode,
  normalizeProfile,
  writeActive,
  type InputMode,
  type Macro,
  type Profile,
} from "./profile";

const STORAGE_KEY = "macroui-profile";

export function useProfile(shell: boolean) {
  const [profile, setProfile] = useState<Profile>(defaultProfile);
  const [selected, setSelected] = useState(defaultProfile.macros[0].id);
  const booted = useRef(false);
  const echo = useRef(true);

  // Layout, so this listener exists before the host effect posts hello.
  useLayoutEffect(() => {
    const off = subscribe((message) => {
      if (message.type === "ready" && message.profile) {
        const next = normalizeProfile(message.profile);
        echo.current = false;
        booted.current = true;
        setProfile(next);
        setSelected((current) => (next.macros.some((macro) => macro.id === current) ? current : next.macros[0]?.id ?? ""));
        post({ type: "input", mode: normalizeInputMode(next.inputMode) });
      }
    });
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

  const update = (next: Profile) => {
    echo.current = true;
    setProfile(writeActive(next, next.macros));
  };

  const saveMacro = (macro: Macro) => {
    update(upsertMacro(profile, macro));
    setSelected(macro.id);
  };

  const setInputMode = (mode: InputMode) => {
    update({ ...profile, inputMode: mode });
    post({ type: "input", mode });
  };

  return { profile, selected, setSelected, update, saveMacro, setInputMode };
}
