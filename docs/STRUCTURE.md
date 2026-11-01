# Repository structure

This repository builds PocketJS firmware for M5Stack boards. It consumes
PocketJS as the `third_party/pocketjs` submodule and follows the directory
split of pocketjs `main` (pocket-nexus/pocketjs#603): **platforms** hold SDK
and board drivers, and **programs** compose drivers into firmware with a
runtime config and the **profile**, the software contract apps compile
against. The launcher, Pocket Home, is `apps/pocket-home`.

```text
pocket-m5/
├─ third_party/
│  └─ pocketjs/                 PocketJS checkout pinned to a commit (submodule)
├─ platforms/esp-idf/           ESP-IDF components owned by this repository
│  ├─ pocket_worker_esp/        FreeRTOS owner of the PocketJS worker core
│  ├─ pocket_http_esp/          worker http node over esp_http_client
│  └─ pocket_io_esp/            worker i2c and uart nodes
├─ programs/
│  └─ m5stack-tab5/             IDF project: main, runtime config, profile, sdkconfig, partitions
│     ├─ components/            pocket_system, pocket_camera, pocket_media, pocket_audio,
│     │                         pocket_device (this program only)
│     └─ profile.ts             tab5-home: 640x360 auxiliary surface, presented at 2x
├─ apps/
│  ├─ pocket-home/              the launcher, from pocket-shell's shells/3ds/home
│  ├─ worker-http/              worker HTTP check for programs/m5stack-tab5
│  └─ tab5-demo/                the Tab5 factory launcher (M5Tab5-UserDemo) on PocketJS
├─ tests/                       host tests: HTTP record, I2C descriptors, sensor drivers,
│                               Pocket Home's clock
├─ tools/                       pocket CLI wrapper, app packaging, formatter, host tests,
│                               tab5-demo artwork conversion, Pocket Home's icons
└─ docs/                        repository and worker reference
```

## Ownership

**PocketJS adapters are board-independent.** `pocket_worker_esp`,
`pocket_http_esp` and `pocket_io_esp` implement the worker host and its
`http`, `i2c` and `uart` nodes against ESP-IDF APIs. They take their configuration from the program, not from a
board. A board support component for one board would go in
`platforms/esp-idf/m5_bsp_<board>/`.

**The program composes and configures.** `programs/m5stack-tab5/main/main.c`
brings up the board through Espressif's `m5stack_tab5_noglib` BSP, creates the
display, input and renderers, starts the launcher services and boots one
runtime per app. The program owns its profile (`profile.ts`);
`home.runtime-config.ts` imports it. The build exports `pocket_profile.h`,
whose `pocket_profile.facts` record the runtime admits packages against, and
compiles Pocket Home (`apps/pocket-home`) against the same profile.

**Components one program uses stay with that program.**
`programs/m5stack-tab5/components/` holds the launcher services, camera,
media, audio (`audio.pcm`, `audio.capture`) and device (`system.device`)
components of that firmware. It builds on board-independent drivers
from the submodule (`pocket_display_esp_dpi`, `pocket_render_esp_ppa` with
its two-lane factory, `pocket_runner`); it reads the microSD card over SDSPI
because the ESP32-C6 radio takes the SDMMC controller.

**The profile is the app contract.** Every installed package carries a variant
whose profile hash equals the program's. Changing any hashed profile field
requires rebuilding every app, Pocket Home included.

## Upstream components

The program adds these submodule components through `EXTRA_COMPONENT_DIRS`:

| Component | Use |
| --- | --- |
| `pocket_runtime` | one Rust archive (runtime, software renderer, PPA factory), host hooks, memory package source, CMake package helpers |
| `pocket_guest_quickjs` | QuickJS realm with the generated guest bindings |
| `pocket_runner` | the task that ticks a runtime |
| `pocket_render_esp_ppa` | ESP32-P4 PPA renderer with software fallback |
| `pocket_display_esp_dpi` | strips into one DPI framebuffer, with rotation, scaling and retained moves |

The Rust archive builds from `third_party/pocketjs/programs/esp-idf/archive`
with the submodule's toolchain pin (`rust-toolchain.toml`) unless a prepared
archive is passed with `POCKET_RUNTIME_ARCHIVE`.

## JavaScript workspace

The root `package.json` declares `third_party/pocketjs` as a Bun workspace, so
`@pocketjs/framework` resolves to the submodule and its dependencies install
once at the root. `tools/pocket` runs the submodule's CLI; the CMake helpers
call it through `POCKETJS_CLI`. The apps under `apps/`, Pocket Home
included, compile with that CLI, and their imports resolve to the root's
`node_modules`.
