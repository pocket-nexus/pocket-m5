# M5Stack Tab5

Firmware for the M5Stack Tab5: an ESP32-P4 with a 720x1280 MIPI-DSI panel,
a GT911 touch controller and an ESP32-C6 Wi-Fi coprocessor. The program
composes the runtime with `pocket_guest_quickjs`, two `pocket_render_esp_ppa`
renderers split across the cores (`pocketjs_esp32p4_ppa_lanes`), the
`pocket_display_esp_dpi` output and `pocket_runner`. Those components, the
IDF archive and `engine/host-services` come from `third_party/pocketjs`; the
program, its components and its host contract live here.

The program was imported from pocket-nexus/pocketjs#616 (`df0d434a`), whose
platform and engine changes reached pocketjs `main` through
pocket-nexus/pocketjs#617.

| Host contract | Launcher | Picture |
| --- | --- | --- |
| `tab5-home` (`pocket.host.json`, through `pocket.host.config.ts`) | Pocket Shell from `apps/pocket-shell` | the auxiliary surface, 640x360 at raster density 1, turned and presented at 2x |

`pocket.host.json` (format `pocket-host/2`) states the facts: one cadence,
30 Hz; a 400x240 primary viewport and the 640x360 auxiliary surface, both
`native` at raster density 1; animation class `repaint`; the capabilities;
and a worker table of one `http`, two `i2c` and one `uart` node, the nodes
`main.c` gives every app's worker domain.

The firmware is a launcher console: `components/pocket_system`
keeps a store of installed apps on the microSD card (SDSPI; the `storage`
flash partition without a card), the Pocket Link to a paired companion,
each app's `data.fs` tree, the Wi-Fi radio on the ESP32-C6 (esp_hosted) and
the wall clock from SNTP. The build compiles Pocket Shell
(`apps/pocket-shell`) with `pocket.host.config.ts`, embeds it as the
launcher and boots one runtime per app; SELECT from the home control returns
to the launcher. The build also compiles the apps that
`POCKETJS_TAB5_BUNDLED_APPS` lists (default
`apps/tab5-demo;apps/worker-http;apps/unit-swap`)
and embeds them; `pocketjs_system_start` admits each once and serves it from
flash as an installed app, so a bundled app is never copied to the card or
into PSRAM. `-DPOCKETJS_TAB5_SHELL_PACKAGE=<package.pocket>` embeds a package
the host contract admits in place of Pocket Shell. The primary surface is
laid out into a PSRAM buffer that nothing scans out.
`pocket_idf_package_open` (the IDF archive) admits a package by the
exported host facts (`pocket_profile.facts`) before the runtime boots it,
once at start for the embedded packages and at each load for a package read
from the card; the runtime then reads the bytes in place without a copy or
a second hash pass (`POCKET_MEMORY_PACKAGE_VERIFIED`), and `pocket_runner`
ticks at the cadence the runtime reports for it. `components/pocket_camera` serves
`globalThis.camera` (the SC202CS on MIPI-CSI: a 320x240 preview at 15 fps,
QR decoding, JPEG stills on the card) and `components/pocket_media` serves
`globalThis.media` (the companion's stream on the ES8388 speaker) to a guest
whose plan declares those capabilities. The camera's capture task runs on
core 1 and opens the video device from a task on core 0: the CSI controller
takes a DW-GDMA channel interrupt, and the DPI panel holds the DW-GDMA source
on core 0 (a shared source routes to one core).

`components/pocket_audio` owns both codecs on one I2S port at 44.1 kHz and
serves `globalThis.audio` (`audio.pcm` streams mixed on the ES8388,
`audio.capture` inputs from the ES7210: the two microphones, a headset
microphone and the speaker loopback); the media stream plays through its
mixer. `components/pocket_device` serves `globalThis.device`
(`system.device`): backlight, speaker level, SoC temperature, the power rails
and plug detection on the two PI4IOE5V6408 IO expanders, 22 free header pins,
the card's directory listing and power off. At boot it sets the rails as
M5Stack's vendor firmware does (battery and QC charging, USB-A 5V and EXT 5V
on, the internal antenna) and starts the USB host that detects USB-A devices.
The codecs and the USB host install from a task on core 1, so their
interrupts are core 1's. The panel is read from the touch controller
(ST7121, ST7123 or ILI9881C). `globalThis.wifi` also serves an open access
point beside the station connection (APSTA), with an HTTP page at
192.168.4.1.

