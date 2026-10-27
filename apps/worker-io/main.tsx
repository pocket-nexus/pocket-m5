import { createMemo, createSignal, For, Match, Switch, type JSX } from "solid-js";
import { mount } from "@pocketjs/framework/solid";
import { View, Text } from "@pocketjs/framework/components";
import { onFrame } from "@pocketjs/framework/lifecycle";
import { io } from "@pocketjs/framework/io";
import { decodeState, encodeCommand, StateFlag, RtcSetStatus, validClock, type State } from "./protocol";
import { accelerationTilt, clockDraft, CLOCK_FIELDS, editField, INITIAL_CLOCK, pad, readDraft, signed, type ClockField } from "./ui-model";

const C = { bg: "#0b151e", panel: "#142630", raised: "#203540", line: "#304650",
  text: "#e7f0f1", muted: "#8ca6af", mint: "#91e3c4", amber: "#f1bd71", red: "#f39188" };
const clamp = (value: number, limit: number) => Math.max(-limit, Math.min(limit, value));
const FIELD_LABELS = { year: "YEAR", month: "MONTH", day: "DAY", hour: "HOUR", minute: "MINUTE", second: "SECOND" };
const FIELD_RANGES = { year: "2000 - 2099", month: "01 - 12", day: "CALENDAR DAY", hour: "00 - 23", minute: "00 - 59", second: "00 - 59" };
const KEYS = ["1", "2", "3", "4", "5", "6", "7", "8", "9", "CLR", "0", "DEL"];

function Pressable(props: { x: number; y: number; w: number; h: number; onPress: () => void;
  selected?: boolean; disabled?: boolean; accent?: boolean; name: string; children: JSX.Element }) {
  return <View focusable={!props.disabled} onPress={() => { if (!props.disabled) props.onPress(); }}
    debugName={props.name} class="absolute flex-col rounded-md items-center justify-center active:opacity-70"
    style={{ insetL: props.x, insetT: props.y, width: props.w, height: props.h,
      bgColor: props.disabled ? C.panel : props.accent ? C.mint : props.selected ? C.raised : C.panel,
      borderWidth: props.selected ? 1 : 0, borderColor: C.mint }}>
    {props.children}
  </View>;
}
function Label(props: { x: number; y: number; text: string; color?: string }) {
  return <Text class="absolute text-xs" style={{ insetL: props.x, insetT: props.y, textColor: props.color ?? C.muted }}>{props.text}</Text>;
}

