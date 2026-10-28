import { io, FactKind, utf8 } from "@pocketjs/framework/io";
import { nowMicros, sleep, workerContext } from "@pocketjs/framework/worker";
import { Bmi270 } from "./drivers/bmi270";
import { Rx8130ce } from "./drivers/rx8130ce";
import { COMMAND_BYTES, STATE_BYTES, Command, StateFlag, RtcSetStatus, decodeClockCommand, reader } from "./protocol";

const rtc = new Rx8130ce("i2c0");
const imu = new Bmi270("i2c0");
const serial = io.uart("uart1").open({ baudRate: 115200 });
const state = io.channel("state");
const packet = new Uint8Array(STATE_BYTES);
const fields = new DataView(packet.buffer);
const pattern = utf8("PocketJS UART\r\n");
let commandEcho = 0;
let sent = 0, received = 0, gaps = 0, mismatch = 0, serialEnded = false;
let rtcSetSequence = 0, rtcSetStatus: number = RtcSetStatus.idle;
let previousRateTime = 0, previousSamples = 0, sampleRate = 0;

function publishState(): void {
  const context = workerContext()!;
  const now = context.nowMicros;
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
  fields.setUint32(76, imu.latest ? Math.min(0xffff_fffe, Math.max(0, (now - imu.latest.observedAtMicros) / 1000)) : 0xffff_ffff, true);
  fields.setUint32(80, context.generation, true); fields.setUint32(84, context.sequence, true);
  fields.setFloat32(88, sampleRate, true);
  state.publish(packet);
}

async function commands(): Promise<void> {
  for await (const data of io.channel("commands").observe()) {
    const bytes = reader(data);
    // The diagnostic realm still sends four-byte echo records.
    if (data.length === 4 || (data.length === COMMAND_BYTES && data[4] === Command.echo)) {
      commandEcho = bytes.u32(0);
      publishState();
    } else if (data.length === COMMAND_BYTES && data[4] === Command.setRtc) {
      const clock = decodeClockCommand(bytes);
      if (rtc.setting) continue; // Do not replace the in-flight operation's identity.
      rtcSetSequence = bytes.u32(0);
      if (clock && rtcSetSequence > 0 && rtc.requestSetTime(clock)) {
        rtcSetStatus = RtcSetStatus.busy;
        void rtc.settled().then(status => {
          rtcSetStatus = status === "succeeded" ? RtcSetStatus.succeeded : RtcSetStatus.failed;
          publishState();
        });
      } else rtcSetStatus = RtcSetStatus.rejected;
      publishState();
    }
  }
}

async function receive(): Promise<void> {
  for await (const fact of serial.observe()) {
    if (fact.kind === FactKind.gap) ++gaps;
    if (fact.kind === FactKind.end) { serialEnded = true; break; }
    if (fact.kind !== FactKind.data) continue;
    for (let i = 0; i < fact.data.length; ++i) {
      if (fact.data[i] !== pattern[received % pattern.length]) ++mismatch;
      ++received;
    }
  }
}

/** One test packet each second; a full TX ring retries after 1 ms. */
async function send(): Promise<void> {
  for await (const _ of io.timer(1_000_000, { firstMicros: 0 })) {
    let pending = pattern.length;
    while (pending > 0 && !serialEnded) {
      const accepted = serial.write(pattern.subarray(pattern.length - pending));
      if (accepted > 0) { pending -= accepted; sent += accepted; }
      else await sleep(1000);
    }
    if (serialEnded) break;
  }
}

/** Counters and calendar integers once a second. Floating-point values travel
 *  in the binary snapshot, so no decimal formatting runs in the worker. */
async function report(): Promise<void> {
  for await (const _ of io.timer(1_000_000, { firstMicros: 0 })) {
    const context = workerContext()!, now = nowMicros()!;
    if (previousRateTime) sampleRate = (imu.samples - previousSamples) * 1_000_000 / (now - previousRateTime);
    previousRateTime = now;
    previousSamples = imu.samples;
    console.log(JSON.stringify({ generation: context.generation, turn: context.sequence,
      rtcTransfers: rtc.transfers, rtcInvalid: rtc.invalidSamples, rtcRaw: rtc.raw,
      rtc: rtc.latest?.time, rtcValid: rtc.currentValid, rtcSetStatus: rtc.setStatus, rtcSetError: rtc.setError,
      imuState: imu.state, imuError: imu.error, imuSamples: imu.samples,
      commandEcho, serialEnded, uartSent: sent, uartReceived: received, uartGaps: gaps, uartMismatch: mismatch }));
  }
}

async function snapshots(): Promise<void> {
  for await (const _ of io.timer(50_000)) publishState();
}

void commands();
void receive();
void send();
void report();
void snapshots();
