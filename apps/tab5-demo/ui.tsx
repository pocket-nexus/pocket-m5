// Shared pieces of the Tab5 demo's interface: absolutely placed boxes,
// labels, buttons and toggles on the 640x360 auxiliary surface, the modal
// window frame and the toast line.
import { Show, type JSX } from "solid-js";
import { Text, View } from "@pocketjs/framework/components";

export const C = {
  bg: "#0f1418",
  panel: "#1b2228",
  raised: "#27313a",
  line: "#34404a",
  text: "#edf2f4",
  muted: "#93a3ad",
  dim: "#5d6b75",
  blue: "#3b82f6",
  green: "#22c55e",
  amber: "#f59e0b",
  red: "#ef4444",
  rose: "#f43f5e",
  violet: "#8b5cf6",
  teal: "#14b8a6",
  scrim: "#000000b0",
} as const;

/** Every printable ASCII character, so the baked glyphs cover any SSID,
 *  file name or serial text the demo shows. */
export const ASCII = " !\"#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`abcdefghijklmnopqrstuvwxyz{|}~";

export type Size = "2xs" | "xs" | "sm" | "base" | "lg" | "xl" | "2xl";

/** Text classes are looked up verbatim, so each size is a whole literal. */
function textClass(size: Size, bold: boolean): string {
  switch (size) {
    case "2xs": return bold ? "absolute text-2xs font-bold" : "absolute text-2xs";
    case "xs": return bold ? "absolute text-xs font-bold" : "absolute text-xs";
    case "sm": return bold ? "absolute text-sm font-bold" : "absolute text-sm";
    case "lg": return bold ? "absolute text-lg font-bold" : "absolute text-lg";
    case "xl": return bold ? "absolute text-xl font-bold" : "absolute text-xl";
    case "2xl": return bold ? "absolute text-2xl font-bold" : "absolute text-2xl";
    default: return bold ? "absolute text-base font-bold" : "absolute text-base";
  }
}

export function Label(props: { x: number; y: number; text: string; size?: Size; color?: string; bold?: boolean; w?: number; center?: boolean }) {
  return <Text class={textClass(props.size ?? "sm", props.bold ?? false)}
    style={{ insetL: props.x, insetT: props.y, textColor: props.color ?? C.text, width: props.w, textAlign: props.center ? 1 : 0 }}>
    {props.text}
  </Text>;
}

export function Box(props: { x: number; y: number; w: number; h: number; color?: string; radius?: number; border?: string; children?: JSX.Element }) {
  return <View class="absolute" style={{
    insetL: props.x, insetT: props.y, width: props.w, height: props.h, bgColor: props.color ?? C.panel,
    radius: props.radius ?? 8, borderColor: props.border, borderWidth: props.border ? 1 : 0,
  }}>{props.children}</View>;
}

/** A titled panel; children are placed relative to it. */
export function Panel(props: { x: number; y: number; w: number; h: number; title: string; accent?: string; children?: JSX.Element }) {
  return <Box x={props.x} y={props.y} w={props.w} h={props.h}>
    <View class="absolute" style={{ insetL: 0, insetT: 8, width: 3, height: 14, bgColor: props.accent ?? C.blue, radius: 2 }} />
    <Label x={10} y={6} text={props.title} size="xs" color={C.muted} bold />
    {props.children}
  </Box>;
}

export function Button(props: {
  x: number; y: number; w: number; h: number; label: string; onPress: () => void;
  color?: string; textColor?: string; size?: Size; disabled?: boolean;
}) {
  return <View focusable onPress={() => { if (!props.disabled) props.onPress(); }}
    class="absolute flex-col items-center justify-center active:opacity-60"
    style={{ insetL: props.x, insetT: props.y, width: props.w, height: props.h, radius: 6,
      bgColor: props.disabled ? C.panel : props.color ?? C.raised, opacity: props.disabled ? 0.5 : 1 }}>
    <Text class={props.size === "lg" ? "text-lg font-bold" : props.size === "xs" ? "text-xs" : props.size === "base" ? "text-base" : "text-sm"}
      style={{ textColor: props.textColor ?? C.text }}>{props.label}</Text>
  </View>;
}

/** A labelled switch: the knob slides right and the track lights when on. */
export function Toggle(props: { x: number; y: number; w: number; label: string; on: boolean; onPress: () => void; accent?: string }) {
  return <View focusable onPress={props.onPress} class="absolute active:opacity-60"
    style={{ insetL: props.x, insetT: props.y, width: props.w, height: 26 }}>
    <Label x={0} y={5} text={props.label} size="xs" color={C.text} />
    <View class="absolute" style={{ insetL: props.w - 40, insetT: 4, width: 40, height: 18, radius: 9,
      bgColor: props.on ? props.accent ?? C.green : C.line }}>
      <View class="absolute" style={{ insetL: props.on ? 24 : 2, insetT: 2, width: 14, height: 14, radius: 7, bgColor: C.text }} />
    </View>
  </View>;
}

/** A presence dot and its name. */
export function Indicator(props: { x: number; y: number; label: string; on: boolean; accent?: string }) {
  return <>
    <View class="absolute" style={{ insetL: props.x, insetT: props.y + 4, width: 10, height: 10, radius: 5,
      bgColor: props.on ? props.accent ?? C.green : C.line }} />
    <Label x={props.x + 15} y={props.y} text={props.label} size="xs" color={props.on ? C.text : C.dim} />
  </>;
}

export const WINDOW = { x: 36, y: 18, w: 568, h: 326 } as const;

/** The modal frame every tool window shares: a scrim over the dashboard, a
 *  title and a close button. Children are placed relative to the frame. */
export function Window(props: { title: string; accent?: string; onClose: () => void; children?: JSX.Element }) {
  return <>
    <View class="absolute" focusable onPress={() => {}} style={{ insetL: 0, insetT: 0, width: 640, height: 360, bgColor: C.scrim }} />
    <Box x={WINDOW.x} y={WINDOW.y} w={WINDOW.w} h={WINDOW.h} color={C.bg} radius={12} border={props.accent ?? C.line}>
      <Label x={16} y={10} text={props.title} size="base" bold />
      <Button x={WINDOW.w - 76} y={8} w={64} h={26} label="Close" onPress={props.onClose} />
      {props.children}
    </Box>
  </>;
}

/** One line at the top centre for two seconds. */
export function Toast(props: { text: string | undefined; color?: string }) {
  return <Show when={props.text}>
    <Box x={170} y={4} w={300} h={24} color={props.color ?? C.raised} radius={12}>
      <Label x={0} y={4} w={300} text={props.text ?? ""} size="xs" center />
    </Box>
  </Show>;
}

export function pad2(value: number): string {
  return value < 10 ? `0${value}` : `${value}`;
}

export function hex2(value: number): string {
  return value.toString(16).toUpperCase().padStart(2, "0");
}
