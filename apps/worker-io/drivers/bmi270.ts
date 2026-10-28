import { io, ResourceError, ResourceStatus, type I2CDevice, type Timer } from "@pocketjs/framework/io";
import { read, write, bytes, type I2CStep } from "@pocketjs/framework/io/i2c";
import { nowMicros, sleep } from "@pocketjs/framework/worker";
import { bmi270Configuration } from "./bmi270-config";
import { decodeBmi270, type ImuSample } from "./decode";

const SAMPLE_MICROS = 10_000;

export class Bmi270 {
  readonly device: I2CDevice;
  latest: (ImuSample & { observedAtMicros: number; configuration: number }) | undefined;
  state: "initializing" | "ready" | "fault" | "closed" = "initializing";
  error: string | undefined;
  samples = 0;
  private closing = false;
  private ticks: Timer | undefined;
  constructor(node: string) {
    this.device = io.i2c(node).open({ address: 0x68, timeoutMillis: 20 });
    void this.run();
  }
  close(): void {
    if (this.closing) return;
    this.closing = true;
    this.ticks?.close();
  }
  private async run(): Promise<void> {
    await sleep(0);
    try {
      await this.initialize();
      this.state = "ready";
      await this.sample();
    } catch (error) {
      if (!this.closing) { this.state = "fault"; this.error = error instanceof Error ? error.message : String(error); }
    }
    this.device.close();
    if (this.state !== "fault") this.state = "closed";
  }
  /** Retries a full request queue after 1 ms; any other outcome is a fault. */
  private async transfer(script: readonly I2CStep[]): Promise<Uint8Array> {
    for (;;) {
      if (this.closing) throw new Error("BMI270 closed");
      try { return await this.device.run(script); }
      catch (error) {
        if (error instanceof ResourceError && error.request < 0 && error.status === ResourceStatus.busy) { await sleep(1000); continue; }
        throw error;
      }
    }
  }
  /** Bosch BMI270 datasheet §4.2: reset, upload the feature image, confirm
   *  initialization, then configure 100 Hz at ±4 g and ±1000 °/s. */
  private async initialize(): Promise<void> {
    const [chip] = await this.transfer([read(0x00, [bytes(1)])]);
    if (chip !== 0x24) throw new Error("Unexpected BMI270 CHIP_ID");
    await this.transfer([write(0x7e, 0xb6)]);
    await sleep(2000);
    await this.transfer([write(0x7c, 0)]);
    await sleep(1000);
    await this.transfer([write(0x59, 0)]);
    for (let offset = 0; offset < bmi270Configuration.length; offset += 128) {
      await this.transfer([write(0x5b, (offset >> 1) & 15, offset >> 5)]);
      await this.transfer([write(0x5e, ...bmi270Configuration.subarray(offset, offset + 128))]);
    }
    await this.transfer([write(0x59, 1)]);
    await sleep(150_000);
    const [status] = await this.transfer([read(0x21, [bytes(1)])]);
    if ((status! & 15) !== 1) throw new Error("BMI270 configuration did not initialize");
    await this.transfer([write(0x40, 0xa8, 1, 0xa8, 1)]);
    await this.transfer([write(0x7d, 0x0e)]);
    await sleep(80_000);
    const readback = await this.transfer([read(0x40, [bytes(4)])]);
    if (readback.length !== 4 || readback[0] !== 0xa8 || readback[1] !== 1 || readback[2] !== 0xa8 || readback[3] !== 1) {
      throw new Error("BMI270 configuration readback mismatch");
    }
  }
  /** One contiguous read per 10 ms tick; a read without both data-ready
   *  flags yields no sample. */
  private async sample(): Promise<void> {
    this.ticks = io.timer(SAMPLE_MICROS);
    for await (const _ of this.ticks) {
      const sample = decodeBmi270(await this.transfer([read(0x03, [bytes(24)])]));
      if (sample) { this.latest = { ...sample, observedAtMicros: nowMicros()!, configuration: 1 }; ++this.samples; }
    }
  }
}
