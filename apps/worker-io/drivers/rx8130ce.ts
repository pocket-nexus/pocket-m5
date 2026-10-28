import { io, type I2CDevice } from "@pocketjs/framework/io";
import { read, write, bytes, type I2CStep } from "@pocketjs/framework/io/i2c";
import type { Inbox, TurnContext } from "@pocketjs/framework/worker";
import { decodeRx8130ce, type RtcTime } from "./decode";

export interface RtcSetTime {
  readonly year: number; readonly month: number; readonly day: number;
  readonly hour: number; readonly minute: number; readonly second: number;
}
export type RtcSetStatus = "idle" | "queued" | "reading" | "writing" |
  "verifying" | "recovering" | "succeeded" | "failed";
type Phase = "poll" | "control" | "write" | "verify" | "clear" | "final" | "recover";

const STOP = 0x40, TEST = 0x80, VLF = 0x02;
const POLL_MICROS = 1_000_000, RETRY_MICROS = 1000, SET_TIMEOUT_MICROS = 2_000_000;
// Flags are write-zero-to-clear; writing one preserves concurrent events.
// Bit 6 is reserved/read-zero, and VBFF (bit 0) is read-only.
const CLEAR_ONLY_VLF = 0xbd;
const MONTH_DAYS = [31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31];
const MONTH_START = [0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334];
function bcd(value: number): number { return Math.floor(value / 10) * 16 + value % 10; }
function leap(year: number): boolean { return year % 4 === 0 && (year % 100 !== 0 || year % 400 === 0); }
function monthDays(year: number, month: number): number {
  return MONTH_DAYS[month - 1]! + (month === 2 && leap(year) ? 1 : 0);
}
function daysSince2000(year: number, month: number, day: number): number {
  const years = year - 2000;
  return years * 365 + Math.floor((years + 3) / 4) + MONTH_START[month - 1]! +
    (month > 2 && leap(year) ? 1 : 0) + day - 1;
}
function normalize(value: RtcSetTime): RtcTime | undefined {
  if (!value || ![value.year, value.month, value.day, value.hour, value.minute, value.second].every(Number.isInteger) ||
      value.year < 2000 || value.year > 2099 || value.month < 1 || value.month > 12 ||
      value.day < 1 || value.day > monthDays(value.year, value.month) ||
      value.hour < 0 || value.hour > 23 || value.minute < 0 || value.minute > 59 ||
      value.second < 0 || value.second > 59) return undefined;
  return { year: value.year, month: value.month, day: value.day,
    weekday: (6 + daysSince2000(value.year, value.month, value.day)) % 7,
    hours: value.hour, minutes: value.minute, seconds: value.second };
}
function secondsSince2000(time: RtcTime): number {
  return daysSince2000(time.year, time.month, time.day) * 86400 +
    time.hours * 3600 + time.minutes * 60 + time.seconds;
}

/** Epson ETM50E-10 §§13.2, 14.1, 14.5 and 18.5. Normal polling never writes. */
export class Rx8130ce {
  readonly device: I2CDevice;
  latest: { time: RtcTime; observedAtMicros: number } | undefined;
  currentValid = false;
  invalidSamples = 0;
  transfers = 0;
  raw: readonly number[] | undefined;
  error: number | undefined;
  setStatus: RtcSetStatus = "idle";
  setError: string | undefined;
  lastSetTime: RtcTime | undefined;
  private target: RtcTime | undefined;
  private phase: Phase = "poll";
  private pending = -1;
  private due = 0;
  private deadline = 0;
  private writtenAt = 0;
  private resumeControl = 0;
  private mayBeStopped = false;
  private recoveryAttempts = 0;
  private failure: string | undefined;
  private closing = false;
  private closed = false;
  constructor(node: string, private readonly timerId = 1) {
    this.device = io.i2c(node).open({ address: 0x32, timeoutMillis: 20 });
  }
  get setting(): boolean { return this.target !== undefined; }

