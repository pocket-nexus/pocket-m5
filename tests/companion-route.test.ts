// tests/companion-route.test.ts — the Tab5's route for the worker's requests
// to companion.pocket (programs/m5stack-tab5/components/pocket_system/src/
// companion.c): built with pocketjs's Pocket Link client under ASan and UBSan
// by tests/companion-route.c, against pocketjs's companion in this process.
// The companion answers companion.pocket itself, for the app the console names;
// with no Mac paired, or the link down, the request fails with a message the
// app can show.

import { afterAll, beforeAll, describe, expect, test } from "bun:test";
import { mkdtempSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { POCKET_BINDING_GENERATION } from "../third_party/pocketjs/contracts/spec/binding-generation.ts";
import { POCKET_SECTION, encodeIdentity, encodePocketPackage } from "../third_party/pocketjs/contracts/spec/pocket-package.ts";
import { Companion } from "../third_party/pocketjs/tools/companion/server.ts";
import { CompanionStore } from "../third_party/pocketjs/tools/companion/store.ts";

const ROOT = join(import.meta.dir, "..");
const SHARED = join(ROOT, "third_party/pocketjs/engine/host-services");
const SYSTEM = join(ROOT, "programs/m5stack-tab5/components/pocket_system/src");

/** An app the companion offers to the Tab5, declaring `services`. */
function makePackage(id: string, services: string[]): Uint8Array {
  return encodePocketPackage({
    manifest: new TextEncoder().encode(JSON.stringify({ pocket: 2, id, title: "Hacker News", version: "1.0.0", applet: { version: 1, services } })),
    variants: [
      {
        target: "tab5-home",
        hostAbi: POCKET_BINDING_GENERATION,
        profileHash: `sha256:${"11".repeat(32)}`,
        sections: [
          { kind: POCKET_SECTION.identity, bytes: encodeIdentity({ output: "hn", id, title: "Hacker News" }) },
          { kind: POCKET_SECTION.js, bytes: new TextEncoder().encode("globalThis.x=1;\0") },
        ],
      },
    ],
  });
}

describe("companion.pocket over the Pocket Link", () => {
  let scratch = "";
  let binary = "";
  let companion: Companion;
  let web: ReturnType<typeof Bun.serve>;

  beforeAll(async () => {
    scratch = mkdtempSync(join(tmpdir(), "pocket-m5-companion-"));
    binary = join(scratch, "companion-route");
    const compile = Bun.spawnSync([
      process.env.CC ?? "cc", "-std=gnu11", "-DPOCKETJS_WORKER_HTTP", "-O1", "-Wall", "-Wextra",
      "-fsanitize=address,undefined",
      `-I${SYSTEM}`, `-I${SHARED}`,
      `-I${join(ROOT, "third_party/pocketjs/engine/worker/include")}`,
      `-I${join(ROOT, "platforms/esp-idf/pocket_http_esp/include")}`,
      join(ROOT, "tests/companion-route.c"), join(SYSTEM, "companion.c"),
      ...["link.c", "link_http.c", "link_proto.c", "launcher.c", "jscan.c", "sha256.c"].map((file) => join(SHARED, file)),
      "-o", binary,
    ]);
    expect(compile.exitCode, compile.stderr.toString()).toBe(0);
    const offer = join(scratch, "offer");
    Bun.spawnSync(["mkdir", "-p", offer]);
    writeFileSync(join(offer, "hn.pocket"), makePackage("dev.pocket-nexus.hn", ["x"]));
    companion = new Companion({
      store: new CompanionStore(join(scratch, "store")),
      name: "Test Mac",
      port: 0,
      host: "127.0.0.1",
      appsDirs: [offer],
      allowPrivate: true,
      beacon: false,
      pairHosts: ["127.0.0.1"],
      log: () => {},
    });
    await companion.start();
    // A server that never answers, so a request is still open when the link closes.
    web = Bun.serve({ hostname: "127.0.0.1", port: 0, fetch: () => new Promise<Response>(() => {}) });
  });

  afterAll(async () => {
    web?.stop(true);
    await companion?.stop();
    rmSync(scratch, { recursive: true, force: true });
  });

  test("routes to the paired companion, and says why when there is none", async () => {
    const { uri } = companion.openPairing();
    const slow = `http://127.0.0.1:${web.port}/slow`;
    const run = Bun.spawn([binary, join(scratch, "console"), uri, slow], { stdin: "pipe", stdout: "pipe", stderr: "pipe" });
    const lines: string[] = [];
    const reader = run.stdout.getReader();
    let pending = "";
    for (;;) {
      const { done, value } = await reader.read();
      if (done) break;
      pending += new TextDecoder().decode(value);
      let newline: number;
      while ((newline = pending.indexOf("\n")) >= 0) {
        const line = pending.slice(0, newline);
        pending = pending.slice(newline + 1);
        lines.push(line);
        if (line === "wait stop") {
          await companion.stop();
          run.stdin.write("stopped\n");
          run.stdin.flush();
        }
      }
    }
    const exitCode = await run.exited;
    const stderr = await new Response(run.stderr).text();
    expect(exitCode, `${stderr}\n${lines.join("\n")}`).toBe(0);

    expect(lines).toContain("start https://companion.pocket/x/status error unavailable: no Mac is paired");
    expect(lines).toContain("status linked");
    // Answered by the companion: an unknown service, a service the running
    // app declares, and the same service refused to the launcher, which
    // declares none: each request names the running app.
    const fetches = lines.filter((line) => line.startsWith("fetch "));
    expect(fetches).toHaveLength(3);
    expect(fetches[0]).toMatch(/^fetch https:\/\/companion\.pocket\/nothing\/here 404 0 \{"error":"no service named nothing"\}$/);
    expect(fetches[1]).toBe('fetch https://companion.pocket/x/status 200 0 {"signedIn":false}');
    expect(fetches[2]).toMatch(/^fetch https:\/\/companion\.pocket\/x\/status 403 0 .*dev\.pocket-nexus\.shell does not declare the x service/);
    // The link closed under an open request, then took none.
    expect(lines).toContain(`end ${slow} -3 unavailable: the Pocket Link closed`);
    expect(lines).toContain("status not linked");
    expect(lines).toContain("start https://companion.pocket/x/status error unavailable: the paired Mac is not connected");
  }, 60_000);
});
