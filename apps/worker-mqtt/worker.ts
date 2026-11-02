// One MQTT session at a time, opened by a `connect` command. The session
// subscribes to `<base>/#` (base = pocket-m5/<client id>) after each CONNACK
// without a session, keeps a retained `online` under `<base>/status` with an
// `offline` will, and runs the checks below on request. Results, messages and
// errors print on the serial console and go to the UI on the `log` ring.
import { io, ResourceError, type MqttClient, type MqttMessage } from "@pocketjs/framework/io";
import { utf8Text } from "@pocketjs/framework/io/mqtt";
import { nowMicros, sleep } from "@pocketjs/framework/worker";
import { clip, decodeJson, encodeText, IDLE_STATUS, type Broker, type Command, type Status } from "./protocol";

const statusChannel = io.channel("status"), logChannel = io.channel("log");
let status: Status = { ...IDLE_STATUS, checks: { ...IDLE_STATUS.checks } };
let client: MqttClient | undefined;
let generation = 0;
let checksAfterConnect = false;

function publishStatus(): void { statusChannel.publish(clip(JSON.stringify(status), 512)); }
function log(line: string): void {
  console.log(`mqtt: ${line}`);
  logChannel.publish(clip(line, 256));
}
function describe(error: unknown): string {
  return error instanceof ResourceError ? error.message : String(error);
}
function now(): number { return nowMicros() ?? 0; }

/* ---- message waiters, for the checks ---- */

interface Waiter { readonly topic: string; resolve(message: MqttMessage | undefined): void }
let waiters: Waiter[] = [];
function deliver(message: MqttMessage): boolean {
  const waiter = waiters.find(item => item.topic === message.topic);
  if (!waiter) return false;
  waiters = waiters.filter(item => item !== waiter);
  waiter.resolve(message);
  return true;
}
/** The next message on `topic`, or undefined after `ms`. */
function expectMessage(topic: string, ms: number): Promise<MqttMessage | undefined> {
  return new Promise(resolve => {
    const waiter: Waiter = { topic, resolve };
    waiters.push(waiter);
    void sleep(ms * 1000).then(() => {
      if (!waiters.includes(waiter)) return;
      waiters = waiters.filter(item => item !== waiter);
      resolve(undefined);
    });
  });
}

/* ---- the session ---- */

function randomId(): string {
  let id = "";
  for (let i = 0; i < 6; ++i) id += "0123456789abcdef"[Math.floor(Math.random() * 16)];
  return id;
}

async function session(broker: Broker, mine: number): Promise<void> {
  const clientId = `pocket-m5-${randomId()}`;
  const base = `pocket-m5/${clientId}`;
  let opened: MqttClient;
  try {
    opened = io.mqtt("mqtt").open({
      url: broker.url, protocol: broker.protocol, clientId,
      ...(broker.username ? { username: broker.username } : {}),
      ...(broker.password ? { password: broker.password } : {}),
      keepAliveSeconds: 30, reconnectMs: 3000, timeoutMs: 10_000, maxMessageBytes: 64 * 1024,
      will: { topic: `${base}/status`, payload: "offline", qos: 1, retain: true },
      ...(broker.protocol === "5" ? { sessionExpirySeconds: 0, userProperties: { device: "M5Stack Tab5" } } : {}),
    });
  } catch (error) {
    status = { ...status, state: "ended", error: describe(error) };
    log(`open ${broker.url}: ${describe(error)}`);
    publishStatus();
    return;
  }
  client = opened;
  status = { ...IDLE_STATUS, checks: { ...IDLE_STATUS.checks }, state: "connecting", url: broker.url, protocol: broker.protocol, base };
  publishStatus();
  log(`open ${broker.url} as ${clientId} (MQTT ${broker.protocol})`);
  for await (const event of opened.events()) {
    if (mine !== generation) break;
    if (event.type === "connected") {
      status = { ...status, state: "connected", sessionPresent: event.sessionPresent, error: "" };
      log(`connected: session ${event.sessionPresent ? "present" : "new"}, max QoS ${event.maximumQos}, receive max ${event.receiveMaximum}, retain ${event.retainAvailable}`);
      publishStatus();
      try {
        if (!event.sessionPresent) {
          const codes = await opened.subscribe({ filter: `${base}/#`, qos: 2 });
          log(`subscribed ${base}/# -> ${codes.join(",")}`);
        }
        await opened.publish(`${base}/status`, "online", { qos: 1, retain: true });
        status.sent++;
      } catch (error) {
        log(`after connect: ${describe(error)}`);
      }
      if (checksAfterConnect) { checksAfterConnect = false; void runChecks(); }
    } else if (event.type === "message") {
      status.received++;
      if (!deliver(event)) log(`rx ${event.topic} q${event.qos}${event.retain ? " retained" : ""}: ${utf8Text(event.payload.subarray(0, 120))}`);
      publishStatus();
    } else if (event.type === "lost") {
      status.lost += event.count;
      log(`lost ${event.count} message${event.count === 1 ? "" : "s"}`);
      publishStatus();
    } else if (event.type === "disconnected") {
      status = { ...status, state: "disconnected", error: `${event.code}: ${event.message}` };
      log(`disconnected: ${event.code}: ${event.message}`);
      publishStatus();
    } else if (event.type === "ended") {
      status = { ...status, state: "ended" };
      log(`ended with status ${event.status}`);
      publishStatus();
    }
  }
  if (client === opened) client = undefined;
}

