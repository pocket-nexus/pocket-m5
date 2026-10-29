// One request per record on the `request` channel. The response streams
// through observe(); the completion closes the resource, which ends the
// stream whether or not an end fact arrived. Headers and errors go to the
// serial console.
import { FactKind, io, ResourceStatus } from "@pocketjs/framework/io";
import { settle } from "@pocketjs/framework/worker";
import { encodeProgress, Phase, TARGETS } from "./protocol";

const progress = io.channel("progress");
const record = new Uint8Array(16);

function publish(phase: number, target: number, bytes: number, status: number, code: number): void {
  progress.publish(encodeProgress({ phase, bytes, status, code, target }, record));
}

/** Header records and error texts are ASCII; QuickJS has no TextDecoder. */
function ascii(bytes: Uint8Array): string {
  let text = "";
  for (const byte of bytes) text += String.fromCharCode(byte);
  return text;
}

async function fetchOnce(target: number): Promise<void> {
  const url = TARGETS[target] ?? TARGETS[0];
  const resource = io.http("http").open({ url, timeoutMs: 30_000, maxBytes: 262_144 });
  const request = resource.submit();
  if (request < 0) {
    publish(Phase.failed, target, 0, ResourceStatus.busy, 0);
    resource.close();
    return;
  }
  publish(Phase.accepted, target, 0, 0, 0);
  const completion = settle(request);
  void completion.then(() => resource.close());
  let total = 0, code = 0, ended = false;
  for await (const fact of resource.observe()) {
    if (fact.kind === FactKind.headers) {
      const head = JSON.parse(ascii(fact.data)) as { status: number; url: string };
      code = head.status;
      console.log(`${url} -> ${head.status} ${ascii(fact.data)}`);
      publish(Phase.headers, target, 0, 0, code);
    } else if (fact.kind === FactKind.data) {
      total = fact.offset + fact.data.length;
      publish(Phase.body, target, total, 0, code);
    } else if (fact.kind === FactKind.error) {
      console.log(`${url}: ${ascii(fact.data)}`);
    } else if (fact.kind === FactKind.end) {
      ended = true;
      total = fact.offset;
      publish(fact.status === ResourceStatus.ok ? Phase.complete : Phase.failed, target, total, fact.status, code);
    }
  }
  const outcome = await completion;
  console.log(`${url}: ${total} bytes, status ${outcome.status}`);
  if (!ended) publish(Phase.failed, target, total, outcome.status, code);
}

async function main(): Promise<void> {
  for await (const request of io.channel("request").observe()) await fetchOnce(request[0] ?? 0);
}
main();
