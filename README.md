# pocket-m5

PocketJS firmware for M5Stack boards. Apps are TypeScript/JSX packages that
run in a QuickJS guest on the PocketJS runtime; the firmware embeds Pocket
Home as its launcher, installs packages from a paired Mac over the Pocket
Link and switches between them on the device.

| Board | SoC | Display |
| --- | --- | --- |
| M5Stack Tab5 | ESP32-P4 | 720×1280 MIPI-DSI, capacitive touch |

[`programs/m5stack-tab5`](programs/m5stack-tab5/README.md) is the Tab5
firmware: Pocket Home 640×360 at 2x, turned onto the panel; installs over the
Pocket Link; a floating home ball; Wi-Fi, SNTP clock, worker HTTP, camera,
media and keyboard. Pocket Home and the applets come from
[pocket-shell](https://github.com/pocket-nexus/pocket-shell); the firmware
build embeds a Pocket Home package compiled there. App workers get an `http`
node (`pocket_http_esp`); see [Worker domain](docs/WORKER.md).

## Requirements

- ESP-IDF 6.2.x with the `esp32p4` target (`pocket_worker_esp` needs 6.2;
  the other components declare `>=6.0,<6.3`)
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
cd programs/m5stack-tab5
idf.py set-target esp32p4
idf.py -DPOCKETJS_TAB5_HOME_PACKAGE=<home-main.pocket> build
idf.py -p <port> flash monitor
```

`<home-main.pocket>` is Pocket Home compiled in pocket-shell against
`programs/m5stack-tab5/home.runtime-config.ts`.

Pair the Tab5 with a Mac in Pocket Home's Settings; the applets install from
the Mac's catalog (pocket-shell's `bun run offer` with Pocket Companion
running). The home ball returns to Pocket Home. `bun run app build
<pocket.json>` compiles an app of this repository for the firmware's profile
into `.pocket-build/apps/`.

## Documentation

- [Repository structure](docs/STRUCTURE.md)
- [M5Stack Tab5 firmware](programs/m5stack-tab5/README.md): profile,
  presentation, launcher services, home ball, keyboard
- [Worker domain](docs/WORKER.md): worker runtime, HTTP node and validation
- [Engineering conventions](AGENTS.md): commits, formatting, dependencies

## PocketJS version

`third_party/pocketjs` is pinned to a commit on pocketjs `main` and tracks
that branch. `main` carries the platform, engine and framework changes of
pocket-nexus/pocketjs#616 that `programs/m5stack-tab5` needs, merged through
pocket-nexus/pocketjs#617, and the worker runtime in `engine/worker` that
`pocket_worker_esp` builds, merged through pocket-nexus/pocketjs#623. The
driver contracts and directory layout come from pocket-nexus/pocketjs#603.

## License

MIT, see [LICENSE](LICENSE).
