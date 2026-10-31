// Tab5 Demo: the board's hardware on one screen, after M5Stack's
// M5Tab5-UserDemo. Live panels show the RTC, the battery monitor, the IMU,
// the backlight, the speaker level, the power rails and the plugs; the tiles
// along the bottom open the camera, the microphones, the music test, the
// card, the I2C scanner, the header pins, the RS485 monitor and Wi-Fi. The
// picture is the auxiliary surface of the tab5-home profile (640x360).
import { createMemo, createSignal, For, Match, Show, Switch } from "solid-js";
import { mount } from "@pocketjs/framework/solid";
import { AuxiliarySurface, Text, View } from "@pocketjs/framework/components";
import { setBrightness, setSwitch, setVolume } from "@pocketjs/framework/device";
import { Driver } from "./protocol";
import { click, down, up } from "./sound";
import { device, notify, refreshDevice, sensors, startStore, toast, wifi } from "./store";
import { Box, Button, C, Indicator, Label, Panel, Toast, Toggle, pad2 } from "./ui";
import {
  CameraWindow, CardWindow, GpioWindow, MicWindow, MusicWindow, PowerWindow, RtcWindow, ScanWindow,
  SerialWindow, WifiWindow, type PowerAction,
} from "./windows";

type Tool = "rtc" | "camera" | "mic" | "headset" | "music" | "card" | "scan" | "gpio" | "serial" | "wifi" | "power";

const TILES: { tool: Tool; label: string; color: string }[] = [
  { tool: "camera", label: "Camera", color: "#1d4ed8" },
  { tool: "mic", label: "Mics", color: "#0f766e" },
  { tool: "headset", label: "Headset", color: "#7c3aed" },
  { tool: "music", label: "Music", color: "#be185d" },
  { tool: "card", label: "SD card", color: "#a16207" },
  { tool: "scan", label: "I2C", color: "#0369a1" },
  { tool: "gpio", label: "GPIO", color: "#15803d" },
  { tool: "serial", label: "RS485", color: "#b45309" },
  { tool: "wifi", label: "Wi-Fi", color: "#4338ca" },
];

const SWITCH_ACCENTS: Record<string, string> = {
  charge: C.rose, "quick-charge": C.amber, "usb-5v": C.red, "ext-5v": C.red, antenna: C.blue,
};

function Header() {
  const radio = () => {
    const status = wifi();
    if (!status) return "No radio";
    if (status.state === "connected") return `${status.ssid} ${status.address}`;
    return status.state === "off" ? "Wi-Fi off" : status.state === "connecting" ? `Joining ${status.ssid}` : "Wi-Fi idle";
  };
  return <>
    <Label x={10} y={5} text="M5Stack Tab5" size="sm" bold />
    <Label x={110} y={6} text="PocketJS hardware demo" size="xs" color={C.muted} />
    <Label x={440} y={6} w={192} text={radio()} size="xs" color={C.muted} />
  </>;
}

/* Each displayed string is a memo: a text node changes only when its
 * characters do, not with every sensor record. */
function RtcPanel(props: { open: () => void }) {
  const s = sensors;
  const time = createMemo(() => `${pad2(s().hour)}:${pad2(s().minute)}:${pad2(s().second)}`);
  const date = createMemo(() => `${s().year}-${pad2(s().month)}-${pad2(s().day)}`);
  const known = createMemo(() => s().year > 0);
  const valid = createMemo(() => s().rtcValid);
  return <Panel x={8} y={30} w={150} h={84} title="RTC  RX8130CE" accent={C.teal}>
    <Show when={known()} fallback={<Label x={10} y={30} text="Reading..." size="base" color={C.muted} />}>
      <Label x={10} y={24} text={time()} size="2xl" bold />
      <Label x={10} y={56} text={date()} size="xs" color={C.muted} />
    </Show>
    <Indicator x={92} y={56} label={valid() ? "valid" : "invalid"} on={valid()} accent={C.teal} />
    <View class="absolute active:opacity-60" focusable onPress={props.open}
      style={{ insetL: 0, insetT: 0, width: 150, height: 84 }} />
  </Panel>;
}

