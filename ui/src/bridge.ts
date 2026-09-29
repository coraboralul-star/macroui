export type HostMessage = Record<string, unknown>;

type Listener = (message: HostMessage) => void;

const listeners = new Set<Listener>();

function webview() {
  return window.chrome?.webview;
}

export function post(message: HostMessage) {
  const host = webview();
  if (host) {
    host.postMessage(message);
    return;
  }
  window.setTimeout(() => {
    if (message.type === "hello") {
      emit({ type: "link", shell: false, pipe: false });
    }
  }, 0);
}

export function subscribe(listener: Listener) {
  listeners.add(listener);
  const host = webview();
  const onHost = (event: { data: unknown }) => {
    if (event.data && typeof event.data === "object") listener(event.data as HostMessage);
  };
  host?.addEventListener("message", onHost);
  return () => {
    listeners.delete(listener);
    host?.removeEventListener("message", onHost);
  };
}

function emit(message: HostMessage) {
  for (const listener of listeners) listener(message);
}

export function hasShell() {
  return Boolean(webview());
}
