import { validClock, type ClockFields } from "./protocol";
export const CLOCK_FIELDS = ["year", "month", "day", "hour", "minute", "second"] as const;
export type ClockField = typeof CLOCK_FIELDS[number];
export type ClockDraft = Record<ClockField, string>;
export const INITIAL_CLOCK: ClockFields = { year: 2000, month: 1, day: 1, hour: 0, minute: 0, second: 0 };
export const pad = (value: number, width = 2): string => String(value).padStart(width, "0");
export function clockDraft(clock: ClockFields): ClockDraft {
  return { year: pad(clock.year, 4), month: pad(clock.month), day: pad(clock.day),
    hour: pad(clock.hour), minute: pad(clock.minute), second: pad(clock.second) };
}
export function readDraft(draft: ClockDraft): ClockFields | undefined {
  if (CLOCK_FIELDS.some(field => !/^\d+$/.test(draft[field]))) return undefined;
  const clock = { year: +draft.year, month: +draft.month, day: +draft.day,
    hour: +draft.hour, minute: +draft.minute, second: +draft.second };
  return validClock(clock) ? clock : undefined;
}
export function editField(value: string, field: ClockField, key: string, replace: boolean): string {
  if (key === "CLR") return "";
  if (key === "DEL") return value.slice(0, -1);
  if (!/^[0-9]$/.test(key)) return value;
  return replace || value.length >= (field === "year" ? 4 : 2) ? key : value + key;
}
/** Tab5 face-up is -Z gravity. Linear acceleration affects this estimate. */
export function accelerationTilt(accel: readonly number[]): { roll: number; pitch: number } | undefined {
  if (accel.length !== 3 || !accel.every(Number.isFinite) || Math.hypot(...accel) < 0.5) return undefined;
  const [x, y, z] = accel;
  return { roll: Math.atan2(y!, -z!) * 180 / Math.PI,
    pitch: Math.atan2(-x!, Math.hypot(y!, z!)) * 180 / Math.PI };
}
export function signed(value: number | undefined, digits = 2): string {
  if (value === undefined || !Number.isFinite(value)) return "--";
  const rounded = Math.abs(value) < 0.5 * 10 ** -digits ? 0 : value;
  return `${rounded >= 0 ? "+" : ""}${rounded.toFixed(digits)}`;
}