function stop(): void {
  ++generation;
  const old = client;
  client = undefined;
  waiters.forEach(waiter => waiter.resolve(undefined));
  waiters = [];
  old?.close();
}

/* ---- checks against the connected broker ---- */

interface Check { readonly name: string; readonly v5?: boolean; run(c: MqttClient, base: string): Promise<string> }
function pattern(length: number, seed: number): Uint8Array {
  const out = new Uint8Array(length);
  for (let i = 0; i < length; ++i) out[i] = (i * 31 + seed) & 0xff;
  return out;
}
function same(a: Uint8Array, b: Uint8Array): boolean {
  if (a.length !== b.length) return false;
  for (let i = 0; i < a.length; ++i) if (a[i] !== b[i]) return false;
  return true;
}
function fail(message: string): never { throw new Error(message); }
async function loopback(c: MqttClient, topic: string, payload: Uint8Array | string, qos: 0 | 1 | 2): Promise<string> {
  const arrival = expectMessage(topic, 10_000);
  const t0 = now();
  const reason = await c.publish(topic, payload, { qos });
  status.sent++;
  const acked = now();
  const message = await arrival;
  if (!message) fail("no loopback within 10 s");
  const bytes = typeof payload === "string" ? encodeText(payload) : payload;
  if (!same(message.payload, bytes)) fail(`payload differs (${message.payload.length} bytes, sent ${bytes.length})`);
  if (message.qos !== qos) fail(`arrived at QoS ${message.qos}`);
  return `ack ${Math.round((acked - t0) / 1000)} ms (reason ${reason}), loopback ${Math.round((now() - t0) / 1000)} ms`;
}

const CHECKS: readonly Check[] = [
  { name: "QoS 0 loopback", run: (c, base) => loopback(c, `${base}/check/q0`, "zero", 0) },
  { name: "QoS 1 loopback", run: (c, base) => loopback(c, `${base}/check/q1`, "one", 1) },
  { name: "QoS 2 loopback", run: (c, base) => loopback(c, `${base}/check/q2`, "two", 2) },
  { name: "8 KiB staged loopback", run: (c, base) => loopback(c, `${base}/check/8k`, pattern(8 * 1024, 1), 1) },
  { name: "32 KiB staged loopback", run: (c, base) => loopback(c, `${base}/check/32k`, pattern(32 * 1024, 2), 1) },
  {
    name: "oversize message dropped",
    async run(c, base) {
      /* 80 KiB against maxMessageBytes 64 KiB: under 3.1.1 the host drops it
       * and counts it lost; under MQTT 5 the broker keeps it back, since the
       * session announced a smaller maximum packet size. */
      const topic = `${base}/check/oversize`;
      const before = status.lost;
      const arrival = expectMessage(topic, 4000);
      await c.publish(topic, pattern(80 * 1024, 3), { qos: 1 });
      status.sent++;
      if (await arrival) fail("the oversize message was delivered");
      for (let waited = 0; c.protocol === "3.1.1" && status.lost === before && waited < 6000; waited += 100) await sleep(100_000);
      if (c.protocol === "3.1.1" && status.lost === before) fail("no lost count");
      if (c.protocol === "5" && status.lost !== before) fail("a message was counted lost");
      return c.protocol === "5" ? "the broker kept it back" : `counted lost (${status.lost - before})`;
    },
  },
  {
    name: "retained message",
    async run(c, base) {
      const topic = `${base}-r/kept`;
      await c.publish(topic, "kept", { qos: 1, retain: true });
      const arrival = expectMessage(topic, 10_000);
      const codes = await c.subscribe({ filter: topic, qos: 1 });
      const message = await arrival;
      await c.unsubscribe(topic);
      await c.publish(topic, undefined, { qos: 1, retain: true });
      status.sent += 2;
      if (!message) fail(`no retained message (SUBACK ${codes.join(",")})`);
      if (!message.retain || utf8Text(message.payload) !== "kept") fail(`retain ${message.retain}, payload ${utf8Text(message.payload)}`);
      return `SUBACK ${codes.join(",")}, retained copy arrived`;
    },
  },
  {
    name: "unsubscribe stops delivery",
    async run(c, base) {
      const topic = `${base}-u/x`;
      await c.subscribe({ filter: topic, qos: 1 });
      const first = expectMessage(topic, 10_000);
      await c.publish(topic, "before", { qos: 1 });
      if (!await first) fail("no message before unsubscribing");
      const codes = await c.unsubscribe(topic);
      const late = expectMessage(topic, 1500);
      await c.publish(topic, "after", { qos: 1 });
      status.sent += 2;
      if (await late) fail("a message arrived after UNSUBACK");
      return `UNSUBACK ${codes.join(",")}, nothing after it`;
    },
  },
  {
    name: "MQTT 5 message properties", v5: true,
    async run(c, base) {
      const topic = `${base}/check/props`;
      const arrival = expectMessage(topic, 10_000);
      await c.publish(topic, "{\"t\":21.5}", {
        qos: 1, payloadUtf8: true, contentType: "application/json", responseTopic: `${base}/check/reply`,
        correlationData: "req-7", messageExpirySeconds: 60, userProperties: [["k", "v"], ["k", "w"]],
      });
      status.sent++;
      const message = await arrival;
      if (!message) fail("no loopback within 10 s");
      const p = message.properties;
      const users = p.userProperties.map(([k, v]) => `${k}=${v}`).join(",");
      if (p.contentType !== "application/json" || p.responseTopic !== `${base}/check/reply` || !p.payloadUtf8 ||
        utf8Text(p.correlationData ?? new Uint8Array()) !== "req-7" || users !== "k=v,k=w")
        fail(`properties ${JSON.stringify({ ...p, correlationData: p.correlationData && utf8Text(p.correlationData) })}`);
      return `content type, response topic, correlation data and ${p.userProperties.length} user properties kept`;
    },
  },
  {
    name: "MQTT 5 subscription identifier", v5: true,
    async run(c, base) {
      const topic = `${base}-s/x`;
      const codes = await c.subscribe({ filter: topic, qos: 1 }, { subscriptionIdentifier: 42 });
      const arrival = expectMessage(topic, 10_000);
      await c.publish(topic, "id", { qos: 1 });
      const message = await arrival;
      await c.unsubscribe(topic);
      status.sent++;
      if (!message) fail(`no message (SUBACK ${codes.join(",")})`);
      if (message.properties.subscriptionIdentifier !== 42) fail(`identifier ${message.properties.subscriptionIdentifier}`);
      return "identifier 42 on the message";
    },
  },
  {
    name: "MQTT 5 no local", v5: true,
    async run(c, base) {
      const topic = `${base}-n/x`;
      await c.subscribe({ filter: topic, qos: 1 }, { noLocal: true });
      const late = expectMessage(topic, 1500);
      await c.publish(topic, "own", { qos: 1 });
      const message = await late;
      await c.unsubscribe(topic);
      status.sent++;
      if (message) fail("the broker sent our own message back");
      return "own message not delivered";
    },
  },
];

