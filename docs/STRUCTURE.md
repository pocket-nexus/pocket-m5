# Repository structure

This repository builds PocketJS firmware for M5Stack boards. It consumes
PocketJS as the `third_party/pocketjs` submodule and follows the directory
split of pocketjs `main` (pocket-nexus/pocketjs#603): **platforms** hold SDK
and board drivers, and **programs** compose drivers into firmware with a
runtime config and the **profile**, the software contract apps compile
against.

```text
pocket-m5/
├─ third_party/pocketjs/        PocketJS checkout pinned to a commit (submodule)
├─ platforms/esp-idf/           ESP-IDF components owned by this repository
│  ├─ pocket_worker_esp/        FreeRTOS owner of the PocketJS worker core
│  └─ pocket_http_esp/          worker http node over esp_http_client
├─ programs/
│  └─ m5stack-tab5/             Hero, Clear and Pocket Home firmware (from pocketjs#616)
│     ├─ components/            pocket_system, pocket_camera, pocket_media (this program only)
│     ├─ profiles/              tab5-hero, tab5-clear, tab5-home
│     └─ apps/                  Tab5 manifests of pocketjs apps/hero and apps/clear
├─ apps/
│  └─ worker-http/              worker HTTP check for programs/m5stack-tab5
├─ tests/                       host test of the HTTP record parser
├─ tools/                       pocket CLI wrapper, app packaging, formatter, host tests
└─ docs/                        repository and worker reference
```

## Ownership

**PocketJS adapters are board-independent.** `pocket_worker_esp` and
`pocket_http_esp` implement the worker host and its `http` node against
ESP-IDF APIs. They take their configuration from the program, not from a
board. A board support component for one board would go in
`platforms/esp-idf/m5_bsp_<board>/`.

**The program composes and configures.** `programs/m5stack-tab5/main/main.c`
brings up the board through Espressif's `m5stack_tab5_noglib` BSP, creates the
display, input and renderers and boots the embedded app; the home variant
starts the launcher services and boots one runtime per app. The program owns
its profiles (`profiles/`); each `<app>.runtime-config.ts` imports one. The
build exports `pocket_profile.h`, whose `pocket_profile.facts` record the
runtime admits packages against.

**Components one program uses stay with that program.**
`programs/m5stack-tab5/components/` holds the launcher services, camera and
media components of that firmware. It builds on board-independent drivers
from the submodule (`pocket_display_esp_dpi`, `pocket_render_esp_ppa` with
its two-lane factory, `pocket_runner`); it reads the microSD card over SDSPI
because the ESP32-C6 radio takes the SDMMC controller.

**The profile is the app contract.** Every installed package carries a variant
whose profile hash equals the program's. Changing any hashed profile field
requires rebuilding every app.

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
call it through `POCKETJS_CLI`.
