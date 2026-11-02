// Worker MQTT: pick or type a broker on the Broker page; the worker keeps
// one session with it. Once the radio first connects the app opens the
// saved broker (mqtt.json in its data root, a public broker before the
// first save) and runs the checks. The picture is the auxiliary surface of
// the tab5-home profile (640x360).
import { createSignal, For, Show, type JSX } from "solid-js";
import { mount } from "@pocketjs/framework/solid";
import { AuxiliarySurface, Text, View } from "@pocketjs/framework/components";
import { onFrame } from "@pocketjs/framework/lifecycle";
import { existsSync, readFileSync, writeFileSync } from "@pocketjs/framework/fs";
import { wifiHost, type WifiStatus } from "@pocketjs/framework/wifi";
import { worker } from "@pocketjs/framework/worker/channels";
import {
  decodeJson, decodeText, DEFAULT_BROKER, encodeJson, IDLE_STATUS, PRESETS, type Broker, type Command, type Status,
} from "./protocol";

const C = { bg: "#0b151e", panel: "#142630", raised: "#203540", text: "#e7f0f1", muted: "#8ca6af", accent: "#2f6fdb", good: "#2e9e62", bad: "#c2453b" };
/* Every printable ASCII character, so the baked glyphs cover any URL, user
 * name or password typed on the keyboard below. */
export const ASCII = " !\"#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`abcdefghijklmnopqrstuvwxyz{|}~";
const LAYOUTS = {
  lower: ["1234567890", "qwertyuiop", "asdfghjkl'", "zxcvbnm,./"],
  upper: ["1234567890", "QWERTYUIOP", "ASDFGHJKL\"", "ZXCVBNM<>?"],
  symbols: ["!@#$%^&*()", "-_=+[]{}\\|", ";:`~", ""],
} as const;
type Layout = keyof typeof LAYOUTS;
type Field = "url" | "username" | "password";
const FIELDS: readonly { key: Field; label: string }[] = [
  { key: "url", label: "URL" }, { key: "username", label: "User" }, { key: "password", label: "Password" },
];
const SETTINGS = "mqtt.json";
const LOG_LINES = 7;

function Button(props: { x: number; y: number; w: number; h: number; onPress: () => void;
  color?: string; children: JSX.Element }) {
  return <View focusable onPress={props.onPress} class="absolute flex-col rounded-md items-center justify-center active:opacity-70"
    style={{ insetL: props.x, insetT: props.y, width: props.w, height: props.h, bgColor: props.color ?? C.raised }}>
    {props.children}
  </View>;
}
function Label(props: { x: number; y: number; text: string; color?: string; size?: "base" | "lg" }) {
  return <Text class={props.size === "lg" ? "absolute text-lg" : props.size === "base" ? "absolute text-base" : "absolute text-sm"}
    style={{ insetL: props.x, insetT: props.y, textColor: props.color ?? C.muted }}>{props.text}</Text>;
}
function Caption(props: { text: string }) {
  return <Text class="text-base" style={{ textColor: C.text }}>{props.text}</Text>;
}

/* Ten keys a row, then shift, symbols, space, delete, cancel and done. */
function Keyboard(props: { onKey: (key: string) => void; onDelete: () => void; onCancel: () => void; onDone: () => void }) {
  const [layout, setLayout] = createSignal<Layout>("lower");
  return <>
    <For each={[0, 1, 2, 3]}>{row =>
      <For each={[...LAYOUTS[layout()][row]]}>{(key, column) =>
        <Button x={10 + column() * 62} y={120 + row * 44} w={58} h={40} onPress={() => props.onKey(key)}>
          <Text class="text-lg" style={{ textColor: C.text }}>{key}</Text>
        </Button>}
      </For>}
    </For>
    <Button x={10} y={300} w={90} h={44} onPress={() => setLayout(layout() === "upper" ? "lower" : "upper")}
      color={layout() === "upper" ? C.accent : C.raised}><Caption text="Shift" /></Button>
    <Button x={104} y={300} w={90} h={44} onPress={() => setLayout(layout() === "symbols" ? "lower" : "symbols")}
      color={layout() === "symbols" ? C.accent : C.raised}><Caption text="#+=" /></Button>
    <Button x={198} y={300} w={150} h={44} onPress={() => props.onKey(" ")}><Caption text="Space" /></Button>
    <Button x={352} y={300} w={90} h={44} onPress={props.onDelete}><Caption text="Delete" /></Button>
    <Button x={446} y={300} w={90} h={44} onPress={props.onCancel}><Caption text="Cancel" /></Button>
    <Button x={540} y={300} w={90} h={44} onPress={props.onDone} color={C.accent}><Caption text="Done" /></Button>
  </>;
}