async function runChecks(): Promise<void> {
  const c = client;
  if (!c || status.state !== "connected" || status.checks.running) {
    log("checks need a connected session");
    return;
  }
  const list = CHECKS.filter(check => !check.v5 || c.protocol === "5");
  status.checks = { running: true, passed: 0, failed: 0, total: list.length, current: "" };
  log(`checks: ${list.length} against ${status.url} (MQTT ${c.protocol})`);
  for (const check of list) {
    if (client !== c) break;
    status.checks.current = check.name;
    publishStatus();
    const t0 = now();
    try {
      const detail = await check.run(c, status.base);
      status.checks.passed++;
      log(`check ${check.name}: ok, ${Math.round((now() - t0) / 1000)} ms, ${detail}`);
    } catch (error) {
      status.checks.failed++;
      log(`check ${check.name}: FAILED, ${describe(error)}`);
    }
  }
  status.checks.running = false;
  status.checks.current = "";
  log(`checks: ${status.checks.passed}/${status.checks.total} passed`);
  publishStatus();
}

async function command(record: Uint8Array): Promise<void> {
  const cmd = decodeJson<Command>(record);
  if (!cmd) return;
  if (cmd.op === "connect") {
    stop();
    checksAfterConnect = cmd.checks;
    void session(cmd.broker, generation);
  } else if (cmd.op === "disconnect") {
    const old = client;
    ++generation;
    client = undefined;
    try {
      if (old?.protocol === "5") await old.disconnect({ reasonCode: 0, userProperties: { reason: "user" } });
      else old?.close();
    } catch (error) {
      log(`disconnect: ${describe(error)}`);
      old?.close();
    }
    status = { ...status, state: "idle", error: "" };
    log("disconnected by request");
    publishStatus();
  } else if (cmd.op === "publish") {
    const c = client;
    if (!c) { log("publish needs a session"); return; }
    try {
      const reason = await c.publish(cmd.topic, cmd.payload, { qos: cmd.qos, retain: cmd.retain });
      status.sent++;
      log(`tx ${cmd.topic} q${cmd.qos}: reason ${reason}`);
    } catch (error) {
      log(`tx ${cmd.topic}: ${describe(error)}`);
    }
    publishStatus();
  } else if (cmd.op === "checks") {
    void runChecks();
  }
}

publishStatus();
async function main(): Promise<void> {
  for await (const record of io.channel("command").observe()) await command(record);
}
main();
