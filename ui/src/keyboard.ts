export type KeyCell = { label: string; code: string; w?: number } | { gap: number };
export type GridKey = { label: string; code: string; caption?: string; col?: number; row?: number; w?: number; h?: number };

const fn = (n: number): KeyCell => ({ label: `F${n}`, code: `f${n}` });

export const MAIN: KeyCell[][] = [
  [
    { label: "Esc", code: "escape" },
    { gap: 1 },
    fn(1), fn(2), fn(3), fn(4),
    { gap: 0.5 },
    fn(5), fn(6), fn(7), fn(8),
    { gap: 0.5 },
    fn(9), fn(10), fn(11), fn(12),
  ],
  [
    { label: "`", code: "`" },
    { label: "1", code: "1" }, { label: "2", code: "2" }, { label: "3", code: "3" }, { label: "4", code: "4" },
    { label: "5", code: "5" }, { label: "6", code: "6" }, { label: "7", code: "7" }, { label: "8", code: "8" },
    { label: "9", code: "9" }, { label: "0", code: "0" },
    { label: "-", code: "-" }, { label: "=", code: "=" },
    { label: "Back", code: "backspace", w: 2 },
  ],
  [
    { label: "Tab", code: "tab", w: 1.5 },
    ..."qwertyuiop".split("").map((letter) => ({ label: letter.toUpperCase(), code: letter })),
    { label: "[", code: "[" }, { label: "]", code: "]" },
    { label: "\\", code: "\\", w: 1.5 },
  ],
  [
    { label: "Caps", code: "CapsLock", w: 1.75 },
    ..."asdfghjkl".split("").map((letter) => ({ label: letter.toUpperCase(), code: letter })),
    { label: ";", code: ";" }, { label: "'", code: "'" },
    { label: "Enter", code: "enter", w: 2.25 },
  ],
  [
    { label: "Shift", code: "shift", w: 2.25 },
    ..."zxcvbnm".split("").map((letter) => ({ label: letter.toUpperCase(), code: letter })),
    { label: ",", code: "," }, { label: ".", code: "." }, { label: "/", code: "/" },
    { label: "Shift", code: "shift", w: 2.75 },
  ],
  [
    { label: "Ctrl", code: "ctrl", w: 1.25 },
    { label: "Win", code: "LWin", w: 1.25 },
    { label: "Alt", code: "alt", w: 1.25 },
    { label: "Space", code: "space", w: 7.5 },
    { label: "Alt", code: "alt", w: 1.25 },
    { label: "Win", code: "RWin", w: 1.25 },
    { label: "Ctrl", code: "ctrl", w: 1.25 },
  ],
];

export const NAV: (GridKey | null)[][] = [
  [
    { label: "PrtSc", caption: "Prt\nSc", code: "PrintScreen" },
    { label: "ScrLk", caption: "Scr\nLk", code: "ScrollLock" },
    { label: "Pause", code: "Pause" },
  ],
  [
    { label: "Ins", code: "Insert" },
    { label: "Home", code: "Home" },
    { label: "PgUp", caption: "Pg\nUp", code: "PgUp" },
  ],
  [
    { label: "Del", code: "Delete" },
    { label: "End", code: "End" },
    { label: "PgDn", caption: "Pg\nDn", code: "PgDn" },
  ],
  [null, null, null],
  [null, { label: "↑", code: "up" }, null],
  [
    { label: "←", code: "left" },
    { label: "↓", code: "down" },
    { label: "→", code: "right" },
  ],
];

export const NUM: GridKey[] = [
  { label: "Num", code: "NumLock", col: 1, row: 1 },
  { label: "/", code: "NumpadDiv", col: 2, row: 1 },
  { label: "*", code: "NumpadMult", col: 3, row: 1 },
  { label: "-", code: "NumpadSub", col: 4, row: 1 },
  { label: "7", code: "Numpad7", col: 1, row: 2 },
  { label: "8", code: "Numpad8", col: 2, row: 2 },
  { label: "9", code: "Numpad9", col: 3, row: 2 },
  { label: "+", code: "NumpadAdd", col: 4, row: 2, h: 2 },
  { label: "4", code: "Numpad4", col: 1, row: 3 },
  { label: "5", code: "Numpad5", col: 2, row: 3 },
  { label: "6", code: "Numpad6", col: 3, row: 3 },
  { label: "1", code: "Numpad1", col: 1, row: 4 },
  { label: "2", code: "Numpad2", col: 2, row: 4 },
  { label: "3", code: "Numpad3", col: 3, row: 4 },
  { label: "Enter", code: "NumpadEnter", col: 4, row: 4, h: 2 },
  { label: "0", code: "Numpad0", col: 1, row: 5, w: 2 },
  { label: ".", code: "NumpadDot", col: 3, row: 5 },
];

