/// <reference types="vite/client" />

interface MacroHost {
  postMessage: (message: unknown) => void;
  addEventListener: (type: "message", listener: (event: { data: unknown }) => void) => void;
  removeEventListener: (type: "message", listener: (event: { data: unknown }) => void) => void;
}

interface Window {
  chrome?: { webview?: MacroHost };
}
