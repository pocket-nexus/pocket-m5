import { expect, test } from "bun:test";
import { accelerationTilt, clockDraft, editField, INITIAL_CLOCK, readDraft, signed } from "../apps/worker-io/ui-model";
test("RTC editor refuses empty fields and invalid calendar dates", () => {
  const draft = clockDraft(INITIAL_CLOCK);
  expect(readDraft(draft)).toEqual(INITIAL_CLOCK);
  expect(readDraft({ ...draft, second: "" })).toBeUndefined();
  expect(readDraft({ ...draft, year: "2025", month: "02", day: "29" })).toBeUndefined();
  expect(readDraft({ ...draft, year: "2024", month: "02", day: "29" })?.day).toBe(29);
  expect(readDraft({ ...draft, year: "2100" })).toBeUndefined();
  expect(readDraft({ ...draft, hour: "24" })).toBeUndefined();
});
test("touch entry replaces selected content and keeps incomplete edits visible", () => {
  expect(editField("2000", "year", "2", true)).toBe("2");
  expect(editField("202", "year", "6", false)).toBe("2026");
  expect(editField("59", "second", "2", false)).toBe("2");
  expect(editField("01", "month", "CLR", false)).toBe("");
  expect(editField("1", "month", "DEL", false)).toBe("");
});
test("acceleration tilt rejects unavailable inputs", () => {
  expect(accelerationTilt([0, 0, -9.80665])?.roll).toBe(0);
  expect(accelerationTilt([0, 0, -9.80665])?.pitch).toBe(-0);
  expect(accelerationTilt([0, 0, 9.80665])?.roll).toBe(180);
  expect(accelerationTilt([0, 9.80665, 0])?.roll).toBe(90);
  expect(accelerationTilt([0, 0, 0])).toBeUndefined();
  expect(accelerationTilt([NaN, 0, 1])).toBeUndefined();
  expect(signed(-0.0001)).toBe("+0.00");
});
