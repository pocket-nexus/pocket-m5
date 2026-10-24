# pocket-m5

PocketJS firmware for M5Stack boards. Apps are TypeScript/JSX packages that
run in a QuickJS guest on the PocketJS runtime; the firmware embeds a home
launcher, installs `.pocket` packages from the microSD card and switches
between them on the device.

| Board | SoC | Display |
| --- | --- | --- |
| [Tab5](docs/TAB5.md) | ESP32-P4 | 720×1280 MIPI-DSI (ILI9881C, ST7123, ST7121 variants), capacitive touch |

The Tab5 has two firmware programs:

| Program | Pictures | Apps and switching | Status |
| --- | --- | --- | --- |
| [`programs/tab5`](programs/tab5/README.md) | 360×640 portrait at density 2, double-buffered flip | embedded home; packages from the SD card inbox; swipe up for home | builds; device validation open |
| [`programs/m5stack-tab5`](programs/m5stack-tab5/README.md) | Hero 640×360 landscape, Clear 360×640 portrait, Pocket Home 640×360 at 2x | pocket-shell's Pocket Home; installs over the Pocket Link from a Mac; home ball; Wi-Fi, camera, media, keyboard | imported from pocketjs#616, measured on the device there |

## Requirements

- ESP-IDF `>=6.0,<6.3` with the `esp32p4` target
- Bun, version in `.bun-version`
- Rust through rustup, with `riscv32imafc-unknown-none-elf` installed for the
  toolchain pinned in `third_party/pocketjs/rust-toolchain.toml`

## Quick start

```sh
git clone --recurse-submodules <repository-url> pocket-m5
cd pocket-m5
bun install                         # JavaScript dependencies and git hooks
rustup target add riscv32imafc-unknown-none-elf --toolchain 1.98.1

. $IDF_PATH/export.sh
cd programs/tab5
idf.py set-target esp32p4
idf.py build
idf.py -p <port> flash monitor
```

`programs/m5stack-tab5` builds the same way from its own directory; its
README lists the `POCKETJS_TAB5_APP` variants and the Pocket Home package it
embeds.

Build an app for `programs/tab5` and stage it for installation:

```sh
bun run app build apps/hello/pocket.json
bun run app install apps/hello/pocket.json --sd /Volumes/<card>
```

The device installs packages from `/pocket/inbox/` at boot and lists them on
the home screen. Swipe up from the bottom edge to return home. See
[docs/APPS.md](docs/APPS.md).

## Documentation

- [Repository structure](docs/STRUCTURE.md)
- [M5Stack Tab5](docs/TAB5.md): panel variants, pins, expanders, presentation
- [Apps](docs/APPS.md): build, install, switch, errors
- [Engineering conventions](AGENTS.md): commits, formatting, dependencies

## PocketJS version

`third_party/pocketjs` is pinned to the pocketjs branch `feat/tab5-platform`:
pocketjs `main` (`a9f186e2`) plus the platform, engine and framework changes
of pocket-nexus/pocketjs#616 that `programs/m5stack-tab5` needs. The submodule
returns to `main` once those changes merge there. The driver contracts and
directory layout come from pocket-nexus/pocketjs#603.

## License

MIT, see [LICENSE](LICENSE). Files derived from Espressif's esp-bsp keep
their Apache-2.0 headers.
