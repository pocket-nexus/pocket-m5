// The tool windows the dashboard opens: RTC setting, camera, microphones,
// music, card listing, I2C scan, header pins, RS485 monitor, Wi-Fi and the
// power actions. Each window owns what it starts and stops it on close.
import { createSignal, For, Index, onCleanup, Show } from "solid-js";
import { Image, View } from "@pocketjs/framework/components";
import { openAudioInput, type AudioInput, type AudioInputSource } from "@pocketjs/framework/audio";
import { closeCamera, openCamera, pollQrCode } from "@pocketjs/framework/camera";
import {
  listDirectory, powerOff, releasePin, setBrightness, setPin, setSwitch, type DeviceDirectory,
} from "@pocketjs/framework/device";
import { onFrame } from "@pocketjs/framework/lifecycle";
import { registerTexture } from "@pocketjs/framework/renderer";
import {
  connectSavedNetwork, joinNetwork, scanNetworks, startAccessPoint, stopAccessPoint, wifiCanServe,
} from "@pocketjs/framework/wifi";
import {
  Bus, Command, RtcSetting, SEND_PAYLOAD, ScanState, SerialKind, Wake, ascii, asciiBytes, encodeClock,
  type ClockFields,
} from "./protocol";
import { click, down, startMusic, startPlayer, up, type Player } from "./sound";
import { device, notify, onSerial, refreshDevice, scan, send, sensors, wifi } from "./store";
import { Box, Button, C, Indicator, Label, Toggle, Window, hex2, pad2 } from "./ui";

// ---- RTC setting ----------------------------------------------------------------

const FIELDS: { key: keyof ClockFields; label: string; min: number; max: number }[] = [
  { key: "year", label: "Year", min: 2000, max: 2099 },
  { key: "month", label: "Month", min: 1, max: 12 },
  { key: "day", label: "Day", min: 1, max: 31 },
  { key: "hour", label: "Hour", min: 0, max: 23 },
  { key: "minute", label: "Minute", min: 0, max: 59 },
  { key: "second", label: "Second", min: 0, max: 59 },
];

export function RtcWindow(props: { onClose: () => void }) {
  const now = sensors();
  const [draft, setDraft] = createSignal<ClockFields>(now.year > 0
    ? { year: now.year, month: now.month, day: now.day, hour: now.hour, minute: now.minute, second: now.second }
    : { year: 2026, month: 1, day: 1, hour: 0, minute: 0, second: 0 });
  const [request, setRequest] = createSignal(0);
  const step = (key: keyof ClockFields, delta: number, min: number, max: number) => {
    const value = draft()[key] + delta;
    setDraft({ ...draft(), [key]: value > max ? min : value < min ? max : value });
    delta > 0 ? up() : down();
  };
  /* The SNTP clock, when the radio has set it: the UI realm's Date. */
  const network = () => {
    const date = new Date();
    return date.getFullYear() >= 2025 ? date : undefined;
  };
  const useNetwork = () => {
    const date = network();
    if (!date) { notify("The network clock is not set"); return; }
    setDraft({ year: date.getFullYear(), month: date.getMonth() + 1, day: date.getDate(),
      hour: date.getHours(), minute: date.getMinutes(), second: date.getSeconds() });
    click();
  };
  const apply = () => {
    const sequence = send(Command.setRtc, (view, bytes) => encodeClock(view, bytes, draft()));
    setRequest(sequence);
    click();
  };
  const status = () => {
    const s = sensors();
    if (!request()) return "";
    if (s.rtcSequence !== request() || s.rtcSetting === RtcSetting.busy) return "Writing and reading back...";
    return s.rtcSetting === RtcSetting.succeeded ? "Set and verified" : "Not confirmed; see the serial log";
  };
  return <Window title="RTC setting" accent={C.teal} onClose={props.onClose}>
    <Label x={16} y={40} text={`The RX8130CE reads ${pad2(sensors().hour)}:${pad2(sensors().minute)}:${pad2(sensors().second)}. Times are UTC.`} size="xs" color={C.muted} />
    <For each={FIELDS}>{(field, index) => {
      const x = 16 + index() * 90;
      return <>
        <Label x={x} y={66} w={80} text={field.label} size="xs" color={C.muted} center />
        <Button x={x} y={86} w={80} h={36} label="+" size="lg" onPress={() => step(field.key, 1, field.min, field.max)} />
        <Box x={x} y={128} w={80} h={48} color={C.panel}>
          <Label x={0} y={10} w={80} text={field.key === "year" ? `${draft().year}` : pad2(draft()[field.key])} size="2xl" bold center />
        </Box>
        <Button x={x} y={182} w={80} h={36} label="-" size="lg" onPress={() => step(field.key, -1, field.min, field.max)} />
      </>;
    }}</For>
    <Button x={16} y={236} w={200} h={40} label="Use the network clock" onPress={useNetwork} disabled={!network()} />
    <Button x={376} y={236} w={176} h={40} label="Apply to RTC" color={C.teal} size="base" onPress={apply} />
    <Label x={16} y={290} text={status()} size="sm" color={sensors().rtcSetting === RtcSetting.failed ? C.red : C.text} />
  </Window>;
}

