# pocket_system

The system services of the Tab5 as a launcher console: installed apps and
switching between them, the Pocket Link to a paired companion, a guest's
`data.fs` tree, the Wi-Fi radio, the SNTP clock and installs from an install
QR code.

- Public header: `pocket/system.h`
- Shared state machines: `engine/host-services` (`link.c`, `launcher.c`,
  `fs.c`), compiled from the repository
- `esp_wifi` comes from `espressif/esp_wifi_remote` and `espressif/esp_hosted`,
  which the program lists with its coprocessor's pins

## Store

`pocketjs_system_start` takes one mounted directory and the name of a data
partition:

| Place | Contents |
| --- | --- |
| `link/` | the device id and one file per paired companion |
| `apps/<slot>/app.txt` | the launcher's cached facts (output, id, title, version) |
| `apps/<slot>/data/` | the guest's `data.fs` tree, quota from `applet.store` |
| `apps/<slot>/link-upload.pocket`, `url-upload.pocket` | an install in progress, from the companion or from a code |
| the `packages_partition` (`apps`) | the installed packages, one extent of 64 KiB pages each |
| `apps/<slot>/package.pocket` | a package installed when the firmware had no `apps` partition |

`<slot>` is the first 16 hex digits of SHA-256 of the app id. An install
streams into `apps/<slot>/link-upload.pocket`, is admitted
(`pocket_idf_package_open`: footer, FNV-1a64, then the first variant the
firmware's host facts satisfy: binding generation, raster density, animation
class, tick rate, viewport, presentation, capabilities and auxiliary surface)
and is programmed into the first free run of pages of the `apps`
partition (`flash_store.c`): a 64-byte header, the package from offset 64,
padding to the page. The bytes go first and the header last, so an
interrupted install leaves free pages, never a package the firmware would
refuse; a replaced or removed package has the state word of its header
cleared in place. The whole partition is mapped read-only once, so an
installed package is read from flash where it is, like an embedded one. The
flash writes take a few seconds for a 1.6 MiB package and stall the other
tasks for each chunk, and the running app cannot be replaced: an install
into its slot is refused until the launcher runs. Without the partition, or
when no run of pages is free, an install fails with that reason; a
`package.pocket` an earlier firmware kept under the store is moved to the
partition at start when it fits and is read into PSRAM at each launch
otherwise.

Start lists every package the partition holds: a package whose slot
directory is missing from the store (another card, or a partition written
over USB by `tools/apps-partition.ts`) gets the directory, and the launcher
reads its facts from the package on the next table scan.

The `bundled` packages of `pocketjs_system_config_t` stay in the firmware
image: start admits each once and keeps its facts, removes a
`package.pocket` or installed copy an earlier firmware kept for its slot,
and rewrites the slot's `app.txt` when the cached hash differs. They are
installed apps the companion can neither replace nor remove. The runtime
reads every package where it is, without a copy or a second hash pass
(`POCKET_MEMORY_PACKAGE_VERIFIED`): the admission here is the hash pass.
`pocketjs_system_mount_flash` mounts a wear-levelled FAT partition when no
card is available.

## Guest lifecycle

The program loads the launcher (`pocketjs_system_load_launcher`), creates the
guest, calls `pocketjs_system_bind` with its realm and boots the runtime
with the app's bytes as its package source. Each tick, before the turn, it
calls `pocketjs_system_pump`; SELECT outside the launcher calls
`pocketjs_system_summon`. When `pocketjs_system_switch_pending` becomes true,
the program stops the runner, destroys the runtime and guest, calls
`pocketjs_system_unbind`, takes the target with `pocketjs_system_take_switch`
and boots it.

## Guest namespaces

| Global | Contract |
| --- | --- |
| `ui.appTable`, `ui.appLaunch`, `ui.appShot` | the `applications` operations, served by the program's operation provider from `pocketjs_system_app_table` and `pocketjs_system_launch` |
| `link` | `contracts/spec/link.ts` |
| `wifi` | `contracts/spec/wifi.ts`, with `scan`, `join` and `forget` |
| `fs` | `contracts/spec/fs.ts`, when the plan's features hold `data.fs` |
| `installer` | the launcher only: installs from an install QR code (below) |

The radio keeps three saved networks in NVS (namespace `pocketwifi`), joins
the most recently used at start, and saves a joined network once it has an
address. Storage and NVS are flash, so every call runs on one task with an
internal stack while the guest's task waits. A scan start, a scan result and
an RSSI read each cross the SDIO link to the ESP32-C6 and wait for its answer:
the `pocketjs_radio` task (8 KB, internal RAM) makes those calls, a scan when
the guest asked for one (`scan_requested`, `scan_done`) and the RSSI read on
an interval while connected; `status` answers from the cached state without
touching the link.

## Installs from a code

Pocket Shell installs an app from a QR code that holds an install URI:

```
pocketlink://install?v=1&a=<app id>&s=<sha256>[&n=<title>]&u=<url>
```

`a` is the manifest id the package carries, `s` the SHA-256 of the whole
`.pocket` file in 64 lowercase hex digits, `n` an optional percent-encoded
title, and `u` the package's `http://` or `https://` address. `u` comes
last and runs to the end of the URI, taken as written, so an address keeps
its own `&` and `%`; it holds no `#` or whitespace and is at most 200
bytes. A URI is at most 400 bytes. `install_uri.c` reads it;
`tests/install-uri.c` runs it under ASan and UBSan.

Only the launcher gets the namespace (`install.c`):

| Function | Result |
| --- | --- |
| `installer.installUri(uri)` | a request id, or `-1` when the URI does not parse or an install is running |
| `installer.poll()` | one JSON array of the events since the last call, or `undefined` |

