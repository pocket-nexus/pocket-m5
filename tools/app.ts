// Compiles an app for the profile of programs/m5stack-tab5 (tab5-home).
//   bun run app build apps/worker-http/pocket.json [--runtime-config <path>]
// Packages land in .pocket-build/apps/<output>.pocket.
import { spawnSync } from "node:child_process";
import { existsSync, readFileSync } from "node:fs";
import { dirname, join, resolve } from "node:path";

const root = resolve(import.meta.dir, "..");
const [command, manifestArgument, ...rest] = process.argv.slice(2);
const option = (name: string) => {
  const index = rest.indexOf(name);
  return index >= 0 ? rest[index + 1] : undefined;
};
const usage = () => {
  console.error("usage: bun run app build <pocket.json> [--runtime-config <path>]");
  process.exit(2);
};
if (command !== "build" || !manifestArgument) usage();

const runtimeConfig = option("--runtime-config") ? resolve(option("--runtime-config")!)
  : join(root, "programs", "m5stack-tab5", "home.runtime-config.ts");
const frameworkRoot = process.env.POCKETJS_ROOT ?? join(root, "third_party", "pocketjs");
if (!existsSync(runtimeConfig)) {
  console.error(`no runtime config: ${runtimeConfig}`);
  process.exit(2);
}
const manifest = resolve(manifestArgument!);
const output: string = JSON.parse(readFileSync(manifest, "utf8")).app.output;
const buildRoot = join(root, ".pocket-build", "apps");
const pocket = join(buildRoot, `${output}.pocket`);
const compile = spawnSync(
  "bun",
  [join(frameworkRoot, "tools", "pocket.ts"), "compile",
    "--manifest", manifest, "--runtime-config", runtimeConfig,
    "--project-root", dirname(manifest), "--outdir", join(buildRoot, output),
    "--plan-dir", join(buildRoot, output, "plans"), "--output", pocket],
  { cwd: root, stdio: "inherit" },
);
if (compile.status !== 0) process.exit(compile.status ?? 1);
