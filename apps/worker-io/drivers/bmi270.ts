import { io, type I2CDevice } from "@pocketjs/framework/io";
import { read, write, bytes, type I2CStep } from "@pocketjs/framework/io/i2c";
import type { Inbox, TurnContext } from "@pocketjs/framework/worker";
import { bmi270Configuration } from "./bmi270-config";
import { decodeBmi270, type ImuSample } from "./decode";

export class Bmi270 {
  readonly device: I2CDevice;
  latest: (ImuSample & { observedAtMicros: number; configuration: number }) | undefined;
  state: "initializing" | "ready" | "fault" | "closed" = "initializing";
  error: string | undefined;
  samples = 0;
  private stage = 0;
  private offset = 0;
  private uploadAddress = true;
  private pending = -1;
  private due = 0;
  constructor(node: string, private readonly timerId = 2) {
    this.device = io.i2c(node).open({ address: 0x68, timeoutMillis: 20 });
  }
  private fail(message: string): void { this.state = "fault"; this.error = message; io.timer(this.timerId, 0); }
  turn(context: TurnContext, inbox: Inbox): void {
    if (this.state === "fault" || this.state === "closed") return;
    if (this.pending >= 0) {
      const result = inbox.completion(this.pending);
      if (!result) return;
      this.pending = -1;
      if (result.status !== 0) { this.fail(`I2C stage ${this.stage}: ${result.status}`); return; }
      const raw = result.take()!;
      if (this.stage === 0 && (raw.length !== 1 || raw[0] !== 0x24)) {
        this.fail("Unexpected BMI270 CHIP_ID"); return;
      }
      if (this.stage === 6 && (raw.length !== 1 || (raw[0]! & 15) !== 1)) {
        this.fail("BMI270 configuration did not initialize"); return;
      }
      if (this.stage === 9) {
        if (raw.length !== 4 || raw[0] !== 0xa8 || raw[1] !== 1 || raw[2] !== 0xa8 || raw[3] !== 1) {
          this.fail("BMI270 configuration readback mismatch"); return;
        }
        this.state = "ready";
      }
      if (this.stage === 10) {
        const sample = decodeBmi270(raw);
        if (sample) { this.latest = { ...sample, observedAtMicros: context.nowMicros, configuration: 1 }; ++this.samples; }
        this.due = context.nowMicros + 10_000;
      } else if (this.stage === 4 && this.uploadAddress) {
        this.uploadAddress = false;
      } else if (this.stage === 4 && (this.offset += 128) < bmi270Configuration.length) {
        this.uploadAddress = true;
      } else {
        ++this.stage;
        if (this.stage === 2) this.due = context.nowMicros + 2000;
        if (this.stage === 3) this.due = context.nowMicros + 1000;
        if (this.stage === 6) this.due = context.nowMicros + 150_000;
        if (this.stage === 9) this.due = context.nowMicros + 80_000;
      }
      if (this.due > context.nowMicros) io.timer(this.timerId, this.due);
    }
    if (context.nowMicros < this.due) return;
    let script: readonly I2CStep[];
    switch (this.stage) {
      case 0: script = [read(0x00, [bytes(1)])]; break;
      case 1: script = [write(0x7e, 0xb6)]; break;
      case 2: script = [write(0x7c, 0)]; break;
      case 3: script = [write(0x59, 0)]; break;
      case 4: script = this.uploadAddress
        ? [write(0x5b, (this.offset >> 1) & 15, this.offset >> 5)]
        : [write(0x5e, ...bmi270Configuration.subarray(this.offset, this.offset + 128))]; break;
      case 5: script = [write(0x59, 1)]; break;
      case 6: script = [read(0x21, [bytes(1)])]; break;
      case 7: script = [write(0x40, 0xa8, 1, 0xa8, 1)]; break;
      case 8: script = [write(0x7d, 0x0e)]; break;
      case 9: script = [read(0x40, [bytes(4)])]; break;
      default: script = [read(0x03, [bytes(24)])];
    }
    this.pending = this.device.submit(script);
    if (this.pending < 0) io.timer(this.timerId, context.nowMicros + 1000);
  }
  close(): void { this.state = "closed"; io.timer(this.timerId, 0); this.device.close(); }
}
