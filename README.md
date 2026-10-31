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
[pocket-shell](https://github.com/pocket-nexus/pocket-shell), pinned as the
`third_party/pocket-shell` submodule; the firmware build compiles Pocket Home
from it. App workers get an `http` node (`pocket_http_esp`); see
[Worker domain](docs/WORKER.md).

## Requirements

- ESP-IDF 6.2.x with the `esp32p4` target (`pocket_worker_esp` needs 6.2;
  the other components declare `>=6.0,<6.3`)
- Bun, version in `.bun-version`
- Rust through rustup, with `riscv32imafc-unknown-none-elf` installed for the
  toolchain pinned in `third_party/pocketjs/rust-toolchain.toml`

## Quick start

```sh
git clone <repository-url> pocket-m5
cd pocket-m5
git submodule update --init third_party/pocketjs third_party/pocket-shell
bun install                         # JavaScript dependencies and git hooks
rustup target add riscv32imafc-unknown-none-elf --toolchain 1.98.1

. $IDF_PATH/export.sh
cd programs/m5stack-tab5
idf.py set-target esp32p4
idf.py build
idf.py -p <port> flash monitor
```

The build does not use pocket-shell's own `vendor/pocketjs` submodule, so the
command above initializes the two submodules without `--recursive`.

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

## Pinned dependencies

`third_party/pocketjs` is pinned to the `tab5-device-66e39a0f` branch: five
commits on pocketjs `main` at `66e39a0f` that add the `system.device` and
`audio.capture` modules, the Wi-Fi access point ops, the package feature
bits the ESP-IDF archive reports for them and the I2C probe opcode, and
re-arm the worker scheduler's host timer after a timer fact. The same
commits, rebased onto pocketjs's host-facts admission
(pocket-nexus/pocketjs#631), are under review as pocket-nexus/pocketjs#635;
this repository and pocket-shell still use the profile-hash admission that
#631 replaced, so the pin stays on the pre-rebase branch. `main` carries the
platform, engine and framework changes of pocket-nexus/pocketjs#616 that
`programs/m5stack-tab5` needs, merged through pocket-nexus/pocketjs#617, and
the worker runtime in `engine/worker` that `pocket_worker_esp` builds,
merged through pocket-nexus/pocketjs#623. The driver contracts and directory
layout come from pocket-nexus/pocketjs#603.

`third_party/pocket-shell` is pinned to a commit on pocket-shell `main`.
The firmware compiles `shells/3ds/home` from it with the pinned pocketjs.

## License

MIT, see [LICENSE](LICENSE). pocket-shell is GPL-3.0 (Pocket Home is
`GPL-3.0-or-later`); a firmware image that embeds Pocket Home contains that
code.
