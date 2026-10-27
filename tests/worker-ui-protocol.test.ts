import { expect, test } from "bun:test";
import { COMMAND_BYTES, STATE_BYTES, Command, decodeClockCommand, decodeState, encodeCommand, validClock } from "../apps/worker-io/protocol";

function reader(bytes: Uint8Array) {
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  return { length: bytes.length, u8: (offset: number) => view.getUint8(offset),
    u16le: (offset: number) => view.getUint16(offset, true),
    u32: (offset: number) => view.getUint32(offset, true),
    f32: (offset: number) => view.getFloat32(offset, true) };
}
const leapDay = { year: 2028, month: 2, day: 29, hour: 23, minute: 59, second: 58 };

test("RTC commands retain the user's UTC calendar and sequence", () => {
  const bytes = encodeCommand(0x12345678, leapDay);
  expect(bytes.length).toBe(COMMAND_BYTES);
  expect(Array.from(bytes.slice(0, 5))).toEqual([0x78, 0x56, 0x34, 0x12, Command.setRtc]);
  expect(decodeClockCommand(reader(bytes))).toEqual(leapDay);
  expect(decodeClockCommand(reader(encodeCommand(10)))).toBeUndefined();
});

test("calendar validation rejects invalid dates and malformed wire commands", () => {
  for (const clock of [
    { ...leapDay, year: 2027 }, { ...leapDay, month: 4, day: 31 },
    { ...leapDay, day: 0 }, { ...leapDay, hour: 24 },
    { ...leapDay, minute: 60 }, { ...leapDay, second: -1 },
    { ...leapDay, year: 2100 }, { ...leapDay, year: 1999 },
    { ...leapDay, second: 1.5 }, { ...leapDay, hour: NaN },
  ]) {
    expect(validClock(clock)).toBe(false);
    expect(() => encodeCommand(1, clock)).toThrow();
  }
  const command = encodeCommand(1, leapDay);
  command[13] = 1;
  expect(decodeClockCommand(reader(command))).toBeUndefined();
  expect(decodeClockCommand(reader(command.subarray(0, 4)))).toBeUndefined();
  expect(() => encodeCommand(0, leapDay)).toThrow();
  expect(() => encodeCommand(0x1_0000_0000)).toThrow();
});

test("snapshots preserve diagnostic counters and signed measurements across subarray boundaries", () => {
  const bytes = new Uint8Array(STATE_BYTES + 5).subarray(5);
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  view.setUint32(0, 41, true); view.setUint32(4, 2486, true);
  view.setUint32(8, 34, true); view.setUint32(12, 12, true);
  view.setUint32(16, 3, true);
  view.setFloat32(20, -1.5, true); view.setFloat32(28, -9.8, true);
  view.setFloat32(36, 0.5, true); view.setUint32(48, 0xffff_ffff, true);
  view.setUint16(60, 2028, true); bytes[62] = 2; bytes[63] = 29;
  bytes[64] = 23; bytes[65] = 59; bytes[66] = 58;
  view.setUint32(68, 9, true); view.setUint32(72, 2, true);
  view.setUint32(76, 16, true); view.setUint32(80, 2, true);
  view.setFloat32(88, 78.5, true);
  const decoded = decodeState(reader(bytes))!;
  expect([decoded.commandEcho, decoded.imuSamples, decoded.rtcTransfers, decoded.rtcInvalid]).toEqual([41, 2486, 34, 12]);
  expect(decoded.accel[0]).toBe(-1.5); expect(decoded.accel[2]).toBeCloseTo(-9.8, 4);
  expect(decoded.gyro[1]).toBe(0.5); expect(decoded.uartReceived).toBe(0xffff_ffff);
  expect(decoded.rtc).toEqual(leapDay);
  expect([decoded.rtcSetSequence, decoded.rtcSetStatus, decoded.imuAgeMillis, decoded.generation, decoded.sampleRate]).toEqual([9, 2, 16, 2, 78.5]);
  expect(decodeState(reader(bytes.subarray(0, 16)))).toBeUndefined();
});