function PowerPanel() {
  const s = sensors;
  const state = createMemo(() => s().power);
  const volts = createMemo(() => `${s().volts.toFixed(2)} V`);
  const flow = createMemo(() => `${s().amps >= 0 ? "+" : ""}${s().amps.toFixed(2)} A  ${s().watts.toFixed(1)} W`);
  const direction = createMemo(() => s().amps > 0.01 ? 1 : s().amps < -0.01 ? -1 : 0);
  const temperature = createMemo(() => device()?.temperature != null ? `SoC ${device()!.temperature!.toFixed(0)} C` : "");
  return <Panel x={8} y={118} w={150} h={90} title="BATTERY  INA226" accent={C.green}>
    <Show when={state() === Driver.ready} fallback={
      <Label x={10} y={30} text={state() === Driver.fault ? "No answer" : "Starting..."} size="base" color={C.muted} />}>
      <Label x={10} y={22} text={volts()} size="2xl" bold />
      <Label x={10} y={52} text={flow()} size="xs" />
      <Label x={10} y={68} text={direction() > 0 ? "^ charging" : direction() < 0 ? "v discharging" : "- idle"}
        size="xs" color={direction() > 0 ? C.green : C.amber} />
    </Show>
    <Label x={96} y={68} text={temperature()} size="xs" color={C.muted} />
  </Panel>;
}

function DisplayPanel() {
  const brightness = () => device()?.display.brightness ?? 0;
  const change = (delta: number) => {
    const next = Math.max(20, Math.min(100, brightness() + delta));
    if (next === brightness()) { delta > 0 ? up() : down(); return; }
    setBrightness(next);
    refreshDevice();
    delta > 0 ? up() : down();
  };
  return <Panel x={8} y={212} w={150} h={82} title="BACKLIGHT" accent={C.amber}>
    <Label x={10} y={24} text={`${brightness()}%`} size="2xl" bold />
    <Button x={84} y={24} w={28} h={28} label="-" size="lg" onPress={() => change(-20)} />
    <Button x={116} y={24} w={28} h={28} label="+" size="lg" onPress={() => change(20)} />
    <Label x={10} y={60} text={`Panel ${device()?.display.panel || "unknown"}`} size="xs" color={C.muted} />
  </Panel>;
}

function SpeakerPanel() {
  const volume = () => device()?.volume ?? 0;
  const change = (delta: number) => {
    const next = Math.max(0, Math.min(100, volume() + delta));
    setVolume(next);
    refreshDevice();
    delta > 0 ? up() : down();
  };
  return <Panel x={320} y={30} w={150} h={84} title="SPEAKER  ES8388" accent={C.violet}>
    <Label x={10} y={26} text={`${volume()}%`} size="2xl" bold />
    <Button x={84} y={26} w={28} h={28} label="-" size="lg" onPress={() => change(-20)} />
    <Button x={116} y={26} w={28} h={28} label="+" size="lg" onPress={() => change(20)} />
    <Label x={10} y={62} text="Microphones ES7210" size="xs" color={C.muted} />
  </Panel>;
}

function SwitchesPanel() {
  const toggle = (id: string, label: string, on: boolean) => {
    if (!setSwitch(id, !on)) { notify(device()?.error || `${label} did not change`); return; }
    refreshDevice();
    notify(`${label} ${on ? "off" : "on"}`);
    click();
  };
  return <Panel x={164} y={30} w={150} h={264} title="POWER RAILS" accent={C.rose}>
    <For each={device()?.switches ?? []}>{(item, index) =>
      <Toggle x={10} y={26 + index() * 28} w={130} label={item.label} on={item.on}
        accent={SWITCH_ACCENTS[item.id]} onPress={() => toggle(item.id, item.label, item.on)} />}
    </For>
    <Label x={10} y={172} text="PLUGS" size="xs" color={C.muted} bold />
    <For each={device()?.ports ?? []}>{(port, index) =>
      <Indicator x={10} y={192 + index() * 20} label={port.label} on={port.connected} accent={C.amber} />}
    </For>
  </Panel>;
}

function ActionsPanel(props: { run: (action: PowerAction) => void }) {
  return <Panel x={320} y={118} w={150} h={176} title="POWER" accent={C.red}>
    <Button x={10} y={26} w={130} h={32} label="Power off" color="#7f1d1d" onPress={() => props.run("off")} />
    <Button x={10} y={62} w={130} h={32} label="Sleep, touch wakes" color="#3f4d1f" onPress={() => props.run("touch")} />
    <Button x={10} y={98} w={130} h={32} label="Off, shake wakes" color="#78350f" onPress={() => props.run("shake")} />
    <Button x={10} y={134} w={130} h={32} label="Off, RTC wakes" color="#1e3a8a" onPress={() => props.run("rtc")} />
  </Panel>;
}

