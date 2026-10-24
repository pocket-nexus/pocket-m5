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
