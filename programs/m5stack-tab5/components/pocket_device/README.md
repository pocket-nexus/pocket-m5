# pocket_device

`globalThis.device` (capability `system.device`,
`contracts/spec/device.ts` in the pinned pocketjs) on the M5Stack Tab5. The
program installs it into a guest whose plan enables `system.device`.

| Op | Mechanism |
| --- | --- |
| `status()` | JSON: backlight, panel, speaker level, SoC temperature, switches, plugs, free pins, card size and free space |
| `setBrightness(percent)` | the BSP's LEDC backlight on GPIO 22 |
| `setVolume(percent)` | the speaker level `pocket_audio` applies to the ES8388 |
| `setSwitch(id, on)` | a pin of the PI4IOE5V6408 IO expanders (table below) |
| `setPin(pin, level)`, `releasePin(pin)` | a push-pull output on one of the pins the program lists; release resets the pin |
| `listDirectory(path)` | up to 64 names and sizes below the card's mount point; `.` and `..` segments are refused |
| `powerOff()` | backlight off, then three 100 ms pulses on the power-off line |

| Id | Expander pin | On |
| --- | --- | --- |
| `charge` | 0x44 P7 | high |
| `quick-charge` | 0x44 P5 | low |
| `usb-5v` | 0x44 P3 | high |
| `ext-5v` | 0x43 P2 | high |
| `antenna` | 0x43 P0 | high (external antenna) |

USB-C presence is 0x44 P6 and headphone presence 0x43 P7, both inputs; the
power-off line is 0x44 P4. The pin assignment and the defaults (charging, QC,
USB-A 5V and EXT 5V on, the internal antenna) follow M5Stack's
M5Tab5-UserDemo; `pocket_device_setup` applies the defaults at boot through
the BSP's `esp_io_expander` handles. USB-A presence counts the USB host
library's device events; the setup starts the host with
`bsp_usb_host_start`.

`pocket_device_detect_panel` reads the ST712x touch controller's firmware
byte at register 0x0000 (1: ST7121, 3: ST7123) or finds a GT911
(ILI9881C), as the BSP does when it chooses the panel driver.

Every op runs on the component's task, whose 6 KB stack is internal RAM: the
guest's stack is in PSRAM, and the card's FAT reads go through the SPI
driver. FAT counts free clusters by reading the whole table (about 400 ms on
a 32 GB card), so the card's figures are read at setup and with each listing,
not with each status. A FAT short name arrives in the card's OEM code page;
a byte that does not start a UTF-8 sequence is listed as `?`.

On USB power the Tab5's power controller keeps the board on after the
pulses; `powerOff` then restores the backlight and returns false. Guest
teardown releases the pins the guest drove and restores the backlight to
100 %.