function loadBroker(): Broker {
  try {
    if (existsSync(SETTINGS)) return { ...DEFAULT_BROKER, ...JSON.parse(readFileSync(SETTINGS, "utf8")) as Partial<Broker> };
  } catch { /* a broken file falls back to the default broker */ }
  return { ...DEFAULT_BROKER };
}
function saveBroker(broker: Broker): void {
  try { writeFileSync(SETTINGS, JSON.stringify(broker)); } catch { /* the session still opens */ }
}

mount(() => {
  const commands = worker.channel("command"), statusChannel = worker.channel("status"), logChannel = worker.channel("log");
  const [page, setPage] = createSignal<"broker" | "session">("session");
  const [broker, setBroker] = createSignal<Broker>(loadBroker());
  const [editing, setEditing] = createSignal<Field>();
  const [draft, setDraft] = createSignal("");
  const [status, setStatus] = createSignal<Status>(IDLE_STATUS);
  const [lines, setLines] = createSignal<string[]>([]);
  let radioText = "";
  const parseRadio = (text: string): WifiStatus | null => {
    radioText = text;
    try { return text ? JSON.parse(text) as WifiStatus : null; } catch { return null; }
  };
  const [radio, setRadio] = createSignal<WifiStatus | null>(parseRadio(wifiHost()?.status() ?? ""));
  const send = (command: Command) => commands.publish(encodeJson(command));
  const connect = (checks: boolean) => {
    saveBroker(broker());
    send({ op: "connect", broker: broker(), checks });
    setPage("session");
  };
  let frame = 0, started = false;
  onFrame(() => {
    if (++frame % 10 === 0) {
      const text = wifiHost()?.status() ?? "";
      if (text !== radioText) setRadio(parseRadio(text));
    }
    if (!started && radio()?.state === "connected" && send({ op: "connect", broker: broker(), checks: true })) started = true;
    for (const record of statusChannel.observe()) {
      const next = decodeJson<Status>(record.copy());
      if (next) setStatus(next);
    }
    const arrived: string[] = [];
    for (const record of logChannel.observe()) arrived.push(decodeText(record.copy()));
    if (arrived.length) setLines([...lines(), ...arrived].slice(-LOG_LINES));
  });
  const header = () => {
    const wifi = radio();
    const s = status();
    const net = !wifi ? "No radio" : wifi.state === "connected" ? `Wi-Fi ${wifi.ssid}` : `Wi-Fi ${wifi.state}`;
    return `${net} / MQTT ${s.state}${s.state === "connected" ? ` (${s.protocol})` : ""}`;
  };
  const shown = (field: Field) => {
    const value = broker()[field];
    return field === "password" ? "*".repeat(value.length) : value;
  };
  const stateColor = () => status().state === "connected" ? C.good : status().state === "idle" ? C.muted : C.bad;
  return <AuxiliarySurface>
    <View class="w-full h-full" style={{ bgColor: C.bg }}>
      <Show when={editing() === undefined}>
        <Button x={10} y={6} w={100} h={32} onPress={() => setPage("broker")} color={page() === "broker" ? C.accent : C.raised}>
          <Caption text="Broker" />
        </Button>
        <Button x={114} y={6} w={100} h={32} onPress={() => setPage("session")} color={page() === "session" ? C.accent : C.raised}>
          <Caption text="Session" />
        </Button>
        <Label x={226} y={14} text={header()} />
      </Show>

      <Show when={page() === "broker" && editing() === undefined}>
        <For each={PRESETS}>{(preset, index) =>
          <Button x={10 + (index() % 4) * 156} y={48 + Math.floor(index() / 4) * 38} w={150} h={32}
            color={broker().url === preset.url ? C.accent : C.panel}
            onPress={() => setBroker({ ...broker(), url: preset.url })}>
            <Text class="text-sm" style={{ textColor: C.text }}>{preset.label}</Text>
          </Button>}
        </For>
        <For each={FIELDS}>{(field, index) => <>
          <Label x={10} y={102 + index() * 42} size="base" text={field.label} />
          <Button x={110} y={92 + index() * 42} w={520} h={36} color={C.panel}
            onPress={() => { setDraft(broker()[field.key]); setEditing(field.key); }}>
            <Text class="text-base" style={{ textColor: C.text }}>{shown(field.key) || "(none)"}</Text>
          </Button>
        </>}</For>
        <Button x={10} y={230} w={120} h={36} color={broker().protocol === "3.1.1" ? C.accent : C.raised}
          onPress={() => setBroker({ ...broker(), protocol: "3.1.1" })}><Caption text="MQTT 3.1.1" /></Button>
        <Button x={134} y={230} w={120} h={36} color={broker().protocol === "5" ? C.accent : C.raised}
          onPress={() => setBroker({ ...broker(), protocol: "5" })}><Caption text="MQTT 5" /></Button>
        <Button x={400} y={230} w={110} h={36} color={C.accent} onPress={() => connect(false)}><Caption text="Connect" /></Button>
        <Button x={516} y={230} w={114} h={36} onPress={() => send({ op: "disconnect" })}><Caption text="Disconnect" /></Button>
        <Label x={10} y={282} text="mqtt://, mqtts://, ws:// or wss://host[:port][/path]; a TLS broker is checked against the firmware's CA bundle." />
        <Label x={10} y={304} text="Join a Wi-Fi network in Pocket Shell or Worker HTTP first. Connect saves the broker." />
      </Show>

      <Show when={editing() !== undefined}>
        <Label x={10} y={50} text={`${FIELDS.find(field => field.key === editing())?.label ?? ""}`} />
        <View class="absolute rounded-md" style={{ insetL: 10, insetT: 72, width: 620, height: 40, bgColor: C.panel }}>
          <Text class="text-lg" style={{ insetL: 10, insetT: 8, textColor: C.text }}>
            {`${editing() === "password" ? "*".repeat(draft().length) : draft().slice(-46)}_`}
          </Text>
        </View>
        <Keyboard
          onKey={key => { if (draft().length < 200) setDraft(draft() + key); }}
          onDelete={() => setDraft(draft().slice(0, -1))}
          onCancel={() => setEditing(undefined)}
          onDone={() => { setBroker({ ...broker(), [editing()!]: draft() }); setEditing(undefined); }} />
      </Show>

      <Show when={page() === "session" && editing() === undefined}>
        <Label x={10} y={48} size="lg" color={stateColor()} text={`${status().state} ${status().url}`} />
        <Label x={10} y={80} size="base" text={status().base ? `${status().base}/#` : "No session"} />
        <Label x={10} y={104} size="base"
          text={`sent ${status().sent} / received ${status().received} / lost ${status().lost}${status().sessionPresent ? " / session present" : ""}`} />
        <Label x={10} y={128} size="base" color={status().checks.failed ? C.bad : C.text}
          text={status().checks.running ? `checks: ${status().checks.passed + status().checks.failed}/${status().checks.total}, ${status().checks.current}`
            : status().checks.total ? `checks: ${status().checks.passed}/${status().checks.total} passed` : "checks: not run"} />
        <Show when={status().error}><Label x={10} y={152} color={C.bad} text={status().error} /></Show>
        <For each={lines()}>{(line, index) =>
          <Label x={10} y={176 + index() * 18} text={line.length > 96 ? `${line.slice(0, 95)}~` : line} />}
        </For>
        <Button x={10} y={310} w={150} h={40} color={C.accent} onPress={() => send({ op: "checks" })}><Caption text="Run checks" /></Button>
        <Button x={166} y={310} w={170} h={40}
          onPress={() => send({ op: "publish", topic: `${status().base}/hello`, payload: "hello from the Tab5", qos: 1, retain: false })}>
          <Caption text="Publish hello" />
        </Button>
        <Button x={342} y={310} w={140} h={40} onPress={() => connect(false)}><Caption text="Reconnect" /></Button>
        <Button x={488} y={310} w={142} h={40} onPress={() => send({ op: "disconnect" })}><Caption text="Disconnect" /></Button>
      </Show>
    </View>
  </AuxiliarySurface>;
});
