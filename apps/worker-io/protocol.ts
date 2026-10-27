/** Shared UI/worker byte layout. The first 16 bytes retain diagnostic counters. */
export const STATE_BYTES = 96;
export const COMMAND_BYTES = 16;
export const Command = { echo: 0, setRtc: 1 } as const;
export const StateFlag = { imuReady: 1, rtcValid: 2, rtcSetting: 4, imuFault: 8, uartEnded: 16 } as const;
export const RtcSetStatus = { idle: 0, busy: 1, succeeded: 2, failed: 3, rejected: 4 } as const;
export interface ClockFields {
  year: number; month: number; day: number; hour: number; minute: number; second: number;
}
export interface ByteReader {
  readonly length: number;
  u8(offset: number): number;
  u16le(offset: number): number;
  u32(offset: number): number;
  f32(offset: number): number;
}
export interface State {
  commandEcho: number; imuSamples: number; rtcTransfers: number; rtcInvalid: number;
  flags: number; accel: [number, number, number]; gyro: [number, number, number];
  uartSent: number; uartReceived: number; uartGaps: number; uartMismatch: number;
  rtc: ClockFields; rtcSetSequence: number; rtcSetStatus: number;
  imuAgeMillis: number; generation: number; turn: number; sampleRate: number;
}
export function decodeState(bytes: ByteReader): State | undefined {
  if (bytes.length !== STATE_BYTES) return undefined;
  return {
    commandEcho: bytes.u32(0), imuSamples: bytes.u32(4), rtcTransfers: bytes.u32(8), rtcInvalid: bytes.u32(12),
    flags: bytes.u32(16), accel: [bytes.f32(20), bytes.f32(24), bytes.f32(28)],
    gyro: [bytes.f32(32), bytes.f32(36), bytes.f32(40)],
    uartSent: bytes.u32(44), uartReceived: bytes.u32(48), uartGaps: bytes.u32(52), uartMismatch: bytes.u32(56),
    rtc: { year: bytes.u16le(60), month: bytes.u8(62), day: bytes.u8(63), hour: bytes.u8(64), minute: bytes.u8(65), second: bytes.u8(66) },
    rtcSetSequence: bytes.u32(68), rtcSetStatus: bytes.u32(72), imuAgeMillis: bytes.u32(76),
    generation: bytes.u32(80), turn: bytes.u32(84), sampleRate: bytes.f32(88),
  };
}
export function validClock(value: ClockFields): boolean {
  const { year, month, day, hour, minute, second } = value;
  if (![year, month, day, hour, minute, second].every(Number.isInteger) ||
      year < 2000 || year > 2099 || month < 1 || month > 12 || hour < 0 || hour > 23 ||
      minute < 0 || minute > 59 || second < 0 || second > 59) return false;
  const days = [31, year % 4 === 0 ? 29 : 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31];
  return day >= 1 && day <= days[month - 1]!;
}
export function encodeCommand(sequence: number, clock?: ClockFields): Uint8Array {
  if (!Number.isInteger(sequence) || sequence < 1 || sequence > 0xffff_ffff) throw new RangeError("Invalid command sequence");
  if (clock && !validClock(clock)) throw new RangeError("Invalid RTC calendar or time");
  const bytes = new Uint8Array(COMMAND_BYTES), view = new DataView(bytes.buffer);
  view.setUint32(0, sequence, true);
  bytes[4] = clock ? Command.setRtc : Command.echo;
  if (clock) {
    view.setUint16(6, clock.year, true);
    bytes[8] = clock.month; bytes[9] = clock.day; bytes[10] = clock.hour;
    bytes[11] = clock.minute; bytes[12] = clock.second;
  }
  return bytes;
}
export function decodeClockCommand(bytes: ByteReader): ClockFields | undefined {
  if (bytes.length !== COMMAND_BYTES || bytes.u8(4) !== Command.setRtc || bytes.u8(5) || bytes.u8(13) || bytes.u8(14) || bytes.u8(15)) return undefined;
  const clock = { year: bytes.u16le(6), month: bytes.u8(8), day: bytes.u8(9),
    hour: bytes.u8(10), minute: bytes.u8(11), second: bytes.u8(12) };
  return validClock(clock) ? clock : undefined;
}
