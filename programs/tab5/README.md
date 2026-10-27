# Tab5 firmware

ESP-IDF project for the M5Stack Tab5. `main/main.c` brings up the board with
`m5_bsp_tab5`, creates the double-buffered DPI display, the multi-contact
touch source and (with `CONFIG_TAB5_RENDERER_PPA`) the PPA renderer, mounts
the microSD card at `/sdcard`, and starts `pocket_app_host` with the embedded
home package.

## Build and flash

```sh
. $IDF_PATH/export.sh            # ESP-IDF >=6.0,<6.3
cd programs/tab5
idf.py set-target esp32p4        # first build only
idf.py build
idf.py -p <port> flash monitor
```

The build runs three tools besides the IDF toolchain:

- **Bun** (`.bun-version`) runs `tools/pocket` to export `pocket_profile.h`
  from `pocket.runtime-config.ts` and to compile `apps/home`.
- **Cargo** builds `libpocket_idf_runtime.a` for `riscv32imafc-unknown-none-elf`
  from the submodule. Install the target for the submodule's toolchain:
  `rustup target add riscv32imafc-unknown-none-elf --toolchain 1.98.1`.
- **Python** from the IDF environment embeds the package.

To link a prepared archive instead of building Rust:
`idf.py -DPOCKETJS_RUST_FROM_SOURCE=OFF -DPOCKET_RUNTIME_ARCHIVE=<path> build`.

## Configuration

`idf.py menuconfig` → *PocketJS Tab5 firmware*: renderer, backlight level,
runtime and QuickJS heap limits per app. *M5Stack Tab5 board*: I2C port,
backlight LEDC channel/timer, charging policy.

| Partition | Size |
| --- | --- |
| `factory` | 8 MiB (the image with home is 2.7 MiB) |
| unallocated | 7.9 MiB |

Memory per running app: two frame buffers (3.5 MiB PSRAM), the package
(PSRAM), runtime heap ≤ 12 MiB and QuickJS heap ≤ 8 MiB by default. The host
task stack is 128 KiB of internal RAM with a 64 KiB QuickJS stack limit.

## Optional worker host

Set `POCKETJS_ENABLE_WORKER=ON` when configuring CMake and enable
`CONFIG_POCKET_APP_HOST_WORKER` to compile the isolated worker runtime, UART
and I2C nodes, and frame-latched UI channels. Worker applications use
`worker.runtime-config.ts` and its separate profile identity. See
[worker build commands and limits](../../docs/WORKER.md).

`POCKETJS_ROOT` selects an explicit source checkout for local integration
builds. The default remains the pinned submodule.
`TAB5_HOME_MANIFEST` can replace the embedded home app for device validation;
it defaults to `apps/home/pocket.json`. For example, use the absolute path to
`apps/worker-io/pocket.json` with the worker profile to boot the sensor example
without installing it on an SD card.
# Device UI validation

`CONFIG_TAB5_UI_VALIDATION` is an opt-in program hook, disabled by default.
Enable it under **PocketJS Tab5 firmware** in `idf.py menuconfig`, then build
and flash the matching ELF and binary. The hook allocates **1,843,200 bytes of
PSRAM** for a frozen 720×1280 RGB565 image. It wraps this program's display and
input drivers; the runtime ABI and app code do not change.

With the Tab5 connected over USB, the IDF OpenOCD and RISC-V `nm` tools available,
and no other debugger attached, run from the repository root:

```sh
python3 tools/tab5-ui-validation.py --elf programs/tab5/build/pocket_m5_tab5.elf status
python3 tools/tab5-ui-validation.py --elf programs/tab5/build/pocket_m5_tab5.elf capture .pocket-build/validation/worker/ui/motion.png
python3 tools/tab5-ui-validation.py --elf programs/tab5/build/pocket_m5_tab5.elf tap 180 120
```

`capture` waits for the next display flush, copies the rendered frame before
the UI owner can reuse it, writes the CPU cache to PSRAM, and publishes the
copy through an internal-RAM mailbox. OpenOCD reads that copy while both CPU
cores run. The Python tool writes a PNG using the standard library. A static
screen with no subsequent rendered frame can time out; trigger a UI update and
retry. A capture adds one full-frame copy to that UI frame and is not a frame
timing benchmark.

`tap` takes **logical coordinates** (360×640 for the worker profile), holds one
contact for three input frames, then submits an empty frame to release it.
`--frames` accepts 1 through 30. The contact follows the runtime input path,
including the app's handlers and worker channel commands. Physical touch
samples are replaced during those four frames; avoid touching the panel while
injecting a tap. The tool checks that both cores remain running and never
halts, resets, or resumes them. This avoids charging debugger pauses to worker
turn deadlines.

The matching ELF is required because the tool resolves the mailbox symbol
from it. `--openocd`, `--scripts`, `--nm`, and `--port` can override tool paths
and the local TCL port. OpenOCD binds to localhost and exits after each action.
Keep PNGs, logs, receipts, and validation firmware under ignored
`.pocket-build/validation/`. Disable the hook, rebuild the same app and driver
sources, and flash that binary for continued device use.