// ---- camera ------------------------------------------------------------------------

export function CameraWindow(props: { onClose: () => void }) {
  const [preview, setPreview] = createSignal("");
  const [code, setCode] = createSignal("");
  const handle = openCamera();
  if (handle >= 0) {
    const key = `tab5-demo.camera.${handle}`;
    registerTexture(key, handle);
    setPreview(key);
  }
  onCleanup(closeCamera);
  onFrame(() => {
    const text = pollQrCode();
    if (text) { setCode(text); click(); }
  });
  return <Window title="Camera  SC202CS" accent={C.blue} onClose={props.onClose}>
    <Show when={preview()} fallback={<Label x={16} y={140} text="The camera did not start." size="base" color={C.muted} />}>
      <View class="absolute overflow-hidden" style={{ insetL: 16, insetT: 44, width: 320, height: 240, radius: 8 }}>
        <Image class="absolute left-0 top-0" src={preview()} style={{ width: 512, height: 256 }} />
      </View>
    </Show>
    <Label x={352} y={48} text="320 x 240 preview, 15 fps" size="xs" color={C.muted} />
    <Label x={352} y={76} text="QR code" size="sm" bold />
    <Box x={352} y={100} w={200} h={120} color={C.panel}>
      <Label x={10} y={10} w={180} text={code() || "Hold a QR code in front of the camera."} size="xs" color={code() ? C.text : C.muted} />
    </Box>
  </Window>;
}

// ---- microphones -------------------------------------------------------------------

/* Peaks drawn every other frame, 32 bars a channel: each bar is a node the
 * renderer redraws. */
const BARS = 32;
const RATE = 22050;
const RECORD_SECONDS = 3;

/** Peaks of the last frames, newest at the right, one bar per frame. */
function Bars(props: { x: number; y: number; w: number; h: number; peaks: number[]; color: string }) {
  const width = props.w / BARS;
  return <Box x={props.x} y={props.y} w={props.w} h={props.h} color={C.panel}>
    <Index each={props.peaks}>{(peak, index) => {
      const height = () => Math.max(2, Math.round(peak() * (props.h - 8)));
      return <View class="absolute" style={{ insetL: index * width + 1, insetT: (props.h - height()) / 2,
        width: Math.max(1, width - 2), height: height(), radius: 1, bgColor: props.color }} />;
    }}</Index>
  </Box>;
}

