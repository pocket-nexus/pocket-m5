import { io, FactKind, utf8 } from "@pocketjs/framework/io";
import type { Inbox, TurnContext } from "@pocketjs/framework/worker";
import { Bmi270 } from "./drivers/bmi270";
import { Rx8130ce } from "./drivers/rx8130ce";
import { COMMAND_BYTES, STATE_BYTES, Command, StateFlag, RtcSetStatus, decodeClockCommand } from "./protocol";

const rtc = new Rx8130ce("i2c0");
const imu = new Bmi270("i2c0");
const serial = io.uart("uart1").open({ baudRate: 115200 });
const state = io.channel("state");
const packet = new Uint8Array(STATE_BYTES);
const fields = new DataView(packet.buffer);
let commandEcho = 0;
const pattern = utf8("PocketJS UART\r\n");
let nextReport = 0, sent = 0, received = 0, gaps = 0, mismatch = 0;
let pending = 0, serialEnded = false;
let rtcSetSequence = 0, rtcSetStatus: number = RtcSetStatus.idle;
let nextState = 0, previousRateTime = 0, previousSamples = 0, sampleRate = 0;
export function turn(context: TurnContext, inbox: Inbox): void {
  let publishNow = false;
  for (const command of inbox.channel("commands")) {
    const data = command.data;
    // The diagnostic realm still sends four-byte echo records.
    if (data.length === 4 || (data.length === COMMAND_BYTES && data.u8(4) === Command.echo)) {
      commandEcho = data.u32(0);
      publishNow = true;
    } else if (data.length === COMMAND_BYTES && data.u8(4) === Command.setRtc) {
      const clock = decodeClockCommand(data);
      if (rtc.setting) continue; // Do not replace the in-flight operation's identity.
      rtcSetSequence = data.u32(0);
      rtcSetStatus = clock && rtcSetSequence > 0 && rtc.requestSetTime(clock)
        ? RtcSetStatus.busy : RtcSetStatus.rejected;
      publishNow = true;
    }
  }
  rtc.turn(context, inbox);
  if (rtcSetStatus === RtcSetStatus.busy && !rtc.setting) {
    rtcSetStatus = rtc.setStatus === "succeeded" ? RtcSetStatus.succeeded : RtcSetStatus.failed;
    publishNow = true;
  }
  imu.turn(context, inbox);
  for (const event of serial.observe(inbox)) {
    if (event.kind === FactKind.gap) ++gaps;
    if (event.kind === FactKind.end) {
      serialEnded = true;
      pending = 0;
      io.timer(4, 0);
    }
    if (event.kind !== FactKind.data) continue;
    for (let i = 0; i < event.data.length; ++i) {
      if (event.data.u8(i) !== pattern[received % pattern.length]) ++mismatch;
      ++received;
    }
  }
  if (context.nowMicros >= nextReport) {
    if (previousRateTime) sampleRate = (imu.samples - previousSamples) * 1_000_000 / (context.nowMicros - previousRateTime);
    previousRateTime = context.nowMicros;
    previousSamples = imu.samples;
    nextReport = context.nowMicros + 1_000_000;
    if (!serialEnded && pending === 0) pending = pattern.length;
    // Keep the periodic console report to counters and calendar integers.
    // Keep floating-point decimal formatting out of the 10 ms worker turn;
    // the binary UI snapshot carries all six sensor measurements.
    console.log(JSON.stringify({ generation: context.generation, turn: context.sequence,
      rtcTransfers: rtc.transfers, rtcInvalid: rtc.invalidSamples, rtcRaw: rtc.raw,
      rtc: rtc.latest?.time, rtcValid: rtc.currentValid, rtcSetStatus: rtc.setStatus, rtcSetError: rtc.setError,
      imuState: imu.state, imuError: imu.error, imuSamples: imu.samples,
      commandEcho, serialEnded, uartSent: sent, uartReceived: received, uartGaps: gaps, uartMismatch: mismatch }));
    io.timer(3, nextReport);
  }
  if (pending > 0) {
    const accepted = serial.write(pattern.subarray(pattern.length - pending));
    if (accepted > 0) { pending -= accepted; sent += accepted; }
    if (pending > 0) io.timer(4, context.nowMicros + 1000);
  }
  if (publishNow || context.nowMicros >= nextState) {
    fields.setUint32(0, commandEcho, true);
    fields.setUint32(4, imu.samples, true);
    fields.setUint32(8, rtc.transfers, true);
    fields.setUint32(12, rtc.invalidSamples, true);
    fields.setUint32(16, (imu.state === "ready" ? StateFlag.imuReady : 0) |
      (imu.state === "fault" ? StateFlag.imuFault : 0) | (rtc.currentValid ? StateFlag.rtcValid : 0) |
      (rtc.setting ? StateFlag.rtcSetting : 0) | (serialEnded ? StateFlag.uartEnded : 0), true);
    for (let axis = 0; axis < 3; ++axis) {
      fields.setFloat32(20 + axis * 4, imu.latest?.accel[axis] ?? 0, true);
      fields.setFloat32(32 + axis * 4, imu.latest?.gyro[axis] ?? 0, true);
    }
    fields.setUint32(44, sent, true); fields.setUint32(48, received, true);
    fields.setUint32(52, gaps, true); fields.setUint32(56, mismatch, true);
    const time = rtc.latest?.time;
    fields.setUint16(60, time?.year ?? 0, true);
    packet[62] = time?.month ?? 0; packet[63] = time?.day ?? 0;
    packet[64] = time?.hours ?? 0; packet[65] = time?.minutes ?? 0; packet[66] = time?.seconds ?? 0;
    fields.setUint32(68, rtcSetSequence, true); fields.setUint32(72, rtcSetStatus, true);
    fields.setUint32(76, imu.latest ? Math.min(0xffff_fffe, Math.max(0, (context.nowMicros - imu.latest.observedAtMicros) / 1000)) : 0xffff_ffff, true);
    fields.setUint32(80, context.generation, true); fields.setUint32(84, context.sequence, true);
    fields.setFloat32(88, sampleRate, true);
    state.publish(packet);
    nextState = context.nowMicros + 50_000;
    io.timer(5, nextState);
  }
}
