// Compiles an app for a board program's profile, optionally copying the
// package to an SD card's install inbox.
//   bun run app build apps/hello/pocket.json [--board tab5]
//   bun run app install apps/hello/pocket.json --sd /Volumes/SD [--board tab5]
// Packages land in .pocket-build/apps/<board>/<output>.pocket.
import { spawnSync } from "node:child_process";
import { copyFileSync, existsSync, mkdirSync, readFileSync } from "node:fs";
import { dirname, join, resolve } from "node:path";

const root = resolve(import.meta.dir, "..");
const [command, manifestArgument, ...rest] = process.argv.slice(2);
const option = (name: string) => {
  const index = rest.indexOf(name);
  return index >= 0 ? rest[index + 1] : undefined;
};
const usage = () => {
  console.error("usage: bun run app build|install <pocket.json> [--board <board>] [--sd <mount>]");
  process.exit(2);
};
if ((command !== "build" && command !== "install") || !manifestArgument) usage();

const board = option("--board") ?? "tab5";
const runtimeConfig = join(root, "programs", board, "pocket.runtime-config.ts");
if (!existsSync(runtimeConfig)) {
  console.error(`no runtime config for board ${board}: ${runtimeConfig}`);
  process.exit(2);
}
const manifest = resolve(manifestArgument!);
const output: string = JSON.parse(readFileSync(manifest, "utf8")).app.output;
const buildRoot = join(root, ".pocket-build", "apps", board);
const pocket = join(buildRoot, `${output}.pocket`);
const compile = spawnSync(
  "bun",
  [join(root, "third_party", "pocketjs", "tools", "pocket.ts"), "compile",
    "--manifest", manifest, "--runtime-config", runtimeConfig,
    "--project-root", dirname(manifest), "--outdir", join(buildRoot, output),
    "--plan-dir", join(buildRoot, output, "plans"), "--output", pocket],
  { cwd: root, stdio: "inherit" },
);
if (compile.status !== 0) process.exit(compile.status ?? 1);

if (command === "install") {
  const sd = option("--sd");
  if (!sd) usage();
  const inbox = join(sd!, "pocket", "inbox");
  mkdirSync(inbox, { recursive: true });
  copyFileSync(pocket, join(inbox, `${output}.pocket`));
  console.log(`copied ${pocket} -> ${inbox}; the device installs it at boot`);
}
