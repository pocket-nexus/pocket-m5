# pocket-m5

PocketJS firmware for M5Stack boards. Apps are TypeScript/JSX packages that
run in a QuickJS guest on the PocketJS runtime; the firmware embeds a home
launcher, installs `.pocket` packages from the microSD card and switches
between them on the device.

| Board | SoC | Display | Status |
| --- | --- | --- | --- |
| [Tab5](docs/TAB5.md) | ESP32-P4 | 720×1280 MIPI-DSI (ILI9881C, ST7123, ST7121 variants), capacitive touch | firmware builds; device validation open |

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

Build an app and stage it for installation:

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

`third_party/pocketjs` tracks pocketjs `main` and is pinned to a commit
(currently `a9f186e2`). The driver contracts and directory layout this
repository uses come from pocket-nexus/pocketjs#603.

## License

MIT, see [LICENSE](LICENSE). Files derived from Espressif's esp-bsp keep
their Apache-2.0 headers.
