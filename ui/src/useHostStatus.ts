import { useEffect, useMemo, useState } from "react";
import { post, subscribe } from "./bridge";
import {
  parseEngineState,
  parseFault,
  parseLink,
  parseMaximized,
  parsePorts,
  parseWindows,
  statusLabel,
  type BoardPort,
  type WindowRow,
} from "./hostMessages";
import type { EngineState } from "./profile";

export function useHostStatus() {
  const [shell, setShell] = useState(false);
  const [pipe, setPipe] = useState(false);
  const [engine, setEngine] = useState<EngineState>({ armed: false, running: [], held: [], activeId: "" });
  const [fault, setFault] = useState<string | null>(null);
  const [ports, setPorts] = useState<BoardPort[]>([]);
  const [windows, setWindows] = useState<WindowRow[]>([]);
  const [maximized, setMaximized] = useState(false);

  useEffect(() => {
    const off = subscribe((message) => {
      const link = parseLink(message);
      if (link) {
        setShell(link.shell);
        setPipe(link.pipe);
      }
      const state = parseEngineState(message);
      if (state) {
        setEngine(state);
        setPipe(true);
        const nextWindows = parseWindows(message);
        if (nextWindows) setWindows(nextWindows);
      }
      const nextFault = parseFault(message);
      if (nextFault !== null) {
        console.error("[vendetta] shell error", message);
        setFault(nextFault);
      }
      const nextMaximized = parseMaximized(message);
      if (nextMaximized !== null) setMaximized(nextMaximized);
      const nextPorts = parsePorts(message);
      if (nextPorts) setPorts(nextPorts);
    });
    post({ type: "hello" });
    return off;
  }, []);

  const runningIds = useMemo(() => new Set(engine.running.map((item) => item.id)), [engine.running]);
  const status = statusLabel(pipe, shell, engine.armed);

  return { shell, pipe, engine, windows, ports, fault, setFault, maximized, runningIds, status };
}