A UI realm has no network module. An app whose package carries a worker
domain (`domains.worker`, package section 8) gets its own worker runtime
(`pocket_worker_esp`) with four nodes:

| Node | Kind | Driver |
| --- | --- | --- |
| `http` | `http` | `pocket_http_esp`: HTTP and HTTPS the Tab5 makes over Wi-Fi (ESP certificate bundle, mbedTLS buffers in PSRAM, certificate dates checked against the SNTP clock) |
| `sys` | `i2c` | `pocket_io_esp` on the BSP's system bus (G31 SDA, G32 SCL, 400 kHz): any address, shared with the board's drivers |
| `porta` | `i2c` | `pocket_io_esp` software bus on Port A (G53 SDA, G54 SCL, 100 kHz), both hardware controllers being taken |
| `rs485` | `uart` | `pocket_io_esp` on UART1 (TX G20, RX G21) in RS485 half duplex, driver enable on G34 |

The worker starts before the realm evaluates, its channels mirror into the
realm once per frame (`before_tick`), and it stops before the realm and the
package bytes are released. Its QuickJS heap (1 MiB) and its 24 KiB task
stack are in PSRAM; it runs on core 1 at priority 3, below the render lane,
the I2C executors at 4 and the two HTTP request tasks at 2. An app whose
worker fails returns to the launcher with the failure shown. The `tab5-home`
host contract lists `runtime.worker`, `net.http`, `io.i2c`, `io.uart`,
`audio.pcm`, `audio.capture` and `system.device`.
`pocket_worker_esp` requires ESP-IDF 6.2.x. `apps/worker-http` is the HTTP
device check: it joins a Wi-Fi network and fetches a URL once connected and
on each tap. `apps/unit-swap` drives one Port A unit at a time (ENV III,
RFID 2, Ultrasonic I2C) and switches drivers when the unit changes.
`apps/tab5-demo` exercises the other nodes and services.

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
under 6 logical px of travel) sends one tick of SELECT to Pocket Shell, and
from an app it summons the launcher instead (`pocketjs_system_summon`); a
finger that moves at once drags the ball anywhere inside the picture; a
finger that rests longer and then travels 24 px makes a gesture, logged by
its direction (`ball gesture: up`) where an effect attaches later.

The Tab5 keyboard (M5Stack A164, `tab5_keyboard.c`) answers at I2C address
0x6D on Ext.Port1 (SDA GPIO 0, SCL GPIO 1, the program's second I2C bus).
The input source declares the TEXT class with or without a keyboard, so the
host contract's `input.text` holds and the firmware boots without one. The
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
git submodule update --init third_party/pocketjs
cd programs/m5stack-tab5
idf.py -p /dev/cu.usbmodem1201 build flash monitor
```

Builds run the Pocket CLI through `tools/pocket` (`POCKETJS_CLI`) and, with
`POCKETJS_RUST_FROM_SOURCE=ON` (the default here), need a Rust toolchain with
the `riscv32imafc-unknown-none-elf` target. The image with Pocket Shell and
the three bundled apps is 7.3 MiB of the 8 MiB `factory` partition (with
Pocket Shell alone 4.1 MiB). `-DPOCKETJS_TAB5_BUNDLED_APPS=` builds without
them. The log reports the frame rate every five seconds.

The applets install from a paired Mac's catalog, and an app installs from an
install QR code that Pocket Shell scans (`components/pocket_system`,
"Installs from a code"). An applet compiled with
`--host-config programs/m5stack-tab5/pocket.host.config.ts` by a pocketjs at
or after pocket-nexus/pocketjs#631 installs here; pocket-shell's `bun run
tab5` reads the former `home.runtime-config.ts` and predates #631. A UI realm
has no network module: an app reaches the network through its worker's
`io.http`, and one that imports `@pocketjs/framework/net`, removed from
pocketjs, does not compile.