function ImuPanel() {
  const s = sensors;
  const g = (axis: number) => s().accel[axis]! / 9.80665;
  const clamp = (value: number) => Math.round(Math.max(-48, Math.min(48, value)));
  // Held in landscape, the sensor's y axis runs along the picture's x axis
  // and its x axis points up: the dot rises to the high side like a bubble.
  const dx = createMemo(() => clamp(g(1) * 50)), dy = createMemo(() => clamp(-g(0) * 50));
  const motion = createMemo(() => Math.max(...s().gyro.map(Math.abs)));
  const size = createMemo(() => Math.round(16 + Math.min(1, motion() / 3) * 28) & ~1);
  const axes = [0, 1, 2].map(axis => createMemo(() => `${"XYZ"[axis]} ${g(axis).toFixed(2)} g`));
  const gyro = createMemo(() => `Gyro ${(motion() * 180 / Math.PI).toFixed(0)} dps`);
  const state = createMemo(() => s().imu);
  return <Panel x={476} y={30} w={156} h={264} title="IMU  BMI270" accent={C.blue}>
    <Box x={18} y={30} w={120} h={120} radius={60} color={C.bg} border={C.line}>
      <View class="absolute" style={{ insetL: 59, insetT: 10, width: 2, height: 100, bgColor: C.line }} />
      <View class="absolute" style={{ insetL: 10, insetT: 59, width: 100, height: 2, bgColor: C.line }} />
      <View class="absolute" style={{ insetL: 60 + dx() - size() / 2, insetT: 60 + dy() - size() / 2,
        width: size(), height: size(), radius: size() / 2, bgColor: "#ff589d" }} />
    </Box>
    <Show when={state() === Driver.ready} fallback={
      <Label x={10} y={164} text={state() === Driver.fault ? "No answer" : "Loading features..."} size="xs" color={C.muted} />}>
      <Label x={10} y={160} text={axes[0]!()} size="sm" />
      <Label x={10} y={180} text={axes[1]!()} size="sm" />
      <Label x={10} y={200} text={axes[2]!()} size="sm" />
      <Label x={10} y={224} text={gyro()} size="xs" color={C.muted} />
    </Show>
  </Panel>;
}

mount(() => {
  startStore();
  const [tool, setTool] = createSignal<Tool>();
  const [action, setAction] = createSignal<PowerAction>("off");
  const open = (next: Tool) => { click(); setTool(next); };
  const close = () => { click(); setTool(undefined); };
  return <AuxiliarySurface>
    <View class="w-full h-full" style={{ bgColor: C.bg }}>
      <Header />
      <RtcPanel open={() => open("rtc")} />
      <PowerPanel />
      <DisplayPanel />
      <SwitchesPanel />
      <SpeakerPanel />
      <ActionsPanel run={next => { setAction(next); open("power"); }} />
      <ImuPanel />
      <For each={TILES}>{(tile, index) =>
        <View focusable onPress={() => open(tile.tool)} class="absolute flex-col items-center justify-center active:opacity-60"
          style={{ insetL: 8 + index() * 70, insetT: 300, width: 64, height: 54, radius: 10, bgColor: tile.color }}>
          <Text class="text-sm font-bold" style={{ textColor: C.text }}>{tile.label}</Text>
        </View>}
      </For>
      <Switch>
        <Match when={tool() === "rtc"}><RtcWindow onClose={close} /></Match>
        <Match when={tool() === "camera"}><CameraWindow onClose={close} /></Match>
        <Match when={tool() === "mic"}><MicWindow source="microphones" onClose={close} /></Match>
        <Match when={tool() === "headset"}><MicWindow source="headset" onClose={close} /></Match>
        <Match when={tool() === "music"}><MusicWindow onClose={close} /></Match>
        <Match when={tool() === "card"}><CardWindow onClose={close} /></Match>
        <Match when={tool() === "scan"}><ScanWindow onClose={close} /></Match>
        <Match when={tool() === "gpio"}><GpioWindow onClose={close} /></Match>
        <Match when={tool() === "serial"}><SerialWindow onClose={close} /></Match>
        <Match when={tool() === "wifi"}><WifiWindow onClose={close} /></Match>
        <Match when={tool() === "power"}><PowerWindow action={action()} onClose={close} /></Match>
      </Switch>
      <Toast text={toast()} />
    </View>
  </AuxiliarySurface>;
});