export const BOARD: KeyCell[][] = MAIN;

function eachKey(visit: (label: string, code: string) => void) {
  for (const row of MAIN) {
    for (const cell of row) {
      if ("code" in cell) visit(cell.label, cell.code);
    }
  }
  for (const row of NAV) {
    for (const cell of row) {
      if (cell) visit(cell.label, cell.code);
    }
  }
  for (const cell of NUM) visit(cell.label, cell.code);
}

export function keyLabel(code: string): string {
  let found = "";
  eachKey((label, name) => {
    if (name === code) found = label;
  });
  return capKey(found || code);
}

function capKey(label: string) {
  return label.length === 1 ? label.toUpperCase() : label;
}

export type KeySpot = { code: string; label: string; x: number; y: number; w: number; h: number };

const ROW_H = 1;
const ROW_GAP = 0.22;

function spotKey(spot: KeySpot) {
  return `${spot.code}:${spot.x}:${spot.y}`;
}

function center(spot: KeySpot) {
  return { x: spot.x + spot.w / 2, y: spot.y + spot.h / 2 };
}

let spotCache: KeySpot[] | null = null;

export function keySpots(): KeySpot[] {
  if (spotCache) return spotCache;
  const spots: KeySpot[] = [];
  let y = 0;
  for (const row of MAIN) {
    let x = 0;
    for (const cell of row) {
      if ("gap" in cell) {
        x += cell.gap;
        continue;
      }
      const w = cell.w ?? 1;
      spots.push({ code: cell.code, label: cell.label, x, y, w, h: ROW_H });
      x += w;
    }
    y += ROW_H + ROW_GAP;
  }
  const navX = 16.4;
  NAV.forEach((row, rowIndex) => {
    row.forEach((cell, col) => {
      if (!cell) return;
      spots.push({
        code: cell.code,
        label: cell.label,
        x: navX + col * 1.16,
        y: rowIndex * (ROW_H + ROW_GAP),
        w: 1,
        h: ROW_H,
      });
    });
  });
  const numX = 20.4;
  for (const cell of NUM) {
    const h = cell.h ?? 1;
    spots.push({
      code: cell.code,
      label: cell.label,
      x: numX + ((cell.col ?? 1) - 1) * 1.16,
      y: ((cell.row ?? 1) - 1) * (ROW_H + ROW_GAP),
      w: (cell.w ?? 1) * 1.16 - 0.16,
      h: h * ROW_H + (h - 1) * ROW_GAP,
    });
  }
  spotCache = spots;
  return spots;
}

function dist(spot: KeySpot, x: number, y: number) {
  const c = center(spot);
  return Math.hypot(c.x - x, c.y - y);
}

export function canonCode(code: string) {
  const lower = code.toLowerCase();
  if (lower === "lshift" || lower === "rshift") return "shift";
  if (lower === "lcontrol" || lower === "rcontrol" || lower === "lctrl" || lower === "rctrl") return "ctrl";
  if (lower === "lalt" || lower === "ralt") return "alt";
  if (lower === "spacebar") return "space";
  return code;
}

export function clusterAround(codes: string[]): { hot: KeySpot[]; near: KeySpot[] } {
  const all = keySpots();
  const hot: KeySpot[] = [];
  for (const code of codes) {
    const name = canonCode(code);
    const matches = all.filter((spot) => spot.code === name);
    if (!matches.length || hot.some((spot) => spot.code === name)) continue;
    if (matches.length === 1 || !hot.length) {
      hot.push(matches[0]);
      continue;
    }
    const anchor = hot.reduce((sum, spot) => {
      const c = center(spot);
      return { x: sum.x + c.x, y: sum.y + c.y };
    }, { x: 0, y: 0 });
    const ax = anchor.x / hot.length;
    const ay = anchor.y / hot.length;
    matches.sort((a, b) => dist(a, ax, ay) - dist(b, ax, ay));
    hot.push(matches[0]);
  }
  const near: KeySpot[] = [];
  const used = new Set(hot.map(spotKey));
  for (const spot of all) {
    const id = spotKey(spot);
    if (used.has(id)) continue;
    if (hot.some((item) => item.code === spot.code)) continue;
    const close = hot.some((item) => {
      const a = center(item);
      const b = center(spot);
      return Math.hypot(a.x - b.x, a.y - b.y) <= 1.55;
    });
    if (close) near.push(spot);
  }
  return { hot, near };
}
