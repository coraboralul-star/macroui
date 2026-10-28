/** Windows virtual-key codes for the names Capture picks. Mouse buttons use
 *  VK_MBUTTON, VK_XBUTTON1 and VK_XBUTTON2, so the shell watches one number.
 *  0 is not a usable input: left and right click, and names this does not know. */
const NAMED: Record<string, number> = {
  space: 0x20, LShift: 0xa0, RShift: 0xa1, LCtrl: 0xa2, RCtrl: 0xa3, LAlt: 0xa4, RAlt: 0xa5,
  tab: 0x09, enter: 0x0d, backspace: 0x08, escape: 0x1b,
  up: 0x26, down: 0x28, left: 0x25, right: 0x27,
  "-": 0xbd, "=": 0xbb, "[": 0xdb, "]": 0xdd, "\\": 0xdc, ";": 0xba, "'": 0xde, ",": 0xbc, ".": 0xbe, "/": 0xbf, "`": 0xc0,
  NumpadDot: 0x6e, NumpadDiv: 0x6f, NumpadMult: 0x6a, NumpadSub: 0x6d, NumpadAdd: 0x6b, NumpadEnter: 0x0d,
  NumLock: 0x90, Insert: 0x2d, Delete: 0x2e, Home: 0x24, End: 0x23, PgUp: 0x21, PgDn: 0x22,
  PrintScreen: 0x2c, ScrollLock: 0x91, Pause: 0x13, CapsLock: 0x14, LWin: 0x5b, RWin: 0x5c,
  MButton: 0x04, XButton1: 0x05, XButton2: 0x06,
};

export function vkOf(name: string): number {
  if (/^[a-z]$/.test(name)) return 0x41 + name.charCodeAt(0) - 97;
  if (/^[0-9]$/.test(name)) return 0x30 + Number(name);
  const f = /^f([1-9]|1[0-9]|2[0-4])$/.exec(name);
  if (f) return 0x6f + Number(f[1]);
  const pad = /^Numpad([0-9])$/.exec(name);
  if (pad) return 0x60 + Number(pad[1]);
  return NAMED[name] ?? 0;
}