  /** Queues one confirmed UI request. Acceptance does not mean the RTC changed. */
  requestSetTime(value: RtcSetTime): boolean {
    if (this.setting || this.closing || this.closed) return false;
    const target = normalize(value);
    if (!target) {
      this.setStatus = "failed";
      this.setError = "Invalid Gregorian date/time (years 2000–2099)";
      return false;
    }
    this.target = target;
    this.setStatus = "queued";
    this.setError = this.failure = undefined;
    this.currentValid = false;
    this.deadline = 0;
    this.recoveryAttempts = 0;
    this.due = 0;
    return true;
  }

  private finishFailure(message: string, now: number): void {
    this.setStatus = "failed";
    this.setError = message;
    this.target = undefined;
    this.currentValid = false;
    this.phase = "poll";
    this.due = now + POLL_MICROS;
    if (this.closing) this.closeResource();
    else io.timer(this.timerId, this.due);
  }
  private fail(message: string, now: number): void {
    this.failure ??= message;
    this.currentValid = false;
    if (this.mayBeStopped) {
      this.phase = "recover";
      this.setStatus = "recovering";
      this.setError = this.failure;
      this.due = now;
    } else this.finishFailure(this.failure, now);
  }
  private sample(data: Uint8Array, now: number): void {
    ++this.transfers;
    this.raw = Array.from(data);
    const time = decodeRx8130ce(data);
    this.currentValid = !!time;
    if (time) this.latest = { time, observedAtMicros: now };
    else ++this.invalidSamples;
  }
  private matches(data: Uint8Array, now: number, allowVlf: boolean): RtcTime | undefined {
    if (!this.target || data.length !== 15 || data[14] !== this.resumeControl) return undefined;
    const checked = data.slice();
    if (allowVlf) checked[13] = checked[13]! & ~VLF;
    const time = decodeRx8130ce(checked);
    if (!time || time.weekday !== (6 + daysSince2000(time.year, time.month, time.day)) % 7) return undefined;
    const elapsedSeconds = Math.floor(Math.max(0, now - this.writtenAt) / 1_000_000) + 1;
    const delta = secondsSince2000(time) - secondsSince2000(this.target);
    return delta >= 0 && delta <= elapsedSeconds ? time : undefined;
  }
  private complete(status: number, data: Uint8Array | undefined, now: number): void {
    if (this.phase === "poll") {
      if (status === 0 && data) { this.error = undefined; this.sample(data, now); }
      else { this.error = status; this.currentValid = false; }
      this.due = this.setting ? now : now + POLL_MICROS;
      if (this.setting) this.currentValid = false;
      return;
    }
    if (this.phase === "recover") {
      if (status === 0 && data?.length === 1 && data[0] === this.resumeControl) {
        this.mayBeStopped = false;
        this.finishFailure(`${this.failure}; clock resumed`, now);
      } else if (this.recoveryAttempts >= 3) {
        this.finishFailure(`${this.failure}; recovery failed, STOP may remain set`, now);
      } else this.due = now + RETRY_MICROS;
      return;
    }
    if (status !== 0 || !data) {
      this.error = status;
      this.fail(`RTC ${this.phase} outcome ${status}; time not confirmed`, now);
      return;
    }
    if (this.failure) { this.fail(this.failure, now); return; }
    if (this.phase === "control") {
      if (data.length !== 1) { this.fail("RTC control read was incomplete", now); return; }
      // HOUR is always 24-hour BCD. TEST must be zero whenever CTRL0 is written.
      this.resumeControl = data[0]! & ~(STOP | TEST);
      this.phase = "write";
      this.setStatus = "writing";
    } else if (this.phase === "write") {
      this.phase = "verify";
      this.setStatus = "verifying";
    } else if (this.phase === "verify") {
      this.raw = Array.from(data);
      if (!this.matches(data, now, true)) { this.fail("RTC time/control readback mismatch", now); return; }
      this.mayBeStopped = false;
      this.phase = "clear";
    } else if (this.phase === "clear") {
      this.phase = "final";
    } else if (this.phase === "final") {
      this.raw = Array.from(data);
      const time = this.matches(data, now, false);
      if (!time) {
        if (data.length === 15 && (data[14]! & (STOP | TEST))) this.mayBeStopped = true;
        this.fail("RTC final readback invalid (time, STOP, TEST or VLF)", now);
        return;
      }
      this.sample(data, now);
      this.lastSetTime = { ...time };
      this.setStatus = "succeeded";
      this.setError = this.error = undefined;
      this.target = undefined;
      this.phase = "poll";
      this.due = now + POLL_MICROS;
      io.timer(this.timerId, this.due);
      return;
    }
    this.due = now;
  }
  private script(): readonly I2CStep[] {
    if (this.phase === "control") return [read(0x1e, [bytes(1)])];
    if (this.phase === "write") {
      const t = this.target!;
      // Three writes plus final STOP use seven of the controller's eight slots.
      // STOP is set and cleared in this one native transaction, not across turns.
      return [write(0x1e, this.resumeControl | STOP),
        write(0x10, bcd(t.seconds), bcd(t.minutes), bcd(t.hours), 1 << t.weekday,
          bcd(t.day), bcd(t.month), bcd(t.year - 2000)),
        write(0x1e, this.resumeControl)];
    }
    if (this.phase === "clear") return [write(0x1d, CLEAR_ONLY_VLF)];
    // One write plus a one-byte read and STOP use all eight command slots.
    if (this.phase === "recover") return [write(0x1e, this.resumeControl), read(0x1e, [bytes(1)])];
    return [read(0x10, [bytes(15)])];
  }
  turn(context: TurnContext, inbox: Inbox): void {
    if (this.closed) return;
    const now = context.nowMicros;
    if (this.setting && !this.deadline) {
      this.deadline = now + SET_TIMEOUT_MICROS;
      io.timer(this.timerId, this.deadline);
    }
    if (this.setting && this.phase !== "recover" && now >= this.deadline && !this.failure) {
      this.failure = "RTC setting timed out; time not confirmed";
      if (this.pending >= 0) this.device.cancel(this.pending);
    }
    if (this.pending >= 0) {
      const result = inbox.completion(this.pending);
      if (!result) return;
      this.pending = -1;
      this.complete(result.status, result.status === 0 ? result.take() : undefined, now);
    }
    if (this.closed) return;
    if (this.setting && this.failure && this.phase !== "recover") this.fail(this.failure, now);
    if (this.setting && this.phase === "poll") {
      this.phase = "control";
      this.setStatus = "reading";
      this.due = now;
    }
    if (now < this.due) { io.timer(this.timerId, this.due); return; }
    if (this.phase === "recover" && this.recoveryAttempts >= 3) {
      this.finishFailure(`${this.failure}; recovery failed, STOP may remain set`, now);
      return;
    }
    if (this.phase === "recover") ++this.recoveryAttempts;
    const request = this.device.submit(this.script());
    if (request < 0) {
      this.due = now + RETRY_MICROS;
      if (this.phase === "poll") this.currentValid = false;
      io.timer(this.timerId, this.due);
      return;
    }
    this.pending = request;
    if (this.phase === "write") { this.mayBeStopped = true; this.writtenAt = now; }
    // A deadline wake can cancel an overdue request; recovery stays bounded by
    // the driver's 20 ms transaction timeout and at most three attempts.
    io.timer(this.timerId, this.setting && this.phase !== "recover" ? this.deadline : 0);
  }
  private closeResource(): void {
    io.timer(this.timerId, 0);
    this.device.close();
    this.closed = true;
    this.currentValid = false;
  }
  /** Keep pumping turns until setting is false when a write needs recovery. */
  close(): void {
    if (this.closed) return;
    this.closing = true;
    if (this.setting) {
      this.failure = "RTC setting cancelled; time not confirmed";
      if (this.mayBeStopped) return;
      this.setStatus = "failed";
      this.setError = this.failure;
      this.target = undefined;
    }
    this.closeResource();
  }
}
