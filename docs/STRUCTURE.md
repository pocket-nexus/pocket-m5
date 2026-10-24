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
│  ├─ m5_bsp_tab5/              Tab5 board: I2C, expanders, rails, panel, touch, SD
│  ├─ pocket_display_esp_dpi/   display output over a double-buffered DPI panel
│  ├─ pocket_input_esp_touch/   multi-contact input source over esp_lcd_touch
│  └─ pocket_app_host/          package catalog, install inbox, guest switching
├─ programs/
│  └─ tab5/                     IDF project: main.c, runtime config, profile, sdkconfig, partitions
│     └─ profile.ts             m5stack-tab5: 720x1280 physical, 360x640 logical, density 2
├─ apps/
│  ├─ home/                     launcher embedded in the firmware
│  └─ hello/                    installable sample
├─ tools/                       pocket CLI wrapper, app packaging, formatter
└─ docs/                        board and app-host reference
```

## Ownership

**Board support is per board and has no PocketJS dependency.** `m5_bsp_tab5`
brings up the hardware and returns esp_lcd and esp_lcd_touch handles. A second
board adds `platforms/esp-idf/m5_bsp_<board>/`.

**PocketJS adapters are board-independent.** `pocket_display_esp_dpi`,
`pocket_input_esp_touch` and `pocket_app_host` implement the driver and host
contracts from `third_party/pocketjs/contracts/generated` against ESP-IDF
APIs. They take handles from the program, not from a BSP.

**The program composes and configures.** `programs/tab5/main/main.c` creates
the board devices, wraps them in PocketJS drivers, selects the renderer and
starts the host. The program owns its profile (`programs/tab5/profile.ts`);
`pocket.runtime-config.ts` imports it. The build exports `pocket_profile.h`,
whose `pocket_profile.facts` record the runtime admits packages against, and
compiles `apps/home` against the same profile. A profile shared by several
programs would live in `profiles/`.

**The profile is the app contract.** Every installed package carries a variant
whose profile hash equals the program's. Changing any hashed profile field
requires rebuilding every app.

## Upstream components

The program adds these submodule components through `EXTRA_COMPONENT_DIRS`:

| Component | Use |
| --- | --- |
| `pocket_runtime` | one Rust archive (runtime, software renderer, PPA factory), host hooks, memory package source, CMake package helpers |
| `pocket_guest_quickjs` | QuickJS realm with the generated guest bindings |
| `pocket_render_esp_ppa` | ESP32-P4 PPA renderer with software fallback |

The Rust archive builds from `third_party/pocketjs/programs/esp-idf/archive`
with the submodule's toolchain pin (`rust-toolchain.toml`) unless a prepared
archive is passed with `POCKET_RUNTIME_ARCHIVE`.

## JavaScript workspace

The root `package.json` declares `third_party/pocketjs` as a Bun workspace, so
`@pocketjs/framework` resolves to the submodule and its dependencies install
once at the root. `tools/pocket` runs the submodule's CLI; the CMake helpers
call it through `POCKETJS_CLI`.
