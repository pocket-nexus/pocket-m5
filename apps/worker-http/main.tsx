// Worker HTTP: join a Wi-Fi network on the Wi-Fi page, then the worker
// fetches the first target once the radio first connects, and a target on
// each tap of the HTTP page. The picture is the auxiliary surface of the
// tab5-home profile (640x360).
import { createSignal, For, Show, type JSX } from "solid-js";
import { mount } from "@pocketjs/framework/solid";
import { AuxiliarySurface, Text, View } from "@pocketjs/framework/components";
import { onFrame } from "@pocketjs/framework/lifecycle";
import { joinNetwork, scanNetworks, wifiHost, type WifiStatus } from "@pocketjs/framework/wifi";
import { worker } from "@pocketjs/framework/worker/channels";
import { decodeProgress, PHASES, TARGETS, type Progress } from "./protocol";

const C = { bg: "#0b151e", panel: "#142630", raised: "#203540", text: "#e7f0f1", muted: "#8ca6af", accent: "#2f6fdb" };
/* Every printable ASCII character, so the baked glyphs cover any SSID or
 * passphrase typed on the keyboard below. */
export const ASCII = " !\"#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`abcdefghijklmnopqrstuvwxyz{|}~";
const LAYOUTS = {
  lower: ["1234567890", "qwertyuiop", "asdfghjkl'", "zxcvbnm,./"],
  upper: ["1234567890", "QWERTYUIOP", "ASDFGHJKL\"", "ZXCVBNM<>?"],
  symbols: ["!@#$%^&*()", "-_=+[]{}\\|", ";:`~", ""],
} as const;
type Layout = keyof typeof LAYOUTS;

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

/* Ten keys a row, then shift, symbols, space, delete, cancel and join. */
function Keyboard(props: { onKey: (key: string) => void; onDelete: () => void; onCancel: () => void; onJoin: () => void }) {
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
      color={layout() === "upper" ? C.accent : C.raised}>
      <Text class="text-base" style={{ textColor: C.text }}>Shift</Text>
    </Button>
    <Button x={104} y={300} w={90} h={44} onPress={() => setLayout(layout() === "symbols" ? "lower" : "symbols")}
      color={layout() === "symbols" ? C.accent : C.raised}>
      <Text class="text-base" style={{ textColor: C.text }}>#+=</Text>
    </Button>
    <Button x={198} y={300} w={150} h={44} onPress={() => props.onKey(" ")}>
      <Text class="text-base" style={{ textColor: C.text }}>Space</Text>
    </Button>
    <Button x={352} y={300} w={90} h={44} onPress={props.onDelete}>
      <Text class="text-base" style={{ textColor: C.text }}>Delete</Text>
    </Button>
    <Button x={446} y={300} w={90} h={44} onPress={props.onCancel}>
      <Text class="text-base" style={{ textColor: C.text }}>Cancel</Text>
    </Button>
    <Button x={540} y={300} w={90} h={44} onPress={props.onJoin} color={C.accent}>
      <Text class="text-base" style={{ textColor: C.text }}>Join</Text>
    </Button>
  </>;
}