export function MicWindow(props: { source: AudioInputSource; onClose: () => void }) {
  const stereo = props.source === "microphones";
  const channels = stereo ? 2 : 1;
  const [left, setLeft] = createSignal<number[]>(new Array(BARS).fill(0));
  const [right, setRight] = createSignal<number[]>(new Array(BARS).fill(0));
  const [phase, setPhase] = createSignal<"live" | "recording" | "playing">("live");
  const input: AudioInput | null = openAudioInput(props.source, RATE, channels);
  const scratch = new Int16Array(4096 * channels);
  const recording = new Int16Array(RATE * RECORD_SECONDS * channels);
  let recorded = 0;
  let player: Player | undefined;
  onCleanup(() => { input?.close(); player?.stop(); });
  let frame = 0, peakL = 0, peakR = 0;
  onFrame(() => {
    player?.pump();
    if (!input) return;
    for (let frames = input.read(scratch); frames > 0; frames = input.read(scratch)) {
      for (let i = 0; i < frames; ++i) {
        const l = Math.abs(scratch[i * channels]!);
        const r = Math.abs(scratch[i * channels + channels - 1]!);
        if (l > peakL) peakL = l;
        if (r > peakR) peakR = r;
      }
      if (phase() === "recording") {
        const take = Math.min(frames, recording.length / channels - recorded);
        recording.set(scratch.subarray(0, take * channels), recorded * channels);
        recorded += take;
        if (recorded * channels >= recording.length) {
          setPhase("playing");
          player = startPlayer(RATE, channels, recorded, (into, offset, count) =>
            into.set(recording.subarray(offset * channels, (offset + count) * channels)), () => setPhase("live"));
          if (!player) setPhase("live");
        }
      }
    }
    if (++frame % 2) return;
    setLeft([...left().slice(1), Math.min(1, peakL / 8192)]);
    if (stereo) setRight([...right().slice(1), Math.min(1, peakR / 8192)]);
    peakL = peakR = 0;
  });
  const record = () => {
    if (phase() !== "live" || !input) return;
    recorded = 0;
    setPhase("recording");
    click();
  };
  const title = stereo ? "Microphones  ES7210" : "Headset microphone";
  return <Window title={title} accent={C.teal} onClose={props.onClose}>
    <Show when={input} fallback={<Label x={16} y={140} text="This board did not open the input." size="base" color={C.muted} />}>
      <Label x={16} y={42} text={stereo ? "Left microphone" : "Headset microphone (plug a headset with a microphone)"} size="xs" color={C.muted} />
      <Bars x={16} y={60} w={536} h={92} peaks={left()} color="#40ffa1" />
      <Show when={stereo}>
        <Label x={16} y={158} text="Right microphone" size="xs" color={C.muted} />
        <Bars x={16} y={176} w={536} h={92} peaks={right()} color="#40c4ff" />
      </Show>
      <Button x={16} y={278} w={160} h={36}
        label={phase() === "recording" ? "Recording..." : phase() === "playing" ? "Playing back..." : `Record ${RECORD_SECONDS} s`}
        color={phase() === "recording" ? "#e17822" : phase() === "playing" ? "#16a34a" : C.raised} onPress={record} />
      <Label x={192} y={288} text={`${RATE} Hz, ${stereo ? "stereo" : "mono"}; the recording plays back on the speaker`} size="xs" color={C.muted} />
    </Show>
  </Window>;
}

// ---- music ---------------------------------------------------------------------------

export function MusicWindow(props: { onClose: () => void }) {
  const [peaks, setPeaks] = createSignal<number[]>(new Array(BARS).fill(0));
  const [playing, setPlaying] = createSignal(false);
  const loopback = openAudioInput("loopback", RATE, 1);
  const scratch = new Int16Array(4096);
  let player: Player | undefined;
  onCleanup(() => { player?.stop(); loopback?.close(); });
  let frame = 0, peak = 0;
  onFrame(() => {
    player?.pump();
    if (!loopback) return;
    for (let frames = loopback.read(scratch); frames > 0; frames = loopback.read(scratch))
      for (let i = 0; i < frames; ++i) peak = Math.max(peak, Math.abs(scratch[i]!));
    if (++frame % 2) return;
    setPeaks([...peaks().slice(1), Math.min(1, peak / 8192)]);
    peak = 0;
  });
  const toggle = () => {
    if (player?.playing()) { player.stop(); player = undefined; setPlaying(false); return; }
    player = startMusic(() => setPlaying(false));
    setPlaying(!!player);
    if (!player) notify("The speaker did not open");
  };
  return <Window title="Music" accent={C.rose} onClose={props.onClose}>
    <Label x={16} y={42} text="Speaker output, captured by the ES7210's loopback channel" size="xs" color={C.muted} />
    <Bars x={16} y={60} w={536} h={150} peaks={peaks()} color="#40ffa1" />
    <Button x={16} y={226} w={160} h={44} label={playing() ? "Stop" : "Play music"} size="base"
      color={playing() ? "#16a34a" : C.rose} onPress={toggle} />
    <Label x={192} y={232} text="An original eight-bar tune synthesized on the device," size="xs" color={C.muted} />
    <Label x={192} y={248} text="streamed through audio.pcm at 22050 Hz." size="xs" color={C.muted} />
    <Label x={16} y={290} text={`Speaker level ${device()?.volume ?? 0}%  (set it on the dashboard)`} size="xs" color={C.muted} />
  </Window>;
}