mount(() => {
  const snapshots = io.channel("state"), commands = io.channel("commands");
  const [tab, setTab] = createSignal(0);
  const [state, setState] = createSignal<State>();
  const [workerStale, setWorkerStale] = createSignal(false);
  const [draft, setDraft] = createSignal(clockDraft(INITIAL_CLOCK));
  const [selected, setSelected] = createSignal<ClockField>("year");
  const [pending, setPending] = createSignal<number>();
  const [notice, setNotice] = createSignal("Enter UTC. No host-time preset.");
  const [noticeTone, setNoticeTone] = createSignal(C.muted);
  let frame = 0, sequence = 0, lastTurnFrame = 0, lastTurn = -1, pendingFrame = 0;
  let previousGeneration: number | undefined, initializedDraft = false, edited = false, replaceInput = true;
  const nextSequence = () => sequence === 0xffff_ffff ? 1 : sequence + 1;
  const parsedClock = createMemo(() => readDraft(draft()));
  const rtcValid = createMemo(() => !!state() && !!(state()!.flags & StateFlag.rtcValid) && validClock(state()!.rtc));
  const imuLive = createMemo(() => !!state() && !workerStale() &&
    !!(state()!.flags & StateFlag.imuReady) && !(state()!.flags & StateFlag.imuFault) && state()!.imuAgeMillis <= 250);
  const tilt = createMemo(() => imuLive() ? accelerationTilt(state()!.accel) : undefined);
  const imuStatus = createMemo(() => !state() ? "WAITING" : state()!.flags & StateFlag.imuFault ? "FAULT" :
    !(state()!.flags & StateFlag.imuReady) ? "STARTING" : imuLive() ? "LIVE" : "STALE");
  const rtcTime = () => rtcValid() ? `${pad(state()!.rtc.hour)}:${pad(state()!.rtc.minute)}:${pad(state()!.rtc.second)}` : "--:--:--";
  const rtcDate = () => rtcValid() ? `${state()!.rtc.year}-${pad(state()!.rtc.month)}-${pad(state()!.rtc.day)}` : "NO VALID RTC";
  const canApply = createMemo(() => !!state() && !workerStale() && parsedClock() !== undefined && pending() === undefined);

  function applyClock() {
    const clock = parsedClock();
    if (!clock || !canApply()) return;
    const id = nextSequence();
    if (!commands.publish(encodeCommand(id, clock))) {
      setNotice("QUEUE BUSY / tap Apply to retry"); setNoticeTone(C.amber); return;
    }
    sequence = id;
    pendingFrame = frame;
    setPending(id); // Lock before the worker acknowledgement arrives.
    setNotice("WRITING / waiting for readback"); setNoticeTone(C.amber);
  }
  function keyPress(key: string) {
    if (pending() !== undefined) return;
    const field = selected();
    setDraft(value => ({ ...value, [field]: editField(value[field], field, key, replaceInput) }));
    replaceInput = false; edited = true;
    setNotice("DRAFT / Apply writes the RTC"); setNoticeTone(C.muted);
  }
  function selectField(field: ClockField) {
    if (pending() !== undefined) return;
    setSelected(field); replaceInput = true;
  }
  function loadClock() {
    if (!rtcValid() || workerStale() || pending() !== undefined) return;
    setDraft(clockDraft(state()!.rtc)); initializedDraft = true; edited = false;
    replaceInput = true;
    setNotice("LOADED / editing hardware UTC"); setNoticeTone(C.muted);
  }
  onFrame(() => {
    ++frame;
    if (frame % 50 === 1) {
      const id = nextSequence();
      if (commands.publish(encodeCommand(id))) sequence = id;
    }
    if (frame % 3 !== 0) return; // 60 Hz host, at most 20 data updates/s.
    const bytes = snapshots.observe()[0];
    const value = bytes ? decodeState(bytes) : undefined;
    if (value) {
      if (value.turn !== lastTurn || value.generation !== previousGeneration) {
        lastTurnFrame = frame; lastTurn = value.turn;
      }
      if (previousGeneration !== undefined && value.generation !== previousGeneration) {
        setPending(undefined); setNotice("WORKER RESTARTED / check RTC"); setNoticeTone(C.amber);
      }
      previousGeneration = value.generation;
      sequence = Math.max(sequence, value.commandEcho, value.rtcSetSequence);
      setState(value);
      if (!initializedDraft && !edited && (value.flags & StateFlag.rtcValid) && validClock(value.rtc)) {
        setDraft(clockDraft(value.rtc)); initializedDraft = true;
      }
      if (pending() !== undefined && value.rtcSetSequence === pending()) {
        if (value.rtcSetStatus === RtcSetStatus.succeeded) {
          setPending(undefined); setNotice("VERIFIED / RTC readback matched"); setNoticeTone(C.mint);
        } else if (value.rtcSetStatus === RtcSetStatus.rejected) {
          setPending(undefined); setNotice("REJECTED / clock was not applied"); setNoticeTone(C.red);
        } else if (value.rtcSetStatus === RtcSetStatus.failed) {
          setPending(undefined); setNotice("ERROR / readback not verified"); setNoticeTone(C.red);
        }
      }
    }
    setWorkerStale(!!state() && frame - lastTurnFrame > 120);
    if (pending() !== undefined && frame - pendingFrame >= 300) {
      setPending(undefined); setNotice("UNKNOWN / timed out; check RTC"); setNoticeTone(C.amber);
    }
  });

  function Motion() {
    return <>
      <Label x={20} y={145} text="01 / MOTION" />
      <Label x={282} y={145} text={imuStatus()} color={imuLive() ? C.mint : C.amber} />
      <View class="absolute rounded-xl" style={{ insetL: 20, insetT: 174, width: 320, height: 190, bgColor: C.panel }}>
        <View class="absolute border" style={{ insetL: 18, insetT: 16, width: 154, height: 154, radius: 77, borderColor: C.line }} />
        <View class="absolute border" style={{ insetL: 44, insetT: 42, width: 102, height: 102, radius: 51, borderColor: C.line }} />
        <View class="absolute" style={{ insetL: 18, insetT: 93, width: 154, height: 1, bgColor: C.line }} />
        <View class="absolute" style={{ insetL: 95, insetT: 16, width: 1, height: 154, bgColor: C.line }} />
        <View class="absolute" style={{ insetL: 87, insetT: 85, width: 16, height: 16, radius: 8,
          bgColor: tilt() ? C.mint : C.muted,
          translateX: tilt() ? clamp(-tilt()!.pitch, 45) * 1.28 : 0,
          translateY: tilt() ? clamp(tilt()!.roll, 45) * 1.28 : 0 }} />
        <Label x={190} y={20} text="ROLL EST." />
        <Text class="absolute text-2xl" style={{ insetL: 188, insetT: 39, textColor: C.text }}>{tilt() ? signed(tilt()!.roll, 1) : "--"}</Text>
        <Label x={190} y={76} text="PITCH EST." />
        <Text class="absolute text-2xl" style={{ insetL: 188, insetT: 95, textColor: C.text }}>{tilt() ? signed(tilt()!.pitch, 1) : "--"}</Text>
        <Label x={190} y={137} text="deg / accel" color={C.mint} />
      </View>
      <Label x={20} y={374} text="ACCELERATION" />
      <Label x={291} y={374} text="m/s2" />
      <For each={["X", "Y", "Z"]}>{(axis, index) => <>
        <Text class="absolute text-sm" style={{ insetL: 20, insetT: 399 + index() * 32, textColor: C.muted }}>{axis}</Text>
        <View class="absolute" style={{ insetL: 52, insetT: 409 + index() * 32, width: 190, height: 4, radius: 2, bgColor: C.line }} />
        <View class="absolute" style={{ insetL: 147, insetT: 405 + index() * 32, width: 1, height: 12, bgColor: C.muted }} />
        <View class="absolute" style={{ insetL: 147 + Math.min(0, imuLive() ? clamp(state()!.accel[index()]! / 19.6133, 1) * 95 : 0), radius: 2,
          insetT: 409 + index() * 32, width: imuLive() ? Math.abs(clamp(state()!.accel[index()]! / 19.6133, 1) * 95) : 0,
          height: 4, bgColor: C.mint }} />
        <Text class="absolute text-sm" style={{ insetL: 261, insetT: 399 + index() * 32, textColor: C.text }}>{signed(imuLive() ? state()!.accel[index()] : undefined)}</Text>
      </>}</For>
      <Label x={20} y={503} text="GYROSCOPE / rad/s" />
      <For each={["X", "Y", "Z"]}>{(axis, index) => <View class="absolute rounded-md" style={{ insetL: 20 + index() * 110, insetT: 526, width: 100, height: 43, bgColor: C.panel }}>
        <Label x={9} y={4} text={axis} />
        <Text class="absolute text-base" style={{ insetL: 28, insetT: 15, textColor: C.text }}>{signed(imuLive() ? state()!.gyro[index()] : undefined)}</Text>
      </View>}</For>
      <Label x={20} y={581} text={`${state() && Number.isFinite(state()!.sampleRate) ? state()!.sampleRate.toFixed(1) : "--"} Hz  /  ${state() && state()!.imuAgeMillis !== 0xffffffff ? state()!.imuAgeMillis : "--"} ms age`} color={C.mint} />
      <Label x={20} y={606} text="Face-up level / accel tilt, not fused." />
    </>;
  }
  function Clock() {
    return <>
      <Label x={20} y={145} text="02 / CLOCK" />
      <Label x={245} y={145} text={rtcValid() ? "VALID / UTC" : "INVALID / UTC"} color={rtcValid() ? C.mint : C.amber} />
      <Pressable x={20} y={170} w={320} h={76} disabled={!rtcValid() || workerStale() || pending() !== undefined}
        name="clock-load" onPress={loadClock}>
        <Text class="absolute text-2xl" style={{ insetL: 14, insetT: 5, textColor: rtcValid() ? C.text : C.amber }}>{rtcTime()}</Text>
        <Label x={15} y={48} text={rtcDate()} />
        <Label x={232} y={16} text="RX8130CE" />
        <Label x={222} y={40} text="TAP TO LOAD" color={C.mint} />
      </Pressable>
      <Text class="absolute text-xs" style={{ insetL: 20, insetT: 255, textColor: noticeTone() }}>{notice()}</Text>
      <For each={CLOCK_FIELDS}>{(field, index) => <Pressable x={20 + index() % 3 * 110} y={278 + Math.floor(index() / 3) * 46}
        w={100} h={40} selected={selected() === field} disabled={pending() !== undefined}
        name={`clock-${field}`} onPress={() => selectField(field)}>
        <Text class="text-xs" style={{ textColor: selected() === field ? C.mint : C.muted }}>{FIELD_LABELS[field]}</Text>
        <Text class="text-base" style={{ textColor: C.text }}>{draft()[field] || "_"}</Text>
      </Pressable>}</For>
      <Label x={20} y={373} text={`${FIELD_LABELS[selected()]} / ${FIELD_RANGES[selected()]}`} />
      <Label x={259} y={373} text={parsedClock() ? "READY" : "CHECK DATE"} color={parsedClock() ? C.mint : C.amber} />
      <For each={KEYS}>{(key, index) => <Pressable x={20 + index() % 3 * 110} y={394 + Math.floor(index() / 3) * 46}
        w={100} h={40} disabled={pending() !== undefined} name={`key-${key}`} onPress={() => keyPress(key)}>
        <Text class={key.length > 1 ? "text-sm" : "text-xl"} style={{ textColor: key.length > 1 ? C.muted : C.text }}>{key}</Text>
      </Pressable>}</For>
      <Pressable x={20} y={584} w={320} h={38} accent disabled={!canApply()} name="clock-apply" onPress={applyClock}>
        <Text class="text-sm font-bold" style={{ textColor: canApply() ? C.bg : C.muted }}>{pending() !== undefined ? "WRITING / WAIT FOR READBACK" : "APPLY UTC TO RTC"}</Text>
      </Pressable>
    </>;
  }
  function Uart() {
    const healthy = () => !!state() && !workerStale() && !(state()!.flags & StateFlag.uartEnded) && state()!.uartReceived > 0 && !state()!.uartMismatch && !state()!.uartGaps;
    const ended = () => !!(state()?.flags && state()!.flags & StateFlag.uartEnded);
    return <>
      <Label x={20} y={145} text="03 / UART LOOPBACK" />
      <Label x={267} y={145} text={ended() ? "STOPPED" : "UART 1"} color={C.mint} />
      <View class="absolute rounded-xl" style={{ insetL: 20, insetT: 174, width: 320, height: 132, bgColor: C.panel }}>
        <Label x={16} y={15} text="TX / BYTES" />
        <Text class="absolute text-2xl" style={{ insetL: 14, insetT: 37, textColor: C.text }}>{state()?.uartSent ?? "--"}</Text>
        <View class="absolute" style={{ insetL: 160, insetT: 18, width: 1, height: 74, bgColor: C.line }} />
        <Label x={178} y={15} text="RX / BYTES" />
        <Text class="absolute text-2xl" style={{ insetL: 176, insetT: 37, textColor: C.mint }}>{state()?.uartReceived ?? "--"}</Text>
        <Label x={16} y={106} text="AUTO TX / ONE PACKET EACH SECOND" />
      </View>
      <For each={["GAPS", "MISMATCH"]}>{(label, index) => <View class="absolute rounded-lg" style={{ insetL: 20 + index() * 165, insetT: 320, width: 155, height: 78, bgColor: C.panel }}>
        <Label x={13} y={12} text={label} />
        <Text class="absolute text-2xl" style={{ insetL: 12, insetT: 33, textColor: (index() ? state()?.uartMismatch : state()?.uartGaps) ? C.amber : C.text }}>{(index() ? state()?.uartMismatch : state()?.uartGaps) ?? "--"}</Text>
      </View>}</For>
      <View class="absolute rounded-lg" style={{ insetL: 20, insetT: 414, width: 320, height: 125, bgColor: C.panel }}>
        <Label x={15} y={13} text="LOOPBACK WIRING" color={C.mint} />
        <Text class="absolute text-xl" style={{ insetL: 14, insetT: 39, textColor: C.text }}>GPIO 53  ---  GPIO 54</Text>
        <Label x={15} y={75} text="TX                  RX" />
        <Label x={15} y={99} text="115200 baud / 8N1 / common GND" />
      </View>
      <Label x={20} y={557} text={ended() ? "STREAM ENDED / restart the worker" : healthy() ? "MATCHED / receiving the test pattern" : "WAITING / check the loopback wire"} color={healthy() ? C.mint : C.amber} />
      <Label x={20} y={587} text="Pattern: PocketJS UART + CR/LF" />
      <Label x={20} y={609} text={`Command echo ${state()?.commandEcho ?? 0} / gen ${state()?.generation ?? "--"}`} />
    </>;
  }
  return <View class="w-full h-full relative overflow-hidden" style={{ bgColor: C.bg }}>
    <Label x={20} y={18} text="POCKET / INSTRUMENT 01" color={C.mint} />
    <Text class="absolute text-2xl font-bold" style={{ insetL: 18, insetT: 35, textColor: C.text }}>Worker IO</Text>
    <View class="absolute" style={{ insetL: 266, insetT: 47, width: 6, height: 6, radius: 3, bgColor: !state() || workerStale() ? C.amber : C.mint }} />
    <Label x={278} y={41} text={!state() ? "WAIT" : workerStale() ? "STALE" : "ONLINE"} />
    <For each={["Motion", "Clock", "UART"]}>{(name, index) => <Pressable x={20 + index() * 110} y={84} w={100} h={44}
      selected={tab() === index()} name={`tab-${name.toLowerCase()}`} onPress={() => setTab(index())}>
      <Text class="text-sm font-bold" style={{ textColor: tab() === index() ? C.mint : C.muted }}>{name}</Text>
    </Pressable>}</For>
    <Switch><Match when={tab() === 0}><Motion /></Match><Match when={tab() === 1}><Clock /></Match><Match when={tab() === 2}><Uart /></Match></Switch>
  </View>;
});
