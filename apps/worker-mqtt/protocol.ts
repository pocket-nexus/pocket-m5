// The channel records between the UI and the worker, as UTF-8 JSON.
//   command (UI -> worker, at most 512 bytes): Command.
//   status (worker -> UI snapshot, at most 512 bytes): Status.
//   log (worker -> UI ring, at most 256 bytes): one line of text.

export type Version = "3.1.1" | "5";
export interface Broker {
  url: string;
  username: string;
  password: string;
  protocol: Version;
}
export interface Preset { label: string; url: string }

/** The public EMQX broker over each transport; it needs no account. The
 *  first is the default broker. A TLS broker is verified against the
 *  firmware's certificate bundle. */
export const PRESETS: readonly Preset[] = [
  { label: "EMQX", url: "mqtt://broker.emqx.io:1883" },
  { label: "EMQX TLS", url: "mqtts://broker.emqx.io:8883" },
  { label: "EMQX WS", url: "ws://broker.emqx.io:8083/mqtt" },
  { label: "EMQX WSS", url: "wss://broker.emqx.io:8084/mqtt" },
];
export const DEFAULT_BROKER: Broker = { url: PRESETS[0]!.url, username: "", password: "", protocol: "5" };

export type Command =
  /** `checks`: run the checks after the first CONNACK. */
  | { op: "connect"; broker: Broker; checks: boolean }
  | { op: "disconnect" }
  | { op: "publish"; topic: string; payload: string; qos: 0 | 1 | 2; retain: boolean }
  | { op: "checks" };

export type SessionState = "idle" | "connecting" | "connected" | "disconnected" | "ended";
export interface Status {
  state: SessionState;
  url: string;
  protocol: Version;
  /** The topic prefix this session subscribes to (`<base>/#`). */
  base: string;
  sessionPresent: boolean;
  error: string;
  sent: number;
  received: number;
  lost: number;
  checks: { running: boolean; passed: number; failed: number; total: number; current: string };
}
export const IDLE_STATUS: Status = {
  state: "idle", url: "", protocol: "5", base: "", sessionPresent: false, error: "", sent: 0, received: 0, lost: 0,
  checks: { running: false, passed: 0, failed: 0, total: 0, current: "" },
};

/** UTF-8 without TextEncoder and TextDecoder, which QuickJS does not
 *  provide; both realms use these. */
export function encodeText(text: string): Uint8Array {
  const out: number[] = [];
  for (const char of text) {
    let cp = char.codePointAt(0)!;
    if (cp >= 0xd800 && cp <= 0xdfff) cp = 0xfffd;
    if (cp < 0x80) out.push(cp);
    else if (cp < 0x800) out.push(0xc0 | cp >> 6, 0x80 | cp & 63);
    else if (cp < 0x10000) out.push(0xe0 | cp >> 12, 0x80 | cp >> 6 & 63, 0x80 | cp & 63);
    else out.push(0xf0 | cp >> 18, 0x80 | cp >> 12 & 63, 0x80 | cp >> 6 & 63, 0x80 | cp & 63);
  }
  return Uint8Array.from(out);
}
export function decodeText(bytes: Uint8Array): string {
  let text = "";
  for (let i = 0; i < bytes.length;) {
    const lead = bytes[i]!;
    const extra = lead < 0x80 ? 0 : lead >= 0xc2 && lead < 0xe0 ? 1 : lead >= 0xe0 && lead < 0xf0 ? 2 : lead >= 0xf0 && lead < 0xf5 ? 3 : -1;
    if (extra < 0 || i + extra >= bytes.length) { text += "�"; ++i; continue; }
    let cp = extra ? lead & (0x3f >> extra) : lead, valid = true;
    for (let k = 1; k <= extra; ++k) {
      const next = bytes[i + k]!;
      if ((next & 0xc0) !== 0x80) valid = false;
      cp = cp << 6 | next & 0x3f;
    }
    if (!valid || cp < [0, 0x80, 0x800, 0x10000][extra]! || cp > 0x10ffff) { text += "�"; ++i; continue; }
    text += String.fromCodePoint(cp);
    i += extra + 1;
  }
  return text;
}
/** Cuts text to at most `limit` UTF-8 bytes on a character boundary. */
export function clip(text: string, limit: number): Uint8Array {
  const bytes = encodeText(text);
  if (bytes.length <= limit) return bytes;
  let end = limit;
  while (end > 0 && (bytes[end]! & 0xc0) === 0x80) --end;
  return bytes.slice(0, end);
}
export function encodeJson(value: unknown): Uint8Array { return encodeText(JSON.stringify(value)); }
export function decodeJson<T>(bytes: Uint8Array): T | undefined {
  try { return JSON.parse(decodeText(bytes)) as T; } catch { return undefined; }
}