// ---- card ------------------------------------------------------------------------------

function size(bytes: number): string {
  if (bytes >= 1 << 30) return `${(bytes / (1 << 30)).toFixed(1)} GB`;
  if (bytes >= 1 << 20) return `${(bytes / (1 << 20)).toFixed(1)} MB`;
  if (bytes >= 1 << 10) return `${(bytes / (1 << 10)).toFixed(1)} KB`;
  return `${bytes} B`;
}

export function CardWindow(props: { onClose: () => void }) {
  const [path, setPath] = createSignal("/");
  const [listing, setListing] = createSignal<DeviceDirectory | null>(listDirectory("/"));
  const [first, setFirst] = createSignal(0);
  const go = (next: string) => {
    setPath(next);
    setListing(listDirectory(next));
    setFirst(0);
    click();
  };
  const parent = () => path().replace(/\/[^/]*$/, "") || "/";
  const card = () => device()?.card;
  const rows = () => (listing()?.entries ?? []).slice(first(), first() + 6);
  return <Window title="SD card" accent={C.amber} onClose={props.onClose}>
    <Show when={card()?.mounted} fallback={<Label x={16} y={140} text="No card is mounted. Insert a microSD card and restart." size="base" color={C.muted} />}>
      <Label x={16} y={42} text={`${size(card()!.freeBytes)} free of ${size(card()!.totalBytes)}`} size="xs" color={C.muted} />
      <Label x={200} y={42} w={340} text={path()} size="xs" bold />
      <Show when={listing()} fallback={<Label x={16} y={100} text={device()?.error || "Not a directory"} size="sm" color={C.muted} />}>
        <For each={rows()}>{(entry, index) =>
          <View focusable onPress={() => entry.directory && go(`${path() === "/" ? "" : path()}/${entry.name}`)}
            class="absolute active:opacity-60" style={{ insetL: 16, insetT: 64 + index() * 36, width: 536, height: 32, radius: 6, bgColor: C.panel }}>
            <Label x={10} y={7} text={entry.directory ? "[DIR]" : "     "} size="xs" color={C.amber} />
            <Label x={60} y={6} w={360} text={entry.name} size="sm" />
            <Label x={430} y={7} w={96} text={entry.directory ? "" : size(entry.size)} size="xs" color={C.muted} />
          </View>}
        </For>
        <Show when={(listing()?.entries.length ?? 0) === 0}><Label x={16} y={100} text="This directory is empty." size="sm" color={C.muted} /></Show>
      </Show>
      <Button x={16} y={284} w={96} h={30} label="Up" disabled={path() === "/"} onPress={() => go(parent())} />
      <Button x={340} y={284} w={100} h={30} label="Previous" disabled={first() === 0} onPress={() => { setFirst(Math.max(0, first() - 6)); click(); }} />
      <Button x={452} y={284} w={100} h={30} label="Next" disabled={first() + 6 >= (listing()?.entries.length ?? 0)}
        onPress={() => { setFirst(first() + 6); click(); }} />
      <Label x={124} y={291} text={listing()?.truncated ? "first 64 entries" : `${listing()?.entries.length ?? 0} entries`} size="xs" color={C.muted} />
    </Show>
  </Window>;
}

// ---- I2C scan ----------------------------------------------------------------------------

const KNOWN: Record<number, string> = {
  0x10: "ES8388", 0x14: "GT911", 0x32: "RX8130", 0x36: "SC202CS", 0x40: "ES7210", 0x41: "INA226",
  0x43: "PI4IOE", 0x44: "PI4IOE", 0x55: "ST7123", 0x5d: "GT911", 0x68: "BMI270",
};

