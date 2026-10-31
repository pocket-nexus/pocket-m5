# Tab5 Demo

The M5Stack Tab5's hardware on one screen, after M5Stack's
[M5Tab5-UserDemo](https://github.com/m5stack/M5Tab5-UserDemo) (its launcher
panels and tool windows), for `programs/m5stack-tab5` (profile `tab5-home`,
640×360 auxiliary surface). The layout, artwork and music are this app's own;
no file of the vendor demo is copied.

## Panels

| Panel | Source | Shows and sets |
| --- | --- | --- |
| RTC | RX8130CE at 0x32, worker `sys` node | time and date (UTC), validity (VLF, STOP); tap for the setting window |
| Battery | INA226 at 0x41 (5 mΩ shunt), worker | bus voltage, current, power at 5 Hz; SoC temperature from `device.status()` |
| Backlight | `device.setBrightness` | 20 % to 100 % in steps of 20, the detected panel (ST7121, ST7123, ILI9881C) |
| Speaker | `device.setVolume` | 0 % to 100 % in steps of 20 |
| Power rails | `device.setSwitch` | battery charging, QC charging, USB-A 5V, EXT 5V, external antenna; USB-C, USB-A device and headphone presence |
| Power | `device.powerOff`, worker | power off after a 15 s countdown; sleep with the backlight off until a touch; power off with the BMI270 any-motion interrupt or the RX8130CE alarm armed to power the board back on |
| IMU | BMI270 at 0x68, worker | acceleration and angular rate at 20 Hz; a dot that rises to the high side |

## Tool windows

| Tile | Source | Does |
| --- | --- | --- |
| Camera | `globalThis.camera` (camera.qr) | 320×240 preview of the SC202CS and decoded QR codes |
| Mics | `openAudioInput("microphones")` | live peaks of the left and right microphones; records 3 s at 22050 Hz and plays it back |
| Headset | `openAudioInput("headset")` | the same for a headset microphone |
| Music | `startPlayer`, `openAudioInput("loopback")` | plays an original eight-bar tune synthesized on the device (11025 Hz, integer sine table) and shows the speaker loopback the ES7210 captures |
| SD card | `device.listDirectory` | names and sizes on the card, 6 rows a page, up to 64 entries a directory |
| I2C | worker `sys` and `porta` nodes | address-only probes of 0x08..0x77 on the system bus or Port A (G53 SDA, G54 SCL); the EXT 5V switch powers Port A |
| GPIO | `device.setPin`, `device.releasePin` | drives the free Ext.Port1 and M-Bus pins high or low; closing releases them |
| RS485 | worker `rs485` node | UART1 at 115200 8N1 (TX G20, RX G21, DE G34): sends `Hello M5Stack!` and shows received lines |
| Wi-Fi | `globalThis.wifi` | the station state, a scan, joining open or saved networks, and an open access point `M5Tab5-PocketJS` serving a page at its address |

## Worker

`worker.ts` drives the sensors in TypeScript over the `sys` I2C node:
`drivers/rx8130ce.ts` (see [RX8130CE clock setting](drivers/RX8130CE.md)),
`drivers/bmi270.ts`, `drivers/ina226.ts` and `drivers/wake.ts` (the RTC alarm
and the motion interrupt). One 20 Hz timer reads the IMU, reads the battery
monitor every fourth tick and publishes the `sensors` snapshot; each awaited
transfer is a worker turn, and turns on the PSRAM heap share the cache with
the renderer, so the sensors follow the display's rate. Commands arrive on the
`commands` ring; `protocol.ts` lists the record layouts.

An alarm or motion wake-up is armed before `device.powerOff()`. On USB power
the Tab5's power controller keeps the board on; the window reports it and the
worker disarms both interrupts. The BMI270 feature image is from Bosch
SensorAPI v2.86.1; [BOSCH-LICENSE.txt](drivers/BOSCH-LICENSE.txt) applies to
that image.

## Build

```sh
bun run app build apps/tab5-demo/pocket.json
idf.py -C programs/m5stack-tab5 \
  -DPOCKETJS_TAB5_HOME_PACKAGE=$PWD/.pocket-build/apps/tab5-demo.pocket build
```

Embedded as the home package the demo runs in place of Pocket Home; building
with `-DPOCKETJS_TAB5_HOME_PACKAGE=` restores Pocket Home. The baked glyphs
cover printable ASCII; a file name outside it shows `?`.