mount(() => {
  const request = worker.channel("request"), progress = worker.channel("progress");
  const [page, setPage] = createSignal<"wifi" | "http">("wifi");
  /* The status JSON is read every 10 frames and parsed only when it changed,
   * so an unchanged network list keeps its rows. */
  let status = "";
  const parse = (text: string): WifiStatus | null => {
    status = text;
    try { return text ? JSON.parse(text) as WifiStatus : null; } catch { return null; }
  };
  const [radio, setRadio] = createSignal<WifiStatus | null>(parse(wifiHost()?.status() ?? ""));
  const [entry, setEntry] = createSignal<string>();
  const [passphrase, setPassphrase] = createSignal("");
  const [latest, setLatest] = createSignal<Progress>({ phase: 0, bytes: 0, status: 0, code: 0, target: 0 });
  let frame = 0, fetched = false;
  onFrame(() => {
    if (++frame % 10 === 0) {
      const text = wifiHost()?.status() ?? "";
      if (text !== status) setRadio(parse(text));
    }
    if (!fetched && radio()?.state === "connected" && request.publish(Uint8Array.of(0))) {
      fetched = true;
      setPage("http");
    }
    for (const record of progress.observe()) setLatest(decodeProgress(record.copy()));
  });
  const join = (ssid: string, key: string) => {
    joinNetwork(ssid, key);
    setEntry(undefined);
    setPassphrase("");
  };
  const header = () => {
    const status = radio();
    if (!status) return "No radio";
    const where = status.state === "connected" ? `${status.ssid} ${status.address}` : status.ssid;
    return `${status.state} ${where}${status.error ? ` - ${status.error}` : ""}`;
  };
  return <AuxiliarySurface>
    <View class="w-full h-full" style={{ bgColor: C.bg }}>
      <Button x={10} y={6} w={90} h={32} onPress={() => setPage("wifi")} color={page() === "wifi" ? C.accent : C.raised}>
        <Text class="text-base" style={{ textColor: C.text }}>Wi-Fi</Text>
      </Button>
      <Button x={104} y={6} w={90} h={32} onPress={() => setPage("http")} color={page() === "http" ? C.accent : C.raised}>
        <Text class="text-base" style={{ textColor: C.text }}>HTTP</Text>
      </Button>
      <Label x={206} y={14} text={header()} />

      <Show when={page() === "wifi" && entry() === undefined}>
        <Button x={10} y={48} w={90} h={36} onPress={() => scanNetworks()}>
          <Text class="text-base" style={{ textColor: C.text }}>Scan</Text>
        </Button>
        <Label x={110} y={58} text={radio()?.scanning ? "Scanning..." : "Tap a network to join it."} />
        <For each={(radio()?.networks ?? []).slice(0, 6)}>{(network, index) =>
          <Button x={10} y={92 + index() * 44} w={620} h={40} color={C.panel}
            onPress={() => network.secure ? setEntry(network.ssid) : join(network.ssid, "")}>
            <Text class="text-base" style={{ textColor: C.text }}>
              {`${network.ssid}  ${"|".repeat(network.strength)}${network.secure ? "  (secured)" : ""}`}
            </Text>
          </Button>}
        </For>
      </Show>

      <Show when={page() === "wifi" && entry() !== undefined}>
        <Label x={10} y={50} text={`Passphrase for ${entry()}`} />
        <View class="absolute rounded-md" style={{ insetL: 10, insetT: 72, width: 620, height: 40, bgColor: C.panel }}>
          <Text class="text-lg" style={{ insetL: 10, insetT: 8, textColor: C.text }}>{`${passphrase()}_`}</Text>
        </View>
        <Keyboard
          onKey={key => { if (passphrase().length < 63) setPassphrase(passphrase() + key); }}
          onDelete={() => setPassphrase(passphrase().slice(0, -1))}
          onCancel={() => { setEntry(undefined); setPassphrase(""); }}
          onJoin={() => join(entry()!, passphrase())} />
      </Show>

      <Show when={page() === "http"}>
        <For each={TARGETS}>{(url, index) =>
          <Button x={10 + index() * 310} y={52} w={300} h={44} color={C.accent}
            onPress={() => request.publish(Uint8Array.of(index()))}>
            <Text class="text-base" style={{ textColor: C.text }}>{url}</Text>
          </Button>}
        </For>
        <Label x={10} y={116} size="lg" color={C.text}
          text={`${TARGETS[latest().target]}: ${PHASES[latest().phase] ?? "Unknown"}`} />
        <Label x={10} y={150} size="base"
          text={`HTTP ${latest().code} / ${latest().bytes} bytes / resource status ${latest().status}`} />
        <Label x={10} y={184} text="Headers and errors print on the serial console." />
      </Show>
    </View>
  </AuxiliarySurface>;
});
