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
  /** Profile waiting on the 180 ms debounce. flush() posts it now (used before quit). */
  const pendingPost = useRef<Profile | null>(null);

  // Layout, so this listener exists before the host effect posts hello.
  useLayoutEffect(() => {
    const off = subscribe((message) => {
      if (message.type === "ready" && message.profile) {
        // A throw here would leave this handler half-applied and the page on the
        // built-in profile. Normalization is tolerant now; this is the backstop.
        let next: Profile;
        try {
          next = normalizeProfile(message.profile);
        } catch (error) {
          console.error("[vendetta] profile on disk could not be read", error);
          return;
        }
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
      // The disk version just replaced an edit still inside the debounce.
      pendingPost.current = null;
      return;
    }
    if (!shell) localStorage.setItem(STORAGE_KEY, JSON.stringify(profile));
    pendingPost.current = profile;
    const timer = window.setTimeout(() => {
      pendingPost.current = null;
      post({ type: "profile", profile });
    }, 180);
    return () => window.clearTimeout(timer);
  }, [profile, shell]);

  // Quitting inside the debounce window dropped the last edit. The close button
  // calls this first so the shell writes the file before it handles the close.
  const flush = () => {
    const waiting = pendingPost.current;
    if (!waiting) return;
    pendingPost.current = null;
    post({ type: "profile", profile: waiting });
  };

  // Tray Quit cannot call flush itself. The shell asks, then waits for "flushed".
  useEffect(() => {
    return subscribe((message) => {
      if (message.type !== "flush") return;
      flush();
      post({ type: "flushed" });
    });
  }, []);

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

  return { profile, selected, setSelected, update, saveMacro, setInputMode, flush };
}
