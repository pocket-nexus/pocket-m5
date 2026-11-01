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
Pocket Link; a floating home ball; Wi-Fi with an optional access point,
SNTP clock, worker HTTP, I2C and RS485 nodes, camera, media, PCM audio and
microphones, board controls and keyboard. Pocket Home is
[`apps/pocket-home`](apps/pocket-home/README.md), taken from
[pocket-shell](https://github.com/pocket-nexus/pocket-shell) at `188c46c`;
the applets (Hacker News, YouTube, Notes and the others) stay in pocket-shell.
App workers get an `http` node (`pocket_http_esp`) and `i2c` and `uart` nodes
(`pocket_io_esp`); see [Worker domain](docs/WORKER.md).
[`apps/tab5-demo`](apps/tab5-demo/README.md) is the Tab5 factory firmware's
launcher (M5Tab5-UserDemo) on PocketJS.

## Requirements

- ESP-IDF 6.2.x with the `esp32p4` target (`pocket_worker_esp` needs 6.2;
  the other components declare `>=6.0,<6.3`). 6.2 has no release branch;
  CI builds with ESP-IDF `master` at the commit in `IDF_COMMIT` of
  `.github/workflows/firmware.yml`.
- Bun, version in `.bun-version`
- Rust through rustup, with `riscv32imafc-unknown-none-elf` installed for the
  toolchain pinned in `third_party/pocketjs/rust-toolchain.toml`

## Quick start

```sh
git clone <repository-url> pocket-m5
cd pocket-m5
git submodule update --init third_party/pocketjs
bun install                         # JavaScript dependencies and git hooks
rustup target add riscv32imafc-unknown-none-elf --toolchain 1.98.1

. $IDF_PATH/export.sh
cd programs/m5stack-tab5
idf.py set-target esp32p4
idf.py build
idf.py -p <port> flash monitor
```

The firmware embeds this repository's apps (`apps/tab5-demo`,
`apps/worker-http`) and installs them at boot, so Pocket Home lists them
without a companion; applets from a paired Mac's catalog (pocket-shell's
`bun run offer` with Pocket Companion running) install beside them. The home
ball returns to Pocket Home. `bun run app build <pocket.app.json>` compiles
an app of this repository with the firmware's host config into
`.pocket-build/apps/`.

## Firmware releases

`.github/workflows/firmware.yml` runs on pull requests, on pushes to `main`
and on `v*` tags:

- **Host checks**: `bun run format:check` and `tools/test-worker-io.sh`.
- **Tab5 firmware**: installs ESP-IDF at `IDF_COMMIT` (cached per commit) and
  the Rust toolchain of `third_party/pocketjs/rust-toolchain.toml`, runs
  `idf.py build` in `programs/m5stack-tab5`, and packages the build with
  `tools/firmware-dist.ts` into the `pocket-m5-tab5` workflow artifact.
- **Release**, on a `v*` tag only: publishes the packaged files as a GitHub
  release of that tag; a tag with a suffix (`v0.2.0-rc1`) is a prerelease.

The firmware version is `git describe` of the checkout, so a tagged build is
`pocket-m5-tab5-v0.1.0`. To publish one:

```sh
git tag -a v0.1.0 -m "pocket-m5 v0.1.0"
git push origin v0.1.0
```

A release holds:

| File | Use |
| --- | --- |
| `pocket-m5-tab5-<version>.bin` | bootloader, partition table and app merged from 0x0; `write-flash 0x0` installs it and erases `nvs` (saved Wi-Fi networks) |
| `pocket-m5-tab5-<version>.zip` | the three images and `flash_args`; `write-flash @flash_args` updates and keeps `nvs` |
| `SHA256SUMS` | SHA-256 of both |

The release notes (`RELEASE.md` from `tools/firmware-dist.ts`) give the
pocketjs and ESP-IDF versions and the `esptool` commands. Neither image
writes the `storage` partition or the microSD card, so installed apps and
their data stay. `bun tools/firmware-dist.ts` packages a local build the same
way (into `.pocket-build/dist/`) with the ESP-IDF environment exported.

## Documentation

- [Repository structure](docs/STRUCTURE.md)
- [M5Stack Tab5 firmware](programs/m5stack-tab5/README.md): host contract,
  presentation, launcher services, home ball, keyboard
- [Worker domain](docs/WORKER.md): worker runtime, HTTP node and validation
- [Engineering conventions](AGENTS.md): commits, formatting, dependencies

## Pinned dependencies

`third_party/pocketjs` is pinned to a commit on pocketjs `main`. It
includes the host-facts admission, host contracts and app contracts of
pocket-nexus/pocketjs#631, and pocket-nexus/pocketjs#635, which adds the
`system.device` and `audio.capture` modules, the Wi-Fi access point ops, the
package feature bits the ESP-IDF archive reports for them and the I2C probe
opcode, and re-arms the worker scheduler's host timer after a timer fact.
`main` carries the platform, engine and framework changes of
pocket-nexus/pocketjs#616 that `programs/m5stack-tab5` needs, merged through
pocket-nexus/pocketjs#617, and the worker runtime in `engine/worker` that
`pocket_worker_esp` builds, merged through pocket-nexus/pocketjs#623. The
driver contracts and directory layout come from pocket-nexus/pocketjs#603.

## License

MIT, see [LICENSE](LICENSE). The files taken from pocket-shell
(`apps/pocket-home`, `tools/pocket-home-icons.ts`, `tools/pocket-home-icons/`,
`tests/pocket-home-clock.test.ts`) keep their `GPL-3.0-or-later` lines; a
firmware image that embeds Pocket Home contains that code.