The events have the shapes of the link's install events:
`{"t":"install.progress","id","app","received","bytes"}` (`bytes` is 0 while
the server has not said the length), then `{"t":"install.done","id","app","output"}`
or `{"t":"error","id","message"}`.

`fetch.c` downloads over `esp_http_client`. One download runs at a time, on
the `pocket_fetch` task: 12 KB of internal RAM for its stack, because it
writes the store, held only while the download runs. It waits for an address,
and for an `https://` address for the SNTP clock (at most 15 s), since
certificate dates are checked against it; with no SNTP answer by then it sets
a clock earlier than the firmware's build time to the build time, so a
certificate issued before the build passes, and SNTP corrects it later.
HTTPS uses the ESP certificate bundle with mbedTLS buffers in PSRAM. It
follows at most three redirects, accepts only a `200`, refuses a body over
24 MiB, writes 16 KB reads to `apps/<slot>/url-upload.pocket` and hashes
them with SHA-256. A connect or a read waits at most 10 s; a read that times
out is tried again until no byte has come for 30 s. `install_pump`, on
the system task once per frame, compares the hash with the URI's and commits
the package with `link_host_install`, the companion's path: it admits the
package by the firmware's host facts (`pocket_idf_package_open`) and refuses
one whose app id does not hash to the slot of `a`, as it does for a package
read from any slot. A guest switch keeps the download running and drops only
its events.

## Companion services

A worker reaches the paired companion's services at
`https://companion.pocket/<service>/...` (pocketjs `docs/APPLETS.md`;
`LINK_SERVICES_HOST` in `contracts/spec/link.ts`) through its `http` node.
`pocketjs_system_companion` is the `pocket_http_esp` transport for that host:
`companion.c` sends the request over the Pocket Link as a `net.stream` request
with engine/host-services `link_http.c`, which the link client carries when
built with `POCKETJS_WORKER_HTTP`. The request names the running app, and the
companion answers it for the services that app declares. The request task
makes each call on the system task (`system_run`), where the link runs.
Every other host goes out over the radio as before.

| When | The request fails with |
| --- | --- |
| no companion is paired | `unavailable: no Mac is paired` |
| the paired companion is not linked | `unavailable: the paired Mac is not connected` |
| the companion offers no `net.stream` | `unavailable: the paired Mac offers no companion services` |
| the link closes during the request | `unavailable: the Pocket Link closed`, status `unknown` |

The link client holds two requests (`LINK_HTTP_SLOTS`), each with a 1 KiB
headers record and an 8 KiB receive window: 18,608 bytes of internal RAM in
`.bss` (`idf.py size-files`). `tests/companion-route.test.ts` builds
`companion.c` with the link client and runs it against pocketjs's companion
in the test process.

## The link over USB-C

The Tab5's USB-C port is the ESP32-P4's USB Serial/JTAG controller: one
CDC-ACM serial line (USB 303a:1001), which is also the console
(`CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG`, no secondary console). `usb_link.c`
carries the Pocket Link over it, so a companion at the other end of the
cable (a phone in USB host mode, or a computer) pairs and links without the
camera or Wi-Fi.

The link client (`engine/host-services/link.c`) connects over TCP only.
`usb_link.c` listens on **127.0.0.1:8628** and carries the connection the
link client opens there over the cable; `link_host_net_ready()` holds while a
companion answers on the cable, Wi-Fi or not. The driver
(`usb_serial_jtag_driver_install`, 16 KiB each way) takes the console's
writes as well (`usb_serial_jtag_vfs_use_driver`), and a packet is one driver
write, so a log line lands before or after a packet, never inside it.

A packet is `F5 50 type len16 payload crc16`, little endian, at most 2048
payload bytes; the CRC is CRC-16/CCITT-FALSE over type, length and payload.
`0xF5` never occurs in UTF-8, so a reader takes every byte outside a valid
packet as log text.

| Type | Direction | Payload |
| --- | --- | --- |
| `01` HELLO | console to companion, every 1 s | `link.status()` JSON |
| `02` ATTACH | companion to console, every 1 s | none: a companion is on the cable (3 s without a packet: gone) |
| `10` OPEN | console to companion | connection id: the link client connected to 127.0.0.1:8628 |
| `11` DATA | both | connection id, then the connection's bytes |
| `12` CLOSE | both | connection id |
| `20` PAIR | companion to console | a `pocketlink://pair` URI, run as if scanned (`link.pair`, on the system task) |
| `21` BEACON | companion to console | a 64-byte Pocket Link beacon, sent to the link client's beacon socket from 127.0.0.1 |

A companion that finds itself missing from HELLO's `paired` list sends PAIR
with host 127.0.0.1 and port 8628: the cable is the trust boundary, as it is
for Pocket Link's USB carrier. The pairing stores 127.0.0.1 as the
companion's address, and the link client also follows the same companion's
beacons on the LAN once the cable is out. pocket-netease-android's
`UsbCarrier` is the companion side.

## Clock and worker support

`clock.c` starts the SNTP client (`esp_netif_sntp`,
`CONFIG_POCKET_SYSTEM_SNTP_SERVER`, `pool.ntp.org` by default) each time the
radio is assigned an address, so a new network synchronizes the clock within
one exchange; lwIP then synchronizes every `CONFIG_LWIP_SNTP_UPDATE_DELAY` ms
(one hour). The clock is UTC and backs `gettimeofday` and the guest's `Date`.
`pocketjs_system_online` and `pocketjs_system_clock_wait` serve the program's
HTTP node from any task; `pocketjs_system_app_worker` and
`pocketjs_system_app_worker_bytecode` return the worker source and the
worker bytecode section of an app (sections 8 and 9 of the admitted variant,
found by its variant hash), whose bytes live as long as the app.