export function ScanWindow(props: { onClose: () => void }) {
  const [bus, setBus] = createSignal<number>(Bus.sys);
  const run = (next: number) => {
    setBus(next);
    send(Command.scan, (_, bytes) => { bytes[8] = next; });
    click();
  };
  run(Bus.sys);
  const result = () => scan().bus === bus() ? scan() : undefined;
  const found = (address: number) => result()?.addresses.includes(address) ?? false;
  const ext5v = () => device()?.switches.find(item => item.id === "ext-5v")?.on ?? false;
  return <Window title="I2C scan" accent={C.blue} onClose={props.onClose}>
    <Button x={16} y={42} w={120} h={30} label="System bus" color={bus() === Bus.sys ? C.blue : C.raised} onPress={() => run(Bus.sys)} />
    <Button x={142} y={42} w={120} h={30} label="Port A" color={bus() === Bus.porta ? C.blue : C.raised} onPress={() => run(Bus.porta)} />
    <Toggle x={290} y={44} w={140} label="EXT 5V output" on={ext5v()} accent={C.red}
      onPress={() => { setSwitch("ext-5v", !ext5v()); refreshDevice(); click(); }} />
    <Label x={444} y={50} text={result()?.state === ScanState.scanning ? "Scanning..." : `${result()?.addresses.length ?? 0} found`} size="xs" color={C.muted} />
    <For each={[0, 1, 2, 3, 4, 5, 6, 7]}>{row =>
      <>
        <Label x={16} y={86 + row * 26} text={`${row}0`} size="xs" color={C.dim} />
        <For each={[0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15]}>{column => {
          const address = row * 16 + column;
          return <Box x={40 + column * 32} y={82 + row * 26} w={30} h={24} radius={4}
            color={found(address) ? C.green : address < 8 || address > 0x77 ? C.bg : C.panel}>
            <Label x={0} y={5} w={30} text={hex2(address)} size="2xs" center color={found(address) ? C.bg : C.dim} />
          </Box>;
        }}</For>
      </>}
    </For>
    <Label x={16} y={296} w={536} size="xs" color={C.muted}
      text={bus() === Bus.sys
        ? (result()?.addresses.map(a => KNOWN[a] ? `${hex2(a)} ${KNOWN[a]}` : hex2(a)).join("  ") ?? "")
        : "Port A: G53 SDA, G54 SCL, 100 kHz. A device needs the EXT 5V output on."} />
  </Window>;
}

// ---- header pins ---------------------------------------------------------------------------

export function GpioWindow(props: { onClose: () => void }) {
  const pins = device()?.pins ?? [];
  const [levels, setLevels] = createSignal<Record<number, boolean>>({});
  onCleanup(() => { for (const pin of pins) releasePin(pin); });
  const toggle = (pin: number) => {
    const next = !levels()[pin];
    if (!setPin(pin, next)) { notify(device()?.error || `G${pin} did not change`); return; }
    setLevels({ ...levels(), [pin]: next });
    next ? up() : down();
  };
  return <Window title="Header pins  Ext.Port1 and M-Bus" accent={C.green} onClose={props.onClose}>
    <Label x={16} y={40} text="Each pin drives push-pull at the level shown. Closing the window releases them." size="xs" color={C.muted} />
    <For each={pins}>{(pin, index) => {
      const x = 16 + (index() % 4) * 136, y = 62 + Math.floor(index() / 4) * 42;
      return <View focusable onPress={() => toggle(pin)} class="absolute active:opacity-60"
        style={{ insetL: x, insetT: y, width: 128, height: 36, radius: 6, bgColor: levels()[pin] ? "#166534" : C.panel }}>
        <Label x={10} y={8} text={`G${pin}`} size="sm" bold />
        <Label x={66} y={9} text={levels()[pin] ? "HIGH" : "LOW"} size="xs" color={levels()[pin] ? "#86efac" : C.muted} />
      </View>;
    }}</For>
  </Window>;
}

// ---- RS485 ------------------------------------------------------------------------------------

