// SPDX-License-Identifier: GPL-3.0-or-later
// bun tools/pocket-home-icons.ts [--only hn,mac] — re-render Pocket Home's
// icons in Blender and bake them into the textures the build reads
// (apps/pocket-home/art/icons/*.png).
//
// tools/pocket-home-icons/icons.py models and renders each icon at 512 px
// into .pocket-build/home-icons/render, and tools/pocket-home-icons/nexus.ts
// draws Pocket Nexus's (the PocketJS mark, with @napi-rs/canvas);
// tools/pocket-home-icons/bake.ts sharpens each downsample, adds the shared
// finish (mask, gloss, rim, shadow), writes the PNGs and a contact sheet in
// .pocket-build/validation/home-icons. Builds use the committed PNGs and
// never need Blender.

import { resolve } from "node:path";
import { $ } from "bun";

const ROOT = resolve(import.meta.dir, "..");
const TOOLS = resolve(import.meta.dir, "pocket-home-icons");
const BLENDER = process.env.BLENDER ?? "/Applications/Blender.app/Contents/MacOS/Blender";
const out = resolve(ROOT, ".pocket-build/home-icons/render");

// A delegated command's own message is the useful one: a failure exits with
// its code instead of a ShellError stack trace.
async function passThrough<T>(work: Promise<T>): Promise<T> {
  try {
    return await work;
  } catch (error) {
    const code = (error as { exitCode?: number }).exitCode;
    process.exit(typeof code === "number" && code !== 0 ? code : 1);
  }
}

await passThrough($`${BLENDER} -b --factory-startup --python ${resolve(TOOLS, "icons.py")} -- --out ${out} ${process.argv.slice(2)}`.cwd(ROOT));
await passThrough($`bun ${resolve(TOOLS, "nexus.ts")} ${out}`.cwd(ROOT));
await passThrough($`bun ${resolve(TOOLS, "bake.ts")} ${out}`.cwd(ROOT));
