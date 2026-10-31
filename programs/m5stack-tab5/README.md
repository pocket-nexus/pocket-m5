# M5Stack Tab5

Firmware for the M5Stack Tab5: an ESP32-P4 with a 720x1280 MIPI-DSI panel,
a GT911 touch controller and an ESP32-C6 Wi-Fi coprocessor. The program
composes the runtime with `pocket_guest_quickjs`, two `pocket_render_esp_ppa`
renderers split across the cores (`pocketjs_esp32p4_ppa_lanes`), the
`pocket_display_esp_dpi` output and `pocket_runner`. Those components, the
IDF archive and `engine/host-services` come from `third_party/pocketjs`; the
program, its components and its profile live here.

The program was imported from pocket-nexus/pocketjs#616 (`df0d434a`), whose
platform and engine changes reached pocketjs `main` through
pocket-nexus/pocketjs#617.

| Profile | Launcher | Picture |
| --- | --- | --- |
| `tab5-home` (`profile.ts`, through `home.runtime-config.ts`) | Pocket Home from `third_party/pocket-shell/shells/3ds/home` | the auxiliary surface, 640x360 at raster density 1, turned and presented at 2x |

The firmware is a launcher console: `components/pocket_system`
keeps a store of installed apps on the microSD card (SDSPI; the `storage`
flash partition without a card), the Pocket Link to a paired companion,
each app's `data.fs` tree, the Wi-Fi radio on the ESP32-C6 (esp_hosted) and
the wall clock from SNTP. The build compiles Pocket Home from the
pocket-shell submodule against `home.runtime-config.ts`, embeds it as the
launcher and boots one runtime per app; SELECT from the home control returns
to the launcher. `-DPOCKETJS_TAB5_HOME_PACKAGE=<package.pocket>` embeds a
package compiled against the same profile instead. The
primary surface of the profile is laid out into a PSRAM buffer that nothing
scans out. `pocket_idf_package_open` (the IDF archive) admits a stored package
for the profile before the runtime boots it. `components/pocket_camera` serves
`globalThis.camera` (the SC202CS on MIPI-CSI: a 320x240 preview at 15 fps,
QR decoding, JPEG stills on the card) and `components/pocket_media` serves
`globalThis.media` (the companion's stream on the ES8388 speaker) to a guest
whose plan declares those capabilities. The camera's capture task runs on
core 1 and opens the video device from a task on core 0: the CSI controller
takes a DW-GDMA channel interrupt, and the DPI panel holds the DW-GDMA source
on core 0 (a shared source routes to one core).

A UI realm has no network module. An app whose package carries a worker
domain (`domains.worker`, package section 8) gets its own worker runtime
(`pocket_worker_esp`) with one `http` node: `io.http` requests the Tab5 makes
itself over Wi-Fi with `pocket_http_esp` (HTTPS with the ESP certificate
bundle, mbedTLS buffers in PSRAM, certificate dates checked against the SNTP
clock). The worker starts before the realm evaluates, its channels mirror into
the realm once per frame (`before_tick`), and it stops before the realm and
the package bytes are released. Its QuickJS heap (384 KiB) and its 24 KiB
task stack are in PSRAM; it runs on core 1 at priority 3, below the render
lane, and the two HTTP request tasks at priority 2. An app whose worker fails
returns to the launcher with the failure shown. The
`tab5-home` profile lists `runtime.worker` and `net.http`, so packages built
against an earlier `tab5-home` must be rebuilt.
`pocket_worker_esp` requires ESP-IDF 6.2.x. `apps/worker-http` is the device
check: embedded with `POCKETJS_TAB5_HOME_PACKAGE` it joins a Wi-Fi network and
fetches a URL once connected and on each tap.

A picture smaller than the screen sits between two equal black margins. The
display moves retained pixels for content that only scrolled
(`POCKET_DISPLAY_RETAINED_MOVE`); the runtime's damage planner finds those
moves and repaints the rest. One AXI GDMA pair carries both the whole-row
strip copies and the retained moves; of the AXI GDMA's other two pairs the
card's SPI bus holds one and the SHA and AES accelerators take the other.
Each strip holds a multiple of the rows that span whole 128-byte cache lines
(`line_rows`: four 1184-byte rows at 592x1, one 1280-byte row at 640x1), and
a turned strip a multiple of 8 raster rows besides.

The launcher paints a 56-px translucent ball at the right edge of the
picture, halfway down, as two ARGB8888 faces (resting and pressed) in PSRAM
and hands the display the current face as its overlay
(`pocket_display_esp_dpi_set_overlay`), so the ball stays on the framebuffer
wherever the picture changes under it. A touch that lands on the ball never
reaches the guest, however far it travels. A tap (lifted within ten polls,
under 6 logical px of travel) sends one tick of SELECT to Pocket Home, and
from an app it summons the launcher instead (`pocketjs_system_summon`); a
finger that moves at once drags the ball anywhere inside the picture; a
finger that rests longer and then travels 24 px makes a gesture, logged by
its direction (`ball gesture: up`) where an effect attaches later.

The Tab5 keyboard (M5Stack A164, `tab5_keyboard.c`) answers at I2C address
0x6D on Ext.Port1 (SDA GPIO 0, SCL GPIO 1, the program's second I2C bus).
The input source declares the TEXT class with or without a keyboard, so the
Home profile's `input.text` holds and the firmware boots without one. The
program probes the bus for a keyboard at boot and every two seconds while
none answers, and lets one go after three polls it does not answer, so the
keyboard plugs in and out without a reboot. Each poll drains the keyboard's
character events into `pocket_text_event` records, a character as a commit
and a named key as a down and up pair named as `KeyboardEvent.key` names it
(`Enter`, `Backspace`, `ArrowLeft`). The keyboard keeps its own Sym and Aa
layers. Its
firmware 1 reports Ctrl in every event once the key was pressed and never
clears it, so the program reads no modifier from it; `Control+z` waits for
a firmware that releases the key.

The runner's task and the application task that boots the runtime each hold
a 192 KB stack in PSRAM (`task_stack_caps`), with the QuickJS stack limit at
144 KB: evaluating a deep Solid tree throws a RangeError before it reaches
the task's guard page.

```sh
git submodule update --init third_party/pocketjs third_party/pocket-shell
cd programs/m5stack-tab5
idf.py -p /dev/cu.usbmodem1201 build flash monitor
```

Builds run the Pocket CLI through `tools/pocket` (`POCKETJS_CLI`) and, with
`POCKETJS_RUST_FROM_SOURCE=ON` (the default here), need a Rust toolchain with
the `riscv32imafc-unknown-none-elf` target. Pocket Home compiles with the
pinned `third_party/pocketjs`, not with pocket-shell's own `vendor/pocketjs`,
which the build does not need; its typecheck inherits
`shells/3ds/tsconfig.json`, whose `bun-types` comes from the root
`package.json`. The image with Pocket Home is 4.1 MiB of the 8 MiB `factory`
partition. The log reports the frame rate every five seconds.

The applets install from a paired Mac's catalog. pocket-shell's
`bun run tab5` and `bun run offer` compile them against this program's
`home.runtime-config.ts` (`POCKET_M5_ROOT` names this checkout); the profile
hash depends on the profile's content, so a package built against an
identical `tab5-home` profile in another checkout installs here. A UI realm
has no network module: an app reaches the network through its worker's
`io.http`, and one that imports `@pocketjs/framework/net`, removed from
pocketjs, does not compile.