export function SerialWindow(props: { onClose: () => void }) {
  const [lines, setLines] = createSignal<string[]>([]);
  const [state, setState] = createSignal("Opening...");
  const [counts, setCounts] = createSignal({ sent: 0, received: 0 });
  let partial = "";
  const push = (line: string) => setLines([...lines(), line].slice(-9));
  onSerial((kind, data) => {
    if (kind === SerialKind.opened) setState("Open");
    else if (kind === SerialKind.closed) setState("Closed");
    else if (kind === SerialKind.failed) setState("The port did not accept that");
    else if (kind === SerialKind.sent) {
      push(`<<< ${ascii(data).trim()}`);
      setCounts({ ...counts(), sent: counts().sent + data.length });
    } else if (kind === SerialKind.received) {
      setCounts({ ...counts(), received: counts().received + data.length });
      partial += ascii(data);
      const parts = partial.split(/\r?\n/);
      partial = parts.pop() ?? "";
      for (const part of parts) push(`>>> ${part}`);
      if (partial.length > 60) { push(`>>> ${partial}`); partial = ""; }
    }
  });
  send(Command.serialOpen);
  onCleanup(() => { onSerial(undefined); send(Command.serialClose); });
  const hello = () => {
    const bytes = asciiBytes("Hello M5Stack!\r\n").subarray(0, SEND_PAYLOAD);
    send(Command.serialSend, (_, record) => { record[8] = bytes.length; record.set(bytes, 9); });
    click();
  };
  return <Window title="RS485 monitor" accent={C.amber} onClose={props.onClose}>
    <Label x={16} y={42} text={`UART1  TX G20  RX G21  DE G34  115200 8N1   ${state()}`} size="xs" color={C.muted} />
    <Box x={16} y={62} w={536} h={204} color={C.panel}>
      <For each={lines()}>{(line, index) =>
        <Label x={10} y={8 + index() * 21} w={516} text={line} size="xs" color={line.startsWith("<<<") ? C.amber : C.text} />}
      </For>
      <Show when={lines().length === 0}><Label x={10} y={8} text="Waiting for bytes on the bus." size="xs" color={C.dim} /></Show>
    </Box>
    <Button x={16} y={278} w={220} h={36} label={'Send "Hello M5Stack!"'} color={C.amber} onPress={hello} />
    <Label x={252} y={288} text={`sent ${counts().sent} B, received ${counts().received} B`} size="xs" color={C.muted} />
  </Window>;
}

// ---- Wi-Fi ---------------------------------------------------------------------------------------

const AP_SSID = "M5Tab5-PocketJS";

export function WifiWindow(props: { onClose: () => void }) {
  const status = wifi;
  const ap = () => status()?.accessPoint;
  const toggleAp = () => {
    const ok = ap()?.on ? stopAccessPoint() : startAccessPoint(AP_SSID);
    if (!ok) notify(status()?.error || "The radio refused");
    click();
  };
  return <Window title="Wi-Fi  ESP32-C6" accent={C.violet} onClose={props.onClose}>
    <Label x={16} y={42} text="Station" size="sm" bold />
    <Label x={16} y={64} w={260} size="xs" color={C.muted}
      text={status() ? `${status()!.state}${status()!.ssid ? `  ${status()!.ssid}` : ""}${status()!.address ? `  ${status()!.address}` : ""}` : "No radio"} />
    <Show when={status()?.error}><Label x={16} y={80} w={260} text={status()?.error ?? ""} size="xs" color={C.amber} /></Show>
    <Button x={16} y={100} w={120} h={30} label={status()?.scanning ? "Scanning..." : "Scan"} onPress={() => { scanNetworks(); click(); }} />
    <For each={(status()?.networks ?? []).slice(0, 5)}>{(network, index) =>
      <View focusable class="absolute active:opacity-60" onPress={() => {
        const saved = status()?.saved.find(slot => slot.ssid === network.ssid);
        const ok = saved ? connectSavedNetwork(saved.slot) : !network.secure && joinNetwork(network.ssid, "");
        notify(ok ? `Joining ${network.ssid}` : network.secure ? "Join secured networks in Pocket Home settings" : "The radio refused");
      }} style={{ insetL: 16, insetT: 138 + index() * 34, width: 260, height: 30, radius: 6, bgColor: C.panel }}>
        <Label x={8} y={7} w={180} text={network.ssid} size="xs" />
        <Label x={196} y={7} text={`${"|".repeat(network.strength)}${network.secure ? " lock" : ""}`} size="xs" color={C.muted} />
      </View>}
    </For>
    <Label x={300} y={42} text="Access point" size="sm" bold />
    <Show when={wifiCanServe()} fallback={<Label x={300} y={64} text="This radio does not serve one." size="xs" color={C.muted} />}>
      <Box x={300} y={64} w={252} h={160} color={C.panel}>
        <Indicator x={12} y={12} label={ap()?.on ? "Serving" : "Off"} on={!!ap()?.on} accent={C.violet} />
        <Label x={12} y={40} text="Connect to:" size="xs" color={C.muted} />
        <Label x={12} y={58} text={AP_SSID} size="base" bold />
        <Label x={12} y={88} text="and open:" size="xs" color={C.muted} />
        <Label x={12} y={106} text={ap()?.on ? `http://${ap()!.address}` : "http://192.168.4.1"} size="base" bold />
        <Label x={12} y={134} text={`${ap()?.stations ?? 0} stations`} size="xs" color={C.muted} />
      </Box>
      <Button x={300} y={234} w={252} h={36} label={ap()?.on ? "Stop the access point" : "Start the access point"}
        color={ap()?.on ? C.raised : C.violet} onPress={toggleAp} />
    </Show>
    <Label x={300} y={282} w={252} text="Open network, beside the station connection." size="xs" color={C.muted} />
  </Window>;
}

// ---- power -------------------------------------------------------------------------------------------

export type PowerAction = "off" | "touch" | "shake" | "rtc";

const POWER_TEXT: Record<PowerAction, { title: string; lines: string[]; confirm: string; color: string }> = {
  off: { title: "Power off", lines: ["The Tab5 will shut down.", "Press the power button to start it again."], confirm: "Shut down", color: "#b91c1c" },
  touch: { title: "Sleep", lines: ["The backlight turns off.", "Touch the screen to wake it."], confirm: "Sleep", color: "#65a30d" },
  shake: { title: "Off until shaken", lines: ["Lay the Tab5 still, then confirm.", "Shaking it powers it back on."], confirm: "Power off", color: "#d97706" },
  rtc: { title: "Off until the RTC alarm", lines: ["The RX8130CE alarm powers the Tab5", "back on at the next full minute."], confirm: "Power off", color: "#2563eb" },
};

export function PowerWindow(props: { action: PowerAction; onClose: () => void }) {
  const text = POWER_TEXT[props.action];
  const [countdown, setCountdown] = createSignal(props.action === "off" ? 15 : 0);
  const [phase, setPhase] = createSignal<"confirm" | "arming" | "asleep" | "failed">("confirm");
  const [request, setRequest] = createSignal(0);
  let frames = 0;
  let brightness = 100;
  const shutDown = () => {
    if (powerOff()) return;
    setPhase("failed");
    notify(device()?.error || "The board did not power off");
    if (props.action === "rtc" || props.action === "shake") send(Command.disarmWake);
  };
  const confirm = () => {
    click();
    if (props.action === "off") { shutDown(); return; }
    if (props.action === "touch") {
      brightness = device()?.display.brightness || 100;
      setBrightness(0);
      setPhase("asleep");
      return;
    }
    setPhase("arming");
    setRequest(send(props.action === "rtc" ? Command.armRtcWake : Command.armMotionWake));
  };
  onFrame(() => {
    if (props.action === "off" && phase() === "confirm" && ++frames % 30 === 0) {
      setCountdown(countdown() - 1);
      if (countdown() <= 0) shutDown();
    }
    const s = sensors();
    if (phase() === "arming" && request() && s.wakeSequence === request()) {
      if (s.wake === Wake.rtc || s.wake === Wake.motion) shutDown();
      else { setPhase("failed"); notify("The wake-up did not arm"); }
    }
  });
  const wake = () => {
    setBrightness(brightness);
    refreshDevice();
    setPhase("confirm");
    props.onClose();
  };
  return <Show when={phase() !== "asleep"} fallback={
    <View class="absolute" focusable onPress={wake} style={{ insetL: 0, insetT: 0, width: 640, height: 360, bgColor: "#000000" }} />}>
    <Window title={text.title} accent={text.color} onClose={props.onClose}>
      <For each={text.lines}>{(line, index) => <Label x={40} y={90 + index() * 30} text={line} size="lg" />}</For>
      <Show when={props.action === "off" && phase() === "confirm"}>
        <Label x={40} y={160} text={`Shutting down in ${countdown()} s.`} size="sm" color={C.muted} />
      </Show>
      <Show when={phase() === "arming"}><Label x={40} y={160} text="Arming the wake-up..." size="sm" color={C.muted} /></Show>
      <Show when={phase() === "failed"}>
        <Label x={40} y={160} text={device()?.error || "Not armed; see the serial log"} size="sm" color={C.red} />
        <Label x={40} y={184} text="On USB power the Tab5 stays on; run it from its battery to power off." size="xs" color={C.muted} />
      </Show>
      <Button x={40} y={230} w={200} h={48} label="Cancel" size="base" onPress={props.onClose} />
      <Button x={328} y={230} w={200} h={48} label={text.confirm} size="base" color={text.color} onPress={confirm}
        disabled={phase() === "arming"} />
    </Window>
  </Show>;
}
